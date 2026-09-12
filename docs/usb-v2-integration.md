# H7/F4 V2 USB 统一调用链

日期：2026-09-10。本文记录当前实现，不把编译或模拟测试当作硬件枚举结果。

## 1. 实际差异与处理方式

本次对照了 ST 官方 H723ZG、F469 CDC ACM 示例，以及本工程
`h723_mc02`、`f407_c_board` 的实际 Board 输出。F469 示例用于理解旧版
USBX 模板，F407 的 PCD、时钟、GPIO、IRQ 仍以本工程 CubeMX 输出为准。

| 项目 | 修改前 | 统一后 |
| --- | --- | --- |
| 公共 API | 头文件已经相同 | 保留 `init/config/callback/try_send/state` |
| USBX 启动 | 两份 Board 应用模板各自实现 | 两个 BSP 分支使用内容一致的 `bridge_usb.c` |
| PCD 启动 | H7 在 USBX device thread；F4 在 BSP init | 都在 USBX device thread，和应用 I/O init 分离 |
| CDC 回调 | 依赖生成模板中的转发函数 | USBX 直接注册 BSP 私有 bridge 函数 |
| 配置/接口编号 | H7 getter；旧 F4 写死 1/0 | 从生成的 framework 读取 CDC ACM 的实际编号 |
| HS framework | 容易按 H7/HS 控制器名称判断 | 按已初始化 PCD 的实际 speed 决定是否提供 |
| PCD handle | 实现写死 HS/FS 名字 | IOC 生成私有 `pnx_usb_pcd()` 绑定 |
| FIFO | 固定端点编号和大小 | 使用生成描述符的端点地址/MPS，保留 HAL 要求的 FIFO 间隙 |

当前 H7 的 `USB_OTG_HS` 配置为 `PCD_SPEED_FULL`、embedded PHY；F4 使用
`USB_OTG_FS`，也是 Full Speed。两者 `dma_enable` 均为 `DISABLE`。
控制器名称中的 HS 不表示当前链路运行在 High Speed。

## 2. 启动链

```text
CubeMX main
  -> MX_USB_OTG_*_PCD_Init()
     初始化实际 PCD / GPIO / clock / IRQ
  -> MX_ThreadX_Init() / tx_kernel_enter()
  -> tx_application_define()
     -> App_ThreadX_Init() -> app_start()
        -> 应用按需调用 bsp::usb::init(config)，创建 RX/TX workers
     -> 创建 CubeMX USBX byte pool
     -> MX_USBX_Device_Init(pool)        [BSP 提供的替换实现]
        -> tx_byte_allocate / ux_system_initialize
        -> 生成 FS、可选 HS、string、language frameworks
        -> 从 FS framework 读取 CDC 配置/接口编号
        -> ux_device_stack_initialize
        -> ux_device_stack_class_register，注册私有 CDC callbacks
        -> 创建 USBX device thread
  -> ThreadX 调度开始
     -> device thread: FIFO -> DCD(instance, handle) -> HAL_PCD_Start
```

启动阶段创建的线程在 ThreadX 初始化结束后开始调度。控制器启动不依赖
USB demo 是否启用，也不由 `bsp::usb::init()` 再启动一次。生成的 main
继续拥有 PCD 初始化；BSP 不复制或调用第二次 `MX_USB_OTG_*_PCD_Init()`。

`MX_USBX_Device_Init()` 返回分配、USBX 栈注册或线程创建结果。device thread
中的控制器启动失败使用 Board 现有 `Error_Handler()`，没有新增重试、回滚
或恢复状态机。此启动钩子仍是初始化阶段的一次性入口。

## 3. 数据调用链与语义

```text
主机数据 -> IRQ / HAL PCD -> STM32 USBX DCD -> CDC read
         -> BSP RX worker -> 应用 on_rx_callback(bytes, length)

应用 try_send(bytes)
  -> 复制到一个 pending slot -> 唤醒 TX worker
  -> 复制至 in-flight buffer -> CDC write -> DCD / HAL / USB
  -> on_tx_result_callback(result)
```

- RX callback 在 USB 读线程，TX result callback 在 USB 写线程。
- callback target 为非拥有对象，须持续存活；RX buffer 仅在回调期间借用。
- `try_send()` 只能在线程上下文调用，当前上限 512 字节，返回前复制输入。
- `ok` 表示请求进入待发送槽，不是主机已收到；最终结果由 TX callback 报告。
- 一个 pending slot 可以与一个 in-flight write 同时存在，不新增队列深度配置。
- `connected()` 表示 CDC 实例存在且 USBX device configured，不表示 DTR 或应用握手完成。
- `state()` 保留现有诊断视图，不将其声明为跨线程原子快照。
- `init()` 首次成功后重复调用不替换配置；应用不应将它作为失败恢复入口。

## 4. CubeMX 文件保持原样

父工程在 `add_subdirectory(CubeMX)` 后执行
`configs/cmake/usbx_integration.cmake`，从产品目标移除：

- `USBX/App/app_usbx_device.c`；
- `USBX/App/ux_device_cdc_acm.c`。

对应功能统一由所选 BSP 的 `usb/src/bridge_usb.c` 提供。
上述生成文件仍留在源树，不修改 USER CODE 区，也不通过宏重命名或 weak/linker
wrap 隐式截获函数。再次运行 CubeMX 后，父工程会继续做相同的源选择。

保留并使用：

- `ux_device_descriptors.c/.h` 和生成的 USBX/ThreadX 配置头；
- `usb_otg.c/.h`、main、HAL MSP 和 IRQ；
- CubeMX 的 USBX middleware、STM32 DCD 和 HAL 驱动。

USB 适配源只位于 BSP，HAL/USBX bridge 头位于 `usb/src`，公共入口仅为
`usb/include/bsp_usb.hpp`。H7/F4 两个分支保留相同源结构，不新增第三个公共
BSP 仓库或运行时 MCU 选择。

每个分支只保留四个 USB 文件：

```text
usb/include/bsp_usb.hpp  公共 API
usb/src/bsp_usb.cpp      应用收发线程与状态
usb/src/bridge_usb.h     私有 PCD 绑定与 CDC handle 声明
usb/src/bridge_usb.c     USBX 初始化、描述符解析、FIFO/PCD 启动、CDC 回调
```

描述符和控制器辅助函数、CDC callbacks 均为 `bridge_usb.c` 内部静态函数，
不再通过独立头文件形成适配层。生成绑定仍由生成器写入 build 目录。

`build.usbx=false` 时，父工程排除 USB BSP、USB demo、描述符和 USBX object
库。因为生成的 `tx_application_define()` 仍无条件调用启动钩子，build 目录
生成一个空启动钩子，使禁用的子系统不启动栈或控制器；这不是可调用的公共 USB
stub，不会给不存在的 USB 硬件报告 `bsp::usb::init()` 成功。生成的 PCD 初始化
和 byte pool 声明仍保留，避免为优化这部分代码修改 CubeMX 文件。

独立打开 Board 自带工程不经过父工程的替换规则；V2 正式入口仍是父工程
`PNX_BOARD` 流程。

## 5. 描述符与私有绑定

生成器从 IOC 的 USB OTG 外设列表生成 `pnx_usb_pcd()`，返回当前配置的
`hpcd_USB_OTG_HS` 或 `hpcd_USB_OTG_FS`。公共 BSP 不暴露该句柄。
当前契约选择一个 CDC ACM 控制器，生成阶段只需确认唯一控制器存在。

本工程 F4 模板没有 H7 的 `USBD_Get_Configuration_Number()` /
`USBD_Get_Interface_Number()`。为保持生成文件原样，私有小型 descriptor walker
直接读取配置描述符的 `bConfigurationValue` 和 CDC ACM 控制接口的
`bInterfaceNumber`。它只做长度边界和 CDC 类型匹配，不实现 composite registry。
模拟测试将其结果与 H7 现有 getter 比较，并使用 F4 原始 descriptor builder 验证。

FIFO 端点编号来自 `USBD_CDCACM_*_ADDR`，包长来自当前速度对应的 MPS。
HAL 通过低编号 FIFO 计算后续 FIFO 偏移，因此未使用的中间 FIFO 也保留最少
16 words。DCD 第一个参数取 `pcd->Instance`，第二个参数取同一个 PCD 指针，
不照搬 F469 示例中 FS handle/HS instance 的不一致组合。

## 6. 内存与支持范围

USBX heap 优先使用生成的 `USBX_DEVICE_MEMORY_STACK_SIZE`。旧 F4 模板未定义
该宏时，采用 H7 当前基线的 7 KiB，device thread 为 1 KiB、priority 10。
这样不再请求占满 F4 当前 10 KiB byte pool 的 10 KiB heap；pool 还需保存
分配开销和 device thread stack。生成头若提供相关宏，以生成值为准。

当前 PCD 采用 FIFO I/O，不进行 USB PCD DMA。H7 的应用工作缓冲区继续使用
既有 `RAM_D1_BSS` 属性，F4 由已有 memory 宏映射到普通 SRAM。没有因为 H7
带 D-cache 就对每次 CDC read/write 增加 clean/invalidate。

若以后在 IOC 启用 PCD DMA，当前私有初始化明确返回不支持。原因是 USBX
内部 endpoint/control buffer 和 heap 也涉及 DMA，不能仅给 BSP 的 RX/TX 数组
加对齐就宣称支持。届时应在具体需求下明确整个 USBX 内存路径；本轮不扩展它。

高速 framework 是可选路径，已做逻辑模拟测试；当前两块板实际配置均为 FS，
没有验证真实 HS PHY 或链路。多 CDC、其他 composite class 和 USB-UART 桥接
不属于本轮契约。

## 7. 验证结果与限制

2026-09-10：

| 验证 | 结果 |
| --- | --- |
| H7 `h723_mc02` Debug 完整编译链接 | 通过 |
| H7 `h723_mc02` Release 完整编译链接 | 合并后通过，FLASH 124764 B、DTCMRAM 80208 B |
| H7/F4 USB 2 个源文件分别编译 | 通过，`-Wall -Wextra -Werror` |
| ARM 模拟 descriptor/controller/bridge 测试 | 两板通过，运行真实编译代码并调用原始 CubeMX descriptor builder |
| USBX enabled/disabled CMake 源选择 | 两种情况均通过，未配置 F4 产品 |
| H7/F4 当前硬件枚举与连续传输 | 未运行 |

Release 构建的 USBX 原始 middleware 有编译器 array-bounds 警告；未通过修改
生成代码或屏蔽整个工程告警处理它。上述 USB BSP 局部编译无警告。

F4 按 `AGENTS.md` 仍要求先重新生成 Board 输出，再进行产品 configure/build。
本轮只做生成器独立调用、USB 局部编译和模拟测试，没有绕过该前置条件。
当前磁盘中的 F4 descriptor 已是 ST 示例 VID/PID `0x0483/0x5710`，并未发现
原文档记载的 identity-confirmed 宏；示例值不等于产品身份已确认。
`build.usbx=false` 保持不变。

测试入口和复现方式见 [tests/usb/README.md](../tests/usb/README.md)。

## 8. 官方参考

- [H723ZG CDC ACM app_usbx_device.c](https://github.com/STMicroelectronics/stm32-usbx-examples/blob/main/Projects/NUCLEO-H723ZG/Applications/USBX/Ux_Device_CDC_ACM/USBX/App/app_usbx_device.c)
- [F469 CDC ACM app_usbx_device.c](https://github.com/STMicroelectronics/x-cube-azrtos-f4/blob/main/Projects/STM32469I-Discovery/Applications/USBX/Ux_Device_CDC_ACM/USBX/App/app_usbx_device.c)

本次直接读取了上述官方文件。它们是实现对照材料，具体外设、端点和速度均由
本工程实际 IOC / CubeMX 输出决定。

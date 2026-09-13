# H7/F4 如何共用一套 PnX

PnX 在 H723 和 F407 上复用同一套 Template、Lib、Device 和 Module，差异只留在构建时选中的 BSP 与 CubeMX Board 中。它不是一份固件在运行时识别 MCU，也不会由 `PNX_BOARD` 自动切换 Git 分支。

正式分支约定如下：

| 仓库 | 分支 |
| --- | --- |
| `pnx_template`、`pnx_libs`、`pnx_devices`、`pnx_modules` | `main` |
| `pnx_bsp` H7 后端 | `stm32h7` |
| `pnx_bsp` F4 后端 | `stm32f4` |

```text
板型 preset + 匹配的 pnx_bsp 分支
              ↓
IOC + board.json + params.json + robot.json
              ↓
构建目录中的配置与私有 HAL binding
              ↓
统一 BSP 公共接口
              ↓
Device → Module → Application
```

## 统一的边界

H7 是公共契约基线。两个 BSP 分支可以有完全不同的 `.cpp`、HAL 类型和中断桥，但对应的公共 `.hpp` 必须保持一致。共享代码只能包含公共 BSP 头并使用 `bsp::` 类型，不能出现 `FDCAN_HandleTypeDef`、`CAN_HandleTypeDef`、`UART_HandleTypeDef` 等 HAL 类型，也不能用 MCU 宏选择业务逻辑。

统一工作的关键规则如下：

- `pnx_bsp` 在 `stm32h7` 和 `stm32f4` 间构建时选择；不增加运行时 BSP 工厂、注册中心或虚接口层。
- 同名公共 `.hpp` 的类型、函数、返回状态与回调语义必须一致。修改一侧公共接口时，必须同步另一侧并完成双板构建。
- HAL handle 和从 handle 反查逻辑只存在于 BSP 私有实现及构建目录生成的 `bsp_bindings.cpp` 中。
- Device、Module 和 Application 使用生成的逻辑名称，例如 `app::uart::referee`、`robot::motors::motor1`，不能写死 `USART1`、`FDCAN2` 或板级引脚。
- IOC/CubeMX Board 拥有外设实例、引脚、时钟、DMA、NVIC 和协议模式；`board.json` 只补充固定接线和内存事实；`params.json` 与 `robot.json` 选择应用功能、参数和设备组成。
- 回调目标不由 BSP 持有。CAN、USART、SPI、ADC 和 EXTI 的 HAL 回调由各自后端桥接到同一个公共 callback 类型；IRQ 路径中的回调不得等待、加锁或执行耗时工作。
- 生成内容位于 `build/<preset>/generated/`，只读且不提交。板型与 BSP family 不匹配时，CMake 必须直接失败。

“接口统一”不表示硬件能力完全相同。共享调用方必须使用公共语义，生成器则应在构建阶段拒绝所选板不支持的组合。

## 需要保留的硬件差异

### DMA 与缓存

H723 是 Cortex-M7，启用 D-cache，存在 DMA 不可访问的 TCM，并需要按 32 字节缓存行维护一致性。F407 是 Cortex-M4，没有 D-cache 一致性操作，但 CCM 同样不能供 DMA 使用。

两板统一使用 `bsp::dma::buffer`、`buffer_view` 和 `BSP_DMA_BUFFER`。H7 后端执行 cache clean/invalidate；F4 对 cache 操作为空操作，但仍检查地址是否处于生成的 DMA 可访问范围。共享代码不能自行硬编码缓存行大小或 RAM 地址。

### Classic CAN 与 CAN FD

公共 CAN 接口统一提供 `init`、`restart`、`transmit`、`rx_frame` 和 `rx_callback`：

| 板型 | 后端 | 支持范围 |
| --- | --- | --- |
| H723 | FDCAN | Classic CAN；IOC 与配置允许时也可使用 CAN FD/BRS，单帧最多 64 字节 |
| F407 | bxCAN | 仅 Classic CAN，不支持 FD/BRS，单帧最多 8 字节 |

`robot.json` 中的 `can_type` 描述设备要求，不能改变 IOC 的硬件模式。F4 选择 FD、引用不存在的 `fdcan*` 或发送超过 8 字节时必须被生成阶段或 BSP 拒绝；不要为此在公共 API 增加运行时 MCU 判断。

### EXTI

公共 EXTI 接口相同。F4 在 BSP 内定义 `HAL_GPIO_EXTI_Callback` 并转发，H7 当前由 CubeMX Board 的 USER CODE 调用同一个私有 dispatch。桥接位置不同不应传播到 Device 或 Module。

### PWM failsafe

PWM 对两板使用相同 channel API。可选 failsafe 绑定把一个 PWM channel 连接到单独配置的定时器/update-DMA，在超时后把输出归零。IOC 拥有定时器和 DMA 配置，`board.json` 只描述逻辑关联；BSP 不应知道该通道是否用于 BMI088 heater。

### USB、ADC 与 Flash

USB CDC 的公共回调、状态和 `try_send` 接口一致，USBX descriptor、PCD instance、FIFO 和启动细节留在 Board/BSP 私有层。当前两板 PCD 都使用 FIFO I/O；启用 PCD DMA 前必须另行建立覆盖 USBX 内部内存的 cache-safe 契约。

ADC 只有在所选 IOC 提供合法通道时才进入构建图；没有 ADC 资源不是返回虚假成功的理由。Flash 写能力仍延期，在两板都确定安全数据分区和擦写契约前不属于 V2 公共 BSP。

## 修改后的最低验证

涉及公共 BSP、生成器或共享消费者的修改，至少应完成：

1. 确认 H7/F4 对应公共头在规范化换行后相同，且没有 HAL/MCU 类型泄漏。
2. 分别用匹配 BSP 分支配置并构建 H7 和 F4；不要复用另一板的构建目录。
3. 对涉及的能力增加合法与非法配置检查，例如 H7 FD、F4 Classic、F4 FD 拒绝、DMA 非法内存拒绝。
4. 把“编译链接通过”和“硬件收发、枚举、加热或传感器运行通过”分开记录。

完整切板命令见[首页](../index.md)，配置归属见[配置](../configuration.md)。

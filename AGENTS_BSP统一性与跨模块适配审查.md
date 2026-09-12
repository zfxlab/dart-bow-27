# PNX H7/F4 V2 BSP 统一性与跨模块适配审查

## 1. 审查范围与结论摘要

本报告完整阅读并逐项审查了当前工作区的 `AGENTS.md`。该文件的主题是
PNX BSP H7/F4 V2 重构，内容覆盖架构约束、仓库与分支映射、板级配置边界、
模块公共契约、H7/F4 差异、迁移状态、验证状态和剩余风险。

总体结论：当前设计已经建立了比较清晰的“公共契约统一、后端按 MCU 构建时
选择、板级硬件由 IOC/生成代码拥有”的边界，方向上能够支持 H7 与 F4 共用
`pnx_modules`、`pnx_devices` 和 `pnx_libs`。最重要的统一方式不是让 H7/F4
共享 HAL 实现，而是让上层只依赖 HAL-free 的公共 BSP 头文件、简单类型、
生成的逻辑标识和共同的状态/回调语义；H7/F4 的 HAL handle、反向查找、
FDCAN/bxCAN、D-cache/无 Cache、USB 细节则留在私有实现中。

但是，`AGENTS.md` 主要是架构决策和状态记录，并不等同于跨仓库 ABI/API
兼容性证明。当前最需要继续确认的地方是：

1. `pnx_modules`、`pnx_devices`、`pnx_libs` 的 `main` 分支是否实际只使用
   文件中列出的公共契约，而没有依赖旧 F4 专属 API。
2. 逻辑标识、状态枚举、回调上下文、DMA buffer/view、错误语义和数据宽度
   在三类共享仓库中的定义是否完全一致。
3. F4 当前缺少 SPI/PWM/ADC，且 USB 产品构建被禁用时，上层模块是否正确把
   这些资源视为“未配置”，而不是默认硬件存在。
4. 生成配置、CMake 选择和分支族校验是否能防止“公共头文件来自一个 BSP、
   私有生成绑定来自另一个板”的错配。
5. 已声明的 compile/link 验证尚未覆盖 H7/F4 实际硬件、完整共享模块组合、
   USB 枚举、CAN 总线行为和跨仓库集成测试。

因此，当前架构可以作为 V2 的有效基线，但还不能仅凭该文件证明所有
`pnx_modules`、`pnx_devices`、`pnx_libs` 组合都已兼容。

## 2. 文件确立的总体架构

### 2.1 结构基线

- STM32H7 是结构和公共契约的基线。
- 只有当真实 MCU 差异无法消除时，F4 才在实现层保留差异。
- H7 与 F4 后端在构建时选择，不允许运行时选择 MCU。
- 不新增 BSP 范围的虚拟接口、工厂、注册表、服务定位器或运行时 MCU
  选择机制。
- BSP API 保持小而直接，优先使用命名空间函数和已有简单类型。

这意味着统一目标是“源代码级公共 API 一致”，不是“底层实现完全相同”。
H7/F4 可以使用不同 HAL 和不同内部控制流程，但调用方不应看到这些差异。

### 2.2 配置与代码所有权

配置所有权被分成四层：

| 层 | 负责内容 | 不应负责内容 |
| --- | --- | --- |
| CubeMX `.ioc` 与生成的 Board 代码 | 外设实例、引脚、复用功能、时钟、DMA、NVIC、CAN/FDCAN 模式、USB/ADC/定时器配置 | 应用设备组成和通用 BSP 行为策略 |
| `board.json` | 逻辑设备/角色绑定、主动电平、Cache line、DMA 可访问内存范围，以及 IOC 不自然表达的少量静态板级事实 | 协议、行为策略、HAL 类型、重复硬件配置 |
| `params.json` | 应用/模块参数 | IOC 硬件配置 |
| `robot.json` | 机器人设备组合 | IOC 硬件配置 |

生成配置是只读构建输出，V2 的目标是生成到 build 目录，而不是源目录。
这可以降低生成文件被手工修改、不同板配置互相污染和源树脏文件误提交的
风险。

### 2.3 公共接口与私有绑定

共享公共 BSP 头文件不得暴露：

- STM32 HAL handle 类型；
- MCU 家族符号；
- H7/F4 私有外设类型和反向查找细节。

HAL 回调桥、生成的 handle 绑定和反向 handle 查找都必须是私有实现细节。
上层应该使用生成的逻辑标识。必要的参数检查、生成绑定检查、DMA 内存检查
和 HAL 返回值检查必须保留，但不能重复叠加无必要的验证或推测性的恢复逻辑。

## 3. H7/F4 BSP 统一方式的详细审查

### 3.1 统一得比较好的部分

当前文件明确要求：

- H7/F4 使用同一组公共模块契约；
- 后端通过构建选择，而不是运行时分派；
- HAL 句柄和设备反查留在私有层；
- 通用逻辑使用生成的逻辑 ID，不硬编码物理引脚、句柄、DMA 地址；
- 缺少资源时返回 `not_configured`，不能把不存在的硬件报告为成功；
- 回调目标为非拥有关系，并且需要记录执行上下文；
- CubeMX 资源缺失时必须先修改 IOC 并重新生成，不能手工修改生成源文件；
- F4 的独有遥测、恢复状态机、代际计数器、回滚、guarded operation 和
  生命周期机制不能未经需求证明就进入公共 API。

这些规定直接解决了最容易破坏跨 MCU 统一性的几个问题：公共头文件泄露
HAL、上层通过设备语义识别定时器、F4 历史实现反向塑造 H7 API、以及在
`board.json` 中复制 CubeMX 配置。

### 3.2 仍然属于后端差异的部分

| 能力 | H7 | F4 | 对上层的影响 |
| --- | --- | --- | --- |
| CPU/内存 | Cortex-M7、D-cache、TCM、多个 H7 内存域 | Cortex-M4、无 D-cache、CCM 不可 DMA | 上层只使用 DMA buffer/view；内存合法性由 BSP/生成策略处理 |
| CAN | FDCAN | Classic bxCAN | 公共层使用共同 CAN 初始化/重启/发送/接收帧契约；帧细节和 HAL 留在后端 |
| USB | H7 USB 细节 | F4 USB FS/USBX/HAL 细节 | 公共层只保留简单回调、状态、`try_send`；USB 控制器生命周期留私有 |
| Flash | 擦除扇区/编程粒度与 F4 不同 | 与 H7 不同 | 目前不做公共可写 Flash API |
| 设备资源 | H7 当前有 PWM failsafe 等资源 | 当前 IOC 没有 PWM/SPI/ADC | 资源不存在时不选后端，调用方必须处理 `not_configured` |

这种分层是合理的，但它要求共享模块对“能力缺失”有明确处理。尤其是
F4 当前不是 H7 的资源全集：它配置了 CAN1/CAN2、USART1/USART3/USART6 和
USB OTG FS，但没有 SPI、PWM、ADC。任何把这些能力当作无条件存在的模块都
会破坏统一设计。

## 4. 各 BSP 模块契约与跨仓库适配影响

### 4.1 DWT

H7/F4 公共契约已经匹配，保持现状。需要继续确认上层没有直接依赖某一 MCU
的计数器宽度、时钟来源或寄存器定义。公共接口应表达计时能力，而不是暴露
具体 Cortex-M 寄存器。

### 4.2 GPIO

采用 H7 的生成输入/输出和主动电平契约。板级角色到实际 pin 的映射由生成
配置提供，应用、Device、Module 只应引用逻辑 GPIO 标识。

兼容性要点：

- `active_level` 必须由逻辑语义统一解释，不能让 F4/H7 各自反转一次；
- 不存在的逻辑 GPIO 必须返回 `not_configured`；
- `pnx_devices` 不应保存 STM32 GPIO port/pin 或 HAL GPIO 类型；
- `pnx_modules` 不应自行读取板级 pin。

### 4.3 EXTI

采用 H7 的 typed GPIO-input 加 `core::callback` 契约，原始 HAL 分发在私有层。
F4 已说明使用生成 pin role 和私有 HAL callback bridge。

主要风险是回调执行上下文：中断回调是否允许调用模块方法、是否允许阻塞、
是否可触发设备状态改变，必须在公共契约或相关共享库文档中明确。否则同一
个 Module 在 H7/F4 上虽然能编译，运行时行为仍可能不同。

### 4.4 DMA 与内存

采用 H7 的 `buffer`/`buffer_view` 公共类型和生成内存事实：

- H7 做 D-cache maintenance，Cache line 为 32；
- F4 Cache maintenance 是 no-op；
- F4 仍拒绝 CCM 或其他不可 DMA 的内存；
- F4 生成的 DMA 可访问范围来自板级配置；
- DMA 地址范围和 Cache line 不得硬编码在通用 BSP 逻辑中。

这是跨模块适配中最敏感的部分。USART、SPI、CAN 以及可能的 USB 都可能
经过 DMA，`pnx_libs` 的 DMA 属性和 buffer/view 必须与 BSP 使用同一套所有权、
长度、可写性和生命周期语义。尤其要确认：

- `buffer_view` 是否只借用内存，回调返回后是否仍有效；
- Cache clean/invalidate 的时机是否由 BSP 统一完成；
- 设备层是否会传入栈内存、TCM/CCM 内存或临时容器；
- H7/F4 的地址范围检查是否在相同阶段发生；
- 失败状态是否能被 `pnx_modules` 正确传播。

如果共享库仍保留旧的“调用方自行做 Cache 操作”约定，就会与当前 BSP
“H7 由 BSP 执行 Cache maintenance”的设计冲突。

### 4.5 PWM

公共语义为 H7 的：`init`、`start`、`stop`、`set_duty`、`set_period_us`、
`set_pulse_width_us`，并使用生成的通道绑定。定时器周期由同一定时器上的
所有通道共享。

可选的生成 `pwm_failsafe` 绑定允许某个 PWM 通道通过独立的 timer/update-DMA
路径 fail closed。BSP 不得根据设备语义识别通道。H7 当前将 BMI088 heater
角色绑定到 TIM3_CH4，并使用 TIM6 update DMA；实现本身不得依赖 BMI088 或
heater。F4 当前 IOC 没有 PWM/failsafe，因此不选择该模块。

跨模块风险：

- 如果 `pnx_devices` 通过“heater”“BMI088”等名字调用 PWM，便重新引入了
  BSP 对设备语义的依赖；正确方式是设备拿到逻辑 PWM role。
- 多通道共享周期意味着一个 Device 修改周期可能影响其他 Device，设备层
  必须知道这一契约，不能把每个通道当成独立频率资源。
- F4 缺少 PWM 时，模块初始化必须失败为 `not_configured` 或按明确的可选
  功能路径跳过，不能伪造成功。

### 4.6 SPI

采用 H7 bus 契约，包含 blocking full-duplex 和配置好的 IT/DMA 操作。设备
片选是生成 GPIO，不是 SPI 专属 board enum。公共层必须不暴露 HAL SPI handle。

F4 当前没有 SPI，旧的手工 SPI1/BMI088 实现已被移除，SPI 后端不进入产品
构建图。若未来需要 F4 SPI，必须先在权威 IOC 中添加资源并重新生成，不能
恢复历史手工实现。

这对 `pnx_devices` 的直接影响很大：BMI088 等设备不得默认构造 SPI1，必须
使用逻辑总线和逻辑片选。否则 H7/F4 统一头文件虽然存在，设备代码仍然
只适用于某块板。

### 4.7 USART

采用 H7 的 `core::callback`、`bsp::dma::buffer_view`、线路配置、发送和
receive-to-idle 契约。F4 原有 delivery modes 和 telemetry 不迁入公共 API。
IOC 生成的 UART/RX-DMA/TX-DMA 私有绑定取代两分支的硬编码 handle map。

共享 Module/Device 需要确认：

- 回调收到的是一段有效数据的 view，还是需要模块复制；
- receive-to-idle 的 idle 事件、零长度数据和错误事件语义是否一致；
- TX 是否允许并发，发送完成回调是否在中断上下文；
- 线路配置中的波特率、数据位、停止位、校验定义是否与旧 F4 参数兼容；
- F4 旧 telemetry 接口的调用方是否已经移除或改写。

### 4.8 CAN

公共契约包括 `init(bus)`、`restart`、`transmit`、`rx_frame` 和
`core::callback`。H7 使用 FDCAN，F4 在内部使用 Classic bxCAN 并报告
Classic frame fields；不引入运行时 backend/capability framework。

board.json 不得声明 bxCAN/FDCAN、帧策略、FD/BRS、FIFO/filter 策略，这些
属于 IOC 或私有实现。上层如果依赖 FD-only 特性，就无法与 F4 Classic
统一；必须在共享契约中明确禁止，或把该特性保持为 H7 私有扩展。

H7 CAN diagnostics 是分支私有扩展，F4 indicator/fault diagnostics 也不是
公共契约。`pnx_modules` 和 `pnx_devices` 不应 include 或调用这些扩展，除非
未来有明确的共同需求并重新设计契约。

### 4.9 ADC

当 IOC 含 ADC channel 时使用 H7 契约；没有 ADC 配置的板不选择 ADC 实现。
F4 当前没有 ADC，因此上层不能假设 ADC backend 一定可链接。依赖 ADC 的
Device 应使用显式可选初始化/配置检查，不应在链接或运行时隐式等待硬件。

### 4.10 USB

V2 纳入 USB，公共层采用较简单的 callback/state/`try_send` 契约。F4 仅保留
USBX/HAL 实现所需的内部生命周期处理和 FS controller startup/disconnect
handle clearing；F4 专属 telemetry、capability、queue API 已移除。

但 F4 USB 产品选择仍被禁用，原因是描述符 VID/PID 为 `0x0000`，且
`PNX_USB_DEVICE_IDENTITY_CONFIRMED=0`。因此当前“USB scoped compile 通过”不
等于“F4 USB 产品功能可用”或“USB 可枚举”。共享 Module 必须能区别编译存在、
BSP 选择存在、设备身份已确认和硬件枚举通过这几种状态。

### 4.11 Flash

Flash 尚未统一。H7/F4 扇区和编程粒度不同，且两块板都没有已批准的保留数据
分区。因此不能为了头文件匹配而开放固件 Flash 写入 API。任何依赖持久化的
Device/Module 目前都不能把该接口当成 V2 公共能力。

## 5. 对 pnx_modules、pnx_devices、pnx_libs 的适配判断

### 5.1 `pnx_modules`

文件规定使用 `main`，并指出当前 main 已包含测量得到的 F407 stack 修正。
适配原则应是：

- 只 include 公共 HAL-free BSP 头文件；
- 通过生成逻辑 ID 获取 GPIO、PWM、SPI、USART、CAN、ADC、USB；
- 不引用 F4 telemetry、恢复状态机、capability API 或私有诊断；
- 对可选资源处理 `not_configured`；
- 不在模块内决定 pin、HAL handle、DMA 地址、FDCAN/bxCAN 类型；
- 不把验证/demo image selector 变成产品 subsystem composition；
- 对中断回调和 DMA view 遵守执行上下文与生命周期规则。

最可能出现的问题是历史模块仍假设 F4 的更丰富生命周期/遥测接口，或者
把 H7 的资源集合当成所有板的最小集合。仅有 H7/F4 BSP 编译通过不能证明
这些模块已经完成适配。

### 5.2 `pnx_devices`

设备层应拥有设备协议和设备组合，但不拥有物理硬件配置。它应使用逻辑
外设/通道/片选/中断角色，并通过 `robot.json` 组合设备，通过 `params.json`
提供应用参数。

典型危险包括：

- BMI088 设备直接假设 SPI1、TIM 通道或 heater PWM；
- 设备把 F4 的历史手工 SPI1 当成通用设备能力；
- 设备直接使用 HAL handle；
- 设备初始化时把缺少 ADC/PWM/SPI 的 F4 报告为成功；
- 设备要求 CAN FD 或 H7 diagnostics，却没有能力隔离；
- 设备持有异步 buffer view 的时间超过 BSP 保证的生命周期。

当前设计已经明确 BMI088 heater 只是 H7 的逻辑 PWM role 绑定，BSP 不包含
BMI088 依赖；这是正确方向，但还应在共享 Device 代码中检查是否存在旧的
设备语义到板级外设的硬编码。

### 5.3 `pnx_libs`

`pnx_libs` 提供共享 callbacks、status、DMA attributes 和 utilities，是统一
契约能否真正稳定的基础。重点检查：

- status 是否包含并一致解释 `not_configured`；
- callback 是否允许非拥有目标，且没有隐含所有权转移；
- callback 的执行上下文是否有统一文档；
- DMA attributes 是否能表达 H7 Cache 与 F4 no-op 的差异；
- buffer/view 的 const、长度、生命周期和对齐语义是否与 BSP 一致；
- utility 是否包含 MCU 家族条件编译而泄露到共享 API；
- 共享库是否错误地假设统一 Cache line 或所有内存都能 DMA。

如果 `pnx_libs` 的公共类型比 BSP 契约更老，最容易出现的结果是：BSP
可以独立编译，但 `pnx_modules` 或 `pnx_devices` 在组合构建时发生类型不匹配、
回调签名不匹配、状态码转换丢失或 DMA 内存检查绕过。

## 6. 分支、配置与构建链路审查

仓库和分支映射如下：

| 仓库 | 分支 | 作用 |
| --- | --- | --- |
| `pnx_template` | `refactor/V2` | 板选择、生成器、构建、文档、公共集成 |
| H7 `pnx_bsp` | `refactor/V2-h7` | H7 直接实现 |
| F4 `pnx_bsp` | `refactor/V2-f4` | 基于 `origin/F4_version_bsp` 的 F4 直接实现 |
| `pnx_devices` | `main`，除非证明需要改公共契约 | 共享设备层 |
| `pnx_modules` | `main`，除非证明需要改公共契约 | 共享模块层 |
| `pnx_libs` | `main`，除非证明需要改公共契约 | callbacks、status、DMA 属性和工具 |

没有独立的 `pnx_frame` 仓库，也不在 V2 范围内。分支命名不能中途改变。

目标流程是：

```text
PNX_BOARD=<profile>
  + 匹配的 pnx_bsp V2 分支
  + CubeMX IOC + board.json + params.json + robot.json
  -> build-local generated config/private bindings
  -> shared public BSP contract
  -> H7/F4 backend
  -> CMake build
```

`PNX_BOARD` 是普通用户唯一需要选择的板级入口。每个
`boards/<board>/` 应拥有 IOC、CubeMX Board tree、linker/toolchain、
`board.cmake` 和 `board.json`；对应的应用配置位于
`configs/boards/<board>/`。

`PNX_BSP_SOURCE_DIR` 只是高级开发覆盖项，用于验证第二个 BSP worktree。
正常构建使用 `pnx_bsp/`，配置阶段应检查 BSP branch family manifest 与板
是否匹配。这里的分支族校验是防止跨 MCU 私有绑定错配的关键保护；如果它只
检查目录存在而不检查 manifest，风险仍然存在。

## 7. 当前迁移状态与验证证据的边界

文件声称以下阶段已经完成：架构审计、V2 设计、分支和 living document、
目录与双板构建、生成绑定形状、GPIO/EXTI、DMA、PWM、SPI、USART、CAN、ADC，
USB 为 implementation done，Flash 延后。

具体状态包括：

- H7/F4 公共头文件和私有实现结构已匹配；
- F4 生成 pin role、HAL callback bridge、memory policy 和各模块私有绑定
  已做 scoped compile；
- F4 当前零 PWM/SPI/ADC 的 IOC 会把相应 backend 从产品 build graph 中
  排除；
- H7/F4 Debug 与 Release 镜像均曾完成编译链接；
- H7 Release 记录 FLASH 124628 B、DTCMRAM 80160 B；
- F4 Debug 记录 FLASH 32936 B、RAM 49616 B；
- F4 Release 记录 FLASH 19528 B、RAM 49616 B；
- F4 USB 只完成 scoped compile，产品构建仍由 `build.usbx=false` 禁用；
- H7 parent 当前没有 host suite；F4 分支有历史 37/37，但那不是 V2 验证；
- H7/F4 V2 硬件均未实际运行验证。

这些证据能证明相当一部分“代码结构和单板构建选择”成立，但不能证明：

- 两个 BSP 与当前三个共享仓库的所有组合都能编译；
- 所有异步回调行为一致；
- DMA Cache coherency 在真实负载下正确；
- CAN 过滤、发送、接收、重启在两种控制器上行为符合预期；
- USB 在 F4 上能合法枚举；
- 设备在资源缺失时不会错误宣称初始化成功；
- Flash 或持久化行为存在可用公共接口。

历史 F4 文档不能被转写成当前 V2 pass claim，这是文件中明确强调的审计
纪律，必须继续保持。

## 8. 需要重点补强的兼容性验证

建议将以下检查作为跨仓库集成门槛：

1. 用 H7 与 F4 两种 `PNX_BOARD` 分别组合当前 `pnx_modules`、
   `pnx_devices`、`pnx_libs` 的完整目标，而不只编译 BSP scoped target。
2. 做公共头文件 API 扫描，确认共享层没有 HAL include、STM32 家族符号、
   旧 F4 telemetry/capability/lifecycle API 和硬编码外设编号。
3. 对每个逻辑资源执行“已配置/未配置”测试，特别是 F4 的 SPI、PWM、ADC
   缺失路径，确认结果是 `not_configured` 而非成功。
4. 对 DMA buffer/view 测试 H7 Cache 维护、F4 CCM 拒绝、长度/方向/对齐和
   异步生命周期；确认调用方不能绕过 BSP 内存检查。
5. 对 GPIO active level、EXTI 回调、USART receive-to-idle、CAN callback
   和 USB callback 明确并测试中断/任务执行上下文。
6. 验证生成文件只出现在 build 目录，源树不会被生成器修改；验证生成绑定
   与 `PNX_BOARD` 和 BSP branch family 一一对应。
7. 在引入 F4 新 SPI/PWM/ADC 资源时，强制流程为修改权威 IOC、重新生成
   Board 输出、更新 `board.json` 逻辑绑定、再改变 CMake 选择；禁止恢复
   历史手工实现。
8. 给共享层增加最小 host contract test，覆盖 status、callback 签名、
   `buffer_view` 和逻辑 ID，而不是只依赖交叉编译。
9. 对 F4 USB 在分配真实 VID/PID、设置
   `PNX_USB_DEVICE_IDENTITY_CONFIRMED=1` 后，重新进行枚举和硬件验证。
10. 在 Flash 之前明确两块板的 reserved-data layout、擦除粒度、写入粒度、
    掉电语义和错误返回，未达成前不让共享模块依赖可写 Flash。

## 9. 剩余风险清单

### 已明确且正在控制的风险

- F407 当前 IOC 没有 PWM/SPI/ADC；不能恢复旧 TIM1/SPI1 手工实现。
- F407 USB VID/PID 为 `0x0000`，产品 USB 构建必须继续禁用。
- H7/F4 Flash 差异未解决，不能公开写入 API。
- H7/F4 硬件验证尚未进行。
- `pnx_modules`、`pnx_devices`、`pnx_libs` 的全量组合验证未在文件中声明。

### 由架构推导出的隐含风险

- 逻辑 ID 的命名、生成和跨仓库分发如果不稳定，会造成编译成功但运行时
  绑定错误。
- `not_configured` 如果只在 BSP 层定义而共享 status 没有同义值，错误会
  在 Module/Device 层被吞掉。
- 回调和 DMA view 的生命周期如果没有共享文档，H7/F4 可能出现不同的
  异步悬空引用问题。
- 定时器周期共享可能使多个设备互相影响，设备层若没有资源模型会出现
  难以发现的运行时冲突。
- FDCAN 与 bxCAN 的能力差异如果只通过注释说明，而没有在公共 frame/API
  中约束，调用方可能误用 H7-only 特性。
- `PNX_BSP_SOURCE_DIR` 如果绕过 branch family 检查，生成绑定和实现可能
  来自不同板族。
- “implementation done”容易被误读为“硬件可用”，尤其是 USB；状态表必须
  继续区分 compile、link、configure、hardware 和 enumeration。

## 10. 最终判断

从 `AGENTS.md` 记录的架构看，H7/F4 BSP 统一方案的核心边界是正确的：
公共契约统一，后端构建时选择，板级硬件由 IOC/生成代码拥有，逻辑绑定由
生成配置提供，HAL 和 MCU 差异留在私有层。该边界也基本能支持共享
`pnx_modules`、`pnx_devices`、`pnx_libs`，并且明确避免把 F4 历史复杂机制
倒灌到公共 API。

当前不能下的结论是“所有跨仓库适配已经完成”。要达到这个结论，必须补上
共享仓库完整组合构建、公共头文件依赖扫描、未配置资源行为测试、DMA/回调
生命周期测试以及硬件验证。特别是 F4 的资源子集和 USB 禁用状态必须被当作
正式能力边界，而不是暂时的构建例外。

后续任何改动都应遵循文件规定的顺序：先确认契约和 IOC 所有权，再生成
build-local 配置，选择匹配 BSP 分支，编译受影响目标，进行跨仓库自审，最后
只把持久事实写回 `AGENTS.md`。不要用历史 F4 行为、验证/demo selector 或
手工生成代码来扩大公共架构。

---

# API 级补充审查

以下内容是在阅读实际公共头文件、当前构建目录中的 H7/F4 生成配置，以及
共享 Device/Module 调用代码之后补充的。它比架构摘要更接近真实的编译和
调用契约。路径均相对于仓库根目录。

## 11. 公共状态类型：所有 API 的共同返回协议

文件：`pnx_libs/common/include/usertypes.hpp`

```cpp
enum class types::status : uint8_t {
    ok = 0,
    error,
    not_configured,
    invalid_arg,
    busy,
    not_initialized,
    not_connected,
    empty,
    too_large,
    invalid_context,
};
```

### 11.1 API 层面的统一解释

| 状态 | 适合表示的条件 | 跨模块要求 |
| --- | --- | --- |
| `ok` | 请求已完成，或异步请求已成功启动 | 不能用于“板上没有该资源” |
| `error` | HAL、外设或内部操作失败 | 不能把参数错误、未配置和忙状态都折叠为 error |
| `not_configured` | 逻辑 ID 在该板上不存在，或后端没有该资源 | Device/Module 必须能识别并处理 |
| `invalid_arg` | 空指针、越界 ID、非法长度、非法配置值 | 必须在 H7/F4 保持一致的输入校验边界 |
| `busy` | 同一硬件正在进行不允许重入的异步操作 | 调用者必须知道何时通过 state/restart 重试 |
| `not_initialized` | 尚未调用成功的 init | init 失败后不能误认为已初始化 |
| `not_connected` | USB 等外部连接当前不可用 | 不等同于外设未配置 |
| `empty` | 没有待处理数据 | 主要用于消息/队列/发送接口 |
| `too_large` | 超出传输或缓存上限 | 不能静默截断 |
| `invalid_context` | 在不允许的 ThreadX/ISR/线程上下文调用 | 公共头文件应明确每个 API 的上下文 |

### 11.2 当前发现的统一性要求

共享模块大量使用 `status != types::status::ok` 做失败判断；因此如果模块
需要区分“未配置”和“硬件故障”，必须显式检查 `not_configured`，不能只做
布尔化处理。否则 F4 缺少 SPI/PWM/ADC 时会被误报为设备损坏，或进入无限
重试。

## 12. 非拥有回调 API：`core::callback`

文件：`pnx_libs/common/include/callback.hpp`

核心类型是：

```cpp
template <typename Result, typename... Args>
class core::callback<Result(Args...)> {
public:
    using entry_type = Result (*)(void*, Args...);

    constexpr callback() noexcept;

    template <Result (*Function)(Args...)>
    static constexpr callback bind() noexcept;

    template <typename Object, Result (Object::*Method)(Args...)>
    static constexpr callback bind(Object* object) noexcept;

    template <typename Object, Result (Object::*Method)(Args...) const>
    static constexpr callback bind(const Object* object) noexcept;

    constexpr bool valid() const noexcept;
    constexpr explicit operator bool() const noexcept;
    Result operator()(Args... args) const;
};
```

### 12.1 生命周期

该类型只保存 `void* context_` 和函数指针，不拥有对象。注册到 EXTI、USART、
CAN 或 USB 后，被绑定的对象必须持续存活到注销或替换完成。不能绑定：

- 初始化函数中的局部对象；
- 线程栈上即将退出的对象；
- 会被移动或释放的容器元素；
- 只在一次调用中临时生成的对象。

### 12.2 上下文

当前公共头文件对回调上下文的声明是：

- EXTI：GPIO HAL interrupt path，不能阻塞；
- USART RX：UART HAL interrupt/DMA callback path，不能阻塞；
- CAN RX：CAN IRQ context，不能阻塞；
- USB RX：USB read ThreadX thread；
- USB TX result：USB write ThreadX thread；
- `remoter::update_callback`：remoter merge thread，同步调用，不能阻塞；
- `referee::update_callback`：referee receive thread，同步调用，不能阻塞。

这说明 USB 与 USART/CAN/EXTI 的 callback 语义并不相同，Device/Module 不
能用同一套“回调总是在任务线程”的假设。

### 12.3 当前 API 风险

`callback::operator()` 直接调用 `entry_`，没有在调用前检查 `valid()`。因此
公共 API 若允许空 callback，底层实现必须先判断；调用者也不能把默认构造的
callback 当成安全的 no-op。特别需要检查 `bsp::exti::attach`、
`bsp::can::register_rx_callback` 和 `bsp::usb::config` 的空回调处理。

## 13. DMA API：`buffer` 与 `buffer_view`

文件：`pnx_bsp/bsp/include/bsp_dma.hpp`。

```cpp
inline constexpr std::size_t bsp::dma::cache_line_size;

class buffer_view {
public:
    constexpr buffer_view() = default;
    constexpr uint8_t* data() const noexcept;
    constexpr std::size_t logical_size() const noexcept;
    constexpr std::size_t capacity() const noexcept;
};

bool valid(buffer_view buffer) noexcept;

template <std::size_t LogicalSize>
class buffer {
public:
    static constexpr std::size_t logical_size_value = LogicalSize;
    static constexpr std::size_t capacity_value =
        ((LogicalSize + cache_line_size - 1U) / cache_line_size) * cache_line_size;
    constexpr uint8_t* data() noexcept;
    constexpr const uint8_t* data() const noexcept;
    static constexpr std::size_t logical_size() noexcept;
    static constexpr std::size_t capacity() noexcept;
    constexpr buffer_view view() noexcept;
};
```

### 13.1 H7/F4 生成事实

当前 build-local 配置显示：

| 项目 | H723 | F407 |
| --- | --- | --- |
| `cache_line_size` | 32 | 1 |
| DMA 可访问范围 | `0x24000000..0x24050000`、`0x30000000..0x30008000` | `0x20000000..0x20020000` |
| dedicated DMA section | 开启 | 关闭 |
| TCM/CCM | H7 TCM 不可 DMA | F4 CCM 不可 DMA |

`buffer<LogicalSize>` 的 `capacity` 是按 Cache line 向上取整的存储容量，
但 `logical_size` 是真实传输长度。调用方不能把 capacity 当成有效数据长度。

### 13.2 传输所有权

当前 SPI、ADC 和 USART 的注释共同建立了如下规则：

- buffer/view 是借用，不转移所有权；
- DMA/异步操作完成前，底层对象必须保持存活；
- DMA buffer 必须来自静态 `bsp::dma::buffer<N>`，并带有 `BSP_DMA_BUFFER`；
- H7 负责 Cache clean/invalidate；F4 不做 Cache maintenance，但仍做地址合法性检查。

这与 `pnx_libs/common/include/memory.h` 存在一个需要持续注意的配置关系：
`BSP_DMA_BUFFER` 的对齐宏依赖 `PNX_DMA_CACHE_LINE_SIZE`，而 C++ buffer 的
容量/对齐依赖生成的 `board::memory::cache_line_size`。两者必须由 CMake 对
同一块板设置为同一值；否则可能出现 C++ 类型容量正确但变量对齐错误，或
链接段属性正确但 Cache 操作范围错误。

## 14. GPIO API

文件：`pnx_bsp/gpio/include/bsp_gpio.hpp`。

```cpp
constexpr bool is_enabled(bsp::gpio::input input_id);
constexpr bool is_enabled(bsp::gpio::output output_id);

types::status read(input input_id, bool& is_high);
types::status is_active(input input_id, bool& active);
types::status write(output output_id, bool is_high);
types::status set_active(output output_id, bool active);
types::status toggle(output output_id);
```

### 14.1 语义

- `read`/`write` 是物理高低电平语义；
- `is_active`/`set_active` 使用 `board.json` 生成的 active level；
- 逻辑 ID 越界或配置为 `port_id::none` 时不应访问 HAL；
- `is_enabled` 只是编译期/运行期快速能力查询，不等价于外设已经初始化；
- `set_active` 适合 Device 的 CS、使能和故障闭锁逻辑，不能在 Device 中再次
  手工反转 active level。

H7 当前生成的逻辑角色包括 BMI088 CS、PS2 CS/CMD/CLK 和两个输入；F407
 当前输入输出枚举为空。因而任何依赖 GPIO 的 F4 设备必须在配置层被排除，
 或明确接受 `not_configured`。

## 15. EXTI API

文件：`pnx_bsp/exti/include/bsp_exti.hpp`。

```cpp
using interrupt_callback = core::callback<void()>;
types::status attach(bsp::gpio::input input, interrupt_callback callback);
```

### 15.1 调用要求

- `input` 必须是已生成并配置的 GPIO input；
- callback 目标必须覆盖整个注册周期；
- callback 在 GPIO HAL 中断路径执行，不得阻塞；
- 不能在 callback 中等待 ThreadX semaphore、调用阻塞 SPI/USART 或进行
  浮点密集处理；
- 需要把事件转发到任务线程时，应使用 ISR-safe 的通知/队列机制。

当前头文件没有显式 `detach`。这意味着替换 callback、对象析构和系统停机
的生命周期必须由实现定义；这是一个需要补充文档或 API 设计确认的地方。

## 16. PWM API

文件：`pnx_bsp/pwm/include/bsp_pwm.hpp`。

```cpp
constexpr bool is_enabled(channel channel_id);
types::status init(channel channel_id);
types::status start(channel channel_id);
types::status stop(channel channel_id);
types::status set_duty(channel channel_id, float duty_ratio);
types::status set_period_us(channel channel_id, uint32_t period_us);
types::status set_pulse_width_us(channel channel_id, uint32_t pulse_width_us);
```

### 16.1 参数与资源语义

- `channel` 是生成逻辑通道，不是通用层的 TIM/HAL 类型；
- `duty_ratio` 的合法范围必须在 H7/F4 相同，通常应检查 `[0, 1]`；
- `period_us` 属于 timer，不只属于一个 channel；
- `pulse_width_us` 不应大于周期；
- `init/start/stop` 的先后关系需要由实现和调用方统一；
- H7 当前有 `tim12_ch2`、`tim3_ch4`，F407 channel 枚举为空；
- `app::pwm::bmi088_heater` 只在 H7 生成，不能成为 F4 公共依赖。

failsafe 是额外生成绑定，不应由 Device 直接知道 TIM6、DMA request 或
F4/H7 的 DMA channel/request 命名。Device 只应调用通用 PWM channel。

## 17. SPI API

文件：`pnx_bsp/spi/include/bsp_spi.hpp`。

```cpp
struct transfer_state {
    volatile bool busy;
    volatile bool complete;
    volatile bool receive;
    volatile bool dma;
    volatile types::status last_status;
};

types::status wait_ready(bus bus, uint32_t timeout_ms = 1000);
bool bus_enabled(bus bus) noexcept;
types::status init(bus bus);
types::status transmit(bus bus, const uint8_t* data, size_t len,
                       uint32_t timeout_ms);
types::status receive(bus bus, uint8_t* data, size_t len,
                      uint32_t timeout_ms);
types::status transmit_receive(bus bus, const uint8_t* tx, uint8_t* rx,
                               size_t len, uint32_t timeout_ms);
types::status transmit_it(bus bus, const uint8_t* data, size_t len);
types::status receive_it(bus bus, uint8_t* data, size_t len);
types::status transmit_dma(bus bus, bsp::dma::buffer_view buffer);
types::status receive_dma(bus bus, bsp::dma::buffer_view buffer);
const transfer_state& state(bus bus) noexcept;
```

### 17.1 调用时序

典型 blocking Device 调用顺序是：

```text
gpio::set_active(cs, false)
spi::init(bus)
spi::wait_ready(bus)
gpio::set_active(cs, true)
spi::transmit_receive(...)
gpio::set_active(cs, false)
```

当前 `pnx_modules/remoter/src/ps2.cpp` 已采用类似的 CS 包围传输模式。它的
兼容性前提是：SPI BSP 不操作 Device 的片选，片选始终由生成 GPIO 提供。

### 17.2 异步 API 的关键问题

IT API 借用调用者 buffer，直到 `state(bus).complete`。DMA API 要求静态
`bsp::dma::buffer<N>`。因此 `transmit_it`/`receive_it` 不能传递临时数组，
`transmit_dma`/`receive_dma` 不能把普通 heap、栈或未知地址包装成 view。

F407 当前 `bsp::spi::bus` 是空枚举且 `bus_count = 0`。这不是“运行时没有
SPI”，而是编译期没有任何合法 SPI bus 标识；F4 上引用 `bsp::spi::bus::spi2`
这样的代码会直接编译失败，引用任意强制转换的枚举则应返回
`not_configured`/`invalid_arg`。

## 18. USART API

文件：`pnx_bsp/usart/include/bsp_usart.hpp`。

### 18.1 类型

```cpp
enum class mode : uint8_t { block = 0, dma = 1, it = 2 };
enum class word_length : uint8_t { bits_8 = 0, bits_9 };
enum class stop_bits : uint8_t { one = 0, two };
enum class parity : uint8_t { none = 0, even, odd };

struct line_config {
    uint32_t baud_rate = 115200;
    word_length data_bits = word_length::bits_8;
    stop_bits stop = stop_bits::one;
    parity parity_mode = parity::none;
    bool enable_tx = true;
    bool enable_rx = true;
};

struct rx_frame {
    uint8_t* data = nullptr;
    size_t len = 0;
};

using rx_callback = core::callback<void(port, const rx_frame&)>;
```

### 18.2 函数

```cpp
bool port_enabled(port port) noexcept;
types::status init(port port, mode mode);
types::status configure(port port, const line_config& config);
types::status transmit(port port, const uint8_t* data, size_t len,
                       uint32_t timeout_ms);
types::status start_rx_to_idle(port port, bsp::dma::buffer_view buffer,
                               rx_callback callback,
                               TX_SEMAPHORE* notify_sem = nullptr);
types::status restart_rx(port port);
```

### 18.3 真实调用约束

`start_rx_to_idle` 的 buffer、callback target 和可选 semaphore 都是借用的。
接收回调在 HAL interrupt/DMA 路径，不能阻塞。回调收到的 `rx_frame` 只保证
在回调期间有效，不能把 `data` 指针保存到下一次回调之后，除非调用者自行
复制。

当前 H7 生成的 USART port 是 4 个：`uart5`、`uart7`、`usart1`、`usart10`；
F4 是 3 个：`usart1`、`usart3`、`usart6`。尽管两个配置都生成
`app::uart::dr16`、`vt03`、`ps2_uart`、`referee` 等别名，F4 的模块 feature
已经全部关闭，所以这些别名不能被当作真实功能证明。值得注意的是，F4
生成的 app UART 别名仍存在但并不意味着对应 Module 已被选择；能力判断应
优先看 feature 和 `port_enabled`。

### 18.4 与现有 Module 的关系

`remoter::dr16`、`vt03`、`ps2_uart` 和 `referee::service` 都依赖 USART DMA
接收和 ThreadX semaphore 通知。它们的接收 callback 必须只做快速复制/标记，
真正解码在任务线程中完成。任何把 callback 改为直接发布复杂消息、调用阻塞
设备或分配内存的做法都会违反 BSP API 上下文约束。

## 19. CAN API

文件：`pnx_bsp/can/include/bsp_can.hpp`。

### 19.1 帧类型

```cpp
struct rx_frame {
    uint32_t id = 0;
    uint8_t len = 0;
    id_type id_kind = id_type::standard;
    bus_type format = bus_type::classic;
    bool bit_rate_switch = false;
    uint8_t data[64]{};
};

using rx_callback = core::callback<void(bus, const rx_frame&)>;
```

### 19.2 函数

```cpp
bool bus_enabled(std::size_t index) noexcept;
bus_type configured_bus_type(std::size_t index) noexcept;
id_type filter_id_type_of(std::size_t index) noexcept;
types::status init(bus bus);
types::status restart(bus bus);
types::status transmit(bus bus, uint32_t id,
                       const uint8_t* data, uint16_t len);
types::status register_rx_callback(bus bus, rx_callback callback);
void unregister_rx_callbacks(bus bus);
TX_SEMAPHORE* err_sem(bus bus);
```

### 19.3 H7/F4 生成差异

| API 生成项 | H723 | F407 |
| --- | --- | --- |
| bus enum | `fdcan1`, `fdcan2`, `fdcan3` | `can1`, `can2` |
| bus count | 3 | 2 |
| bus types | FD、Classic、Classic | Classic、Classic |
| filter ID | 当前均 standard | 当前均 standard |
| callback slots | 8 | 8 |

公共 CAN API 虽然允许 `len` 到 64，但 F4 Classic bxCAN 的有效 payload 上限
是 8。F4 实现必须在统一 API 内把大于 8 的长度拒绝为 `invalid_arg` 或
`too_large`，而不是截断。上层 Device 若要 H7/F4 共用，只能依赖 Classic
8-byte 帧，或者明确只在 H7 feature 下使用 FD。

### 19.4 现有 Device 的风险

`pnx_devices/motors` 和 `dmimu` 注册 CAN RX callback。由于 callback 在 CAN
IRQ context，回调只能复制帧或入队，不能做浮点解码、ThreadX 阻塞或复杂协议
处理。DMIMU 当前头文件本身已经采用“ISR 只复制到内部队列，服务线程解码”
的设计，这是正确的跨 MCU 结构。

`bus_count` 是生成常量，`motorhandler` 中的 `rx_ready_` 以它为数组长度。
因此它只能按当前生成配置编译，不能把一个 H7 build 的 enum/value/header
和一个 F4 build 的实现混用。

## 20. ADC API

文件：`pnx_bsp/adc/include/bsp_adc.hpp`。

```cpp
struct conversion_state {
    volatile bool busy;
    volatile bool complete;
    volatile bool dma;
    volatile types::status last_status;
    volatile uint32_t last_value;
};

types::status init(channel channel_id);
types::status calibrate(channel channel_id);
types::status read_raw(channel channel_id, uint32_t& raw_value,
                       uint32_t timeout_ms);
types::status start_it(channel channel_id);
types::status start_dma(channel channel_id, bsp::dma::buffer_view buffer);
types::status stop_dma(channel channel_id);
const conversion_state& state(channel channel_id) noexcept;
```

DMA buffer 里的内容是 `uint32_t` samples，必须是静态 DMA buffer。当前 H7
有 `adc1_ch4`，F4 的 channel enum 为空且 count 为 0。F4 上任何 ADC Device
应在应用配置阶段关闭，而不是依赖一个不存在的枚举值。

## 21. USB API

文件：`pnx_bsp/usb/include/bsp_usb.hpp`。

```cpp
using rx_callback = core::callback<void(const uint8_t*, uint16_t)>;

enum class tx_result_code : uint8_t {
    success = 0,
    not_connected,
    usb_error,
    short_write,
};

struct tx_result {
    uint16_t requested_len = 0;
    uint16_t actual_len = 0;
    uint32_t usb_status = 0;
    tx_result_code code = tx_result_code::usb_error;
    constexpr bool success() const noexcept;
};

struct config {
    uint8_t read_priority;
    uint8_t write_priority;
    uint32_t period_ticks;
    rx_callback on_rx_callback;
    core::callback<void(const tx_result&)> on_tx_result_callback;
};

struct runtime_state { /* connection, busy, counts and last result */ };

types::status init(const config& cfg);
bool connected();
const runtime_state& state();
types::status try_send(const uint8_t* data, std::size_t len);
template <typename Packet> types::status try_send(const Packet& packet);
```

### 21.1 语义

- CDC 是字节流，不保证 RX callback 收到完整 packet；
- RX callback 在 USB read ThreadX thread；
- TX result callback 在 USB write ThreadX thread；
- `try_send` 只能在线程上下文调用；
- `try_send` 会在返回前复制数据，因此输入 buffer 只需保持到函数返回；
- packet overload 要求 trivially copyable，直接发送对象的内存布局；
- 结构体 padding、字节序和版本必须由上层协议负责，不能把 C++ 对象布局
  自动当成稳定线协议。

F4 当前 `HW_HAS_USB = 1`，但 `ENABLE_USBX = 0`，因此“有 USB 控制器”与
“公共 USB service 可用”是两个不同事实。当前 F4 不能因为 `hw_has_usb`
为真就选择 USBX Module。

## 22. Flash API 与文档状态矛盾

文件：`pnx_bsp/flash/include/bsp_flash.hpp` 暴露了：

```cpp
inline constexpr uint16_t flash_word_size = 32;
types::status erase_sector(std::uint32_t addr);
types::status write_flash_word(std::uint32_t addr, const void* data);
```

但 `AGENTS.md` 同时明确写着 Flash deferred，并要求“不要暴露 firmware Flash
为 writable”。这是当前仓库中最明确的文档/API 不一致之一。

需要做出一个明确选择：

- 如果 Flash 确实 deferred，则公共头文件和产品 build 不应把这两个写操作
  作为 V2 可用 API 暴露；至少应从公共构建图移除，或者标记为未启用内部
  实验接口；
- 如果要保留 API，则必须先补充 H7/F4 的 reserved data partition、合法
  地址校验、扇区擦除语义、写入粒度、对齐要求、掉电一致性、并发/中断限制
  和错误码，然后更新 `AGENTS.md` 的 Flash 状态。

此外，`flash_word_size = 32` 只表示一个固定值，不能单独证明 H7/F4 的实际
编程粒度一致；文件中已经承认两者 Flash 擦除扇区和编程粒度不同。因此当前
任何 Device/Module 都不应调用该 API。

## 23. 共享 Device/Module 的实际调用面

### 23.1 PS2 / SPI + GPIO + DWT

`pnx_modules/remoter/src/ps2.cpp` 使用：

- `bsp::spi::init(bus)`；
- `bsp::gpio::set_active(cs, false/true)`；
- `bsp::spi::transmit_receive(...)`；
- GPIO bit-bang 后端使用 `bsp::dwt::init()` 和 GPIO read/write；
- 所有步骤按 `types::status` 传播。

这说明 PS2 已经采用“transport 抽象 + 逻辑 GPIO/SPI”方向。H7 的 PS2 可用，
F4 当前没有 GPIO/SPI 逻辑资源，因此必须依赖 `PS2_BACKEND_*` 和 feature
关闭，不能在 F4 运行时尝试访问不存在的 enum。

### 23.2 USART + ThreadX + message

DR16、VT03、PS2 UART 和 Referee Module 都使用：

- `bsp::usart::configure`/`init`；
- `start_rx_to_idle`；
- 静态 DMA receive buffer；
- ISR callback 设置 frame-ready 或 semaphore；
- ThreadX 线程消费数据并发布 `msg::channel<T>`。

`msg::channel<T>` 要求 `T` trivially copyable，主题实例本身拥有 storage，
subscriber 只是指向 topic 的非拥有句柄。`msg::read` 只能在 subscriber 和
目标 buffer 生命周期有效时调用。这个消息层与 BSP callback 的正确组合是：

```text
HAL/DMA IRQ
  -> BSP rx_callback：只记录/复制/通知
  -> ThreadX module thread
  -> decode
  -> msg::publish
  -> service merge/update callback
```

不能在 BSP IRQ callback 中直接调用可能阻塞的 `msg::publish` 默认路径，除非
显式使用并且正确实现 `publish_opts{.from_isr = true}`。

### 23.3 CAN + Device handlers

电机 handler、DMIMU 使用 `bsp::can::init`、`register_rx_callback`、
`transmit`。公共设备代码只能依赖：

- bus enum 的逻辑值；
- classic-compatible frame；
- 8-byte payload 的共同子集；
- IRQ callback 只入队/复制；
- service thread 解码。

不能把 H7 的 `fdcan1` 名称、FD payload、BRS 或 H7 diagnostics 写进共享
Device。默认配置也必须检查：DMIMU 头文件的默认 `can_bus = fdcan3` 对 F4
并不存在，因此 DMIMU 若在 F4 被启用，会产生直接的跨板默认值问题。当前
F4 feature 关闭 DMIMU，所以构建暂时不触发；若将来打开，必须改为板级生成
别名或在 F4 `robot.json` 中提供有效逻辑 bus。

### 23.4 H7-only 设备绑定

H7 生成的 `board::device` 当前包含：

- `board::device::bmi088::{spi, acc_cs, gyro_cs, gyro_drdy, heater}`；
- `board::device::led::spi`；
- `board::device::ps2::{cmd, data, clk, cs}`。

F4 的 `board::device` 为空。故这些名字是板级生成应用绑定，不是跨板公共
符号。共享 Device 头文件如果直接默认引用它们，就会在 F4 编译失败；正确
做法是 feature/robot composition 让该 Device 根本不进入 F4 产品目标，或
为 F4 生成同名且有效的逻辑绑定。

## 24. H7/F4 生成 API 的实际兼容性矩阵

| 资源 | H7 生成状态 | F4 生成状态 | 共享代码要求 |
| --- | --- | --- | --- |
| CAN | 3 bus，含 FD | 2 bus，Classic | 只能默认依赖 Classic；不要硬编码 bus 名称 |
| SPI | 2 bus | 0 bus | Device 必须可选；F4 不编译 SPI 设备 |
| GPIO input/output | 2/5 | 0/0 | 逻辑 GPIO 必须由 robot/board 配置提供 |
| PWM | 2 channel | 0 channel | Heater/failsafe 只能在 H7 feature 中存在 |
| ADC | 1 channel | 0 channel | 不存在时返回/传播 `not_configured` |
| USART | 4 port，RX DMA 全有 | 3 port，RX DMA 全有 | port 名称是生成值，不可硬编码跨板 |
| USB controller | 有 | 有 | F4 USBX/产品能力仍关闭 |
| DMA Cache line | 32 | 1 | 共享库不能自行固定为 32 或 4 |
| DMA range | H7 SRAM 域 | F4 SRAM | buffer 必须经 BSP `valid` 检查 |

## 25. API 级必须修正/确认项

按优先级排列：

### P0：Flash API 与 deferred 决策冲突

`bsp_flash.hpp` 已暴露写 API，而架构文件要求暂缓。必须在公共 API 层关闭
或完成完整板级数据分区设计。

### P0：跨板默认枚举值

`dmimu::transport_config` 默认使用 `bsp::can::bus::fdcan3`。该值在 F4
不存在。只要 DMIMU 不是严格 H7-only，就必须改成生成的逻辑角色/配置注入，
不能把 H7 enum 作为共享 Device 默认值。

### P1：F4 空枚举的模块防护

F4 `bsp::spi::bus`、GPIO input/output、PWM channel、ADC channel 都为空。应
检查 CMake 是否真正排除所有会引用这些 enum 的 Device/Module，而不是只关闭
顶层 feature 宏。否则会出现“头文件能 include，但模板/默认配置仍编译失败”。

### P1：DMA 宏与生成 Cache line 一致性

验证 `PNX_DMA_CACHE_LINE_SIZE` 与 `board::memory::cache_line_size` 的来源
相同，并分别检查 H7=32、F4=1 的预处理结果和链接段。

### P1：回调注销/替换语义

EXTI 没有 detach；CAN 有 unregister；USART 是“同一 owner 替换”语义；USB
是 config 固定/线程回调语义。应为每种注册 API 明确对象销毁、替换、并发 IRQ
时的行为，否则共享 Device 难以安全重启。

### P1：CAN Classic 长度约束

公共 `rx_frame` 支持 64 字节，但 F4 Classic 只能 8 字节。必须保证 F4
发送/接收和共享 Device 的错误语义明确，禁止静默截断。

### P2：状态快照的并发一致性

SPI/ADC 的 `transfer_state`/`conversion_state` 字段是 volatile，但 volatile
不等同于原子同步。共享 Module 读取 `complete`、`busy`、`last_status` 时，
需要确认 ISR 与任务线程间是否有足够的内存屏障或 ThreadX 同步保证。

### P2：公共头文件依赖边界

USART、CAN、USB 公共头文件 include `tx_api.h`。这不是 MCU HAL 泄露，但它使
公共 BSP API 依赖 ThreadX。若 `pnx_modules` 未来需要 host test，应该通过
公共 platform shim 或测试替身隔离，而不是让 host 构建直接依赖 MCU/ThreadX。

## 26. API 级验收标准

在宣称“F4/H7 与共享模块完全统一”之前，至少应完成以下矩阵：

| 测试 | H7 | F4 |
| --- | --- | --- |
| 所有公共头文件独立语法编译 | 通过 | 通过 |
| `types::status` 每个错误分支映射 | 通过 | 通过 |
| 空/越界 logical ID | `invalid_arg`/`not_configured` | 同语义 |
| 缺少 SPI/PWM/ADC/GPIO | 不适用或有效 | `not_configured`/未编译 |
| DMA 静态 buffer、长度和地址范围 | Cache clean/invalidate | CCM 拒绝、Cache no-op |
| USART receive-to-idle 生命周期 | IRQ->ThreadX | IRQ->ThreadX |
| CAN 8-byte Classic 兼容 | Classic bus | 全部 bus |
| CAN FD/64-byte 拒绝或 feature 隔离 | 明确 | 明确拒绝 |
| callback 空值、替换、注销 | 明确 | 明确 |
| USB connected/not connected/short write | 编译+硬件 | 先身份确认，再枚举 |
| Flash 写 API | 应禁用或正式设计 | 应禁用或正式设计 |
| 完整 modules/devices/libs 组合链接 | 通过 | 通过 |

只有 scoped BSP compile 通过，而完整组合链接、错误语义和资源缺失测试没有
通过时，结论应写成“BSP API 结构通过”，不能写成“跨模块适配完成”。

---

# 板间细节、CMake 生成链路与 JSON 配置补充

## 27. 两块板的真实硬件输入对照

本节依据当前仓库中的两个 IOC、两个 board profile、两个 `board.json`、两个
`params.json`、两个 `robot.json` 和 build-local 生成结果整理。它描述的是
“输入文件如何变成公共 API 可见事实”，不是只罗列 MCU 名称。

### 27.1 H723 board profile

`boards/h723_mc02/board.cmake` 设置：

```cmake
PNX_BOARD_IOC          = boards/h723_mc02/h723_mc02.ioc
PNX_BOARD_CUBEMX_DIR   = boards/h723_mc02/cmake/stm32cubemx
PNX_BOARD_TOOLCHAIN    = boards/h723_mc02/toolchain.cmake
PNX_BOARD_LINKER_SCRIPT= boards/h723_mc02/pnx_STM32H723XG_FLASH.ld
PNX_BOARD_PARAMS       = configs/boards/h723_mc02/params.json
PNX_BOARD_ROBOT_CONFIG = configs/boards/h723_mc02/robot.json
PNX_BOARD_FAMILY       = stm32h7
```

IOC 中的关键硬件资源是：

- MCU：`STM32H723VGTx`，Cortex-M7；
- CAN：`FDCAN1`、`FDCAN2`、`FDCAN3`；
- SPI：`SPI2`、`SPI6`；
- UART/USART：`UART5`、`UART7`、`USART1`、`USART10`；
- ADC：`ADC1` regular channel 4；
- PWM：`TIM3 CH4`、`TIM12 CH2`；
- failsafe/update DMA：`TIM6_UP`；
- USB：`USB_OTG_HS`，配置为 device-only FS 虚拟模式；
- DMA：USART、SPI2 和 TIM6 update DMA；
- NVIC：FDCAN、SPI2、UART、USART、USB、EXTI 和 DMA 中断均由 IOC 管理；
- 系统时钟：CPU 520 MHz，HCLK 260 MHz，FDCAN 时钟 80 MHz，USB 48 MHz。

`board.json` 进一步补充 IOC 不适合承载的事实：

- D-cache line 为 32；
- DMA policy 为 `dedicated_section`；
- DMA 可访问区为 `0x24000000..0x24050000` 和
  `0x30000000..0x30008000`；
- `bmi088` 绑定 SPI2、两个 CS、gyro DRDY 和 heater PWM；
- `led` 绑定 SPI6；
- PWM failsafe timer 绑定到 TIM6，但 timer period、DMA request、channel 等
  仍由 IOC 所有；
- H7 的 `board::device` 最终生成 BMI088、LED、PS2 的逻辑别名。

### 27.2 F407 board profile

`boards/f407_c_board/board.cmake` 设置：

```cmake
PNX_BOARD_IOC          = boards/f407_c_board/f407_c_board.ioc
PNX_BOARD_CUBEMX_DIR   = boards/f407_c_board/cmake/stm32cubemx
PNX_BOARD_TOOLCHAIN    = boards/f407_c_board/toolchain.cmake
PNX_BOARD_LINKER_SCRIPT= boards/f407_c_board/STM32F407XX_FLASH.ld
PNX_BOARD_PARAMS       = configs/boards/f407_c_board/params.json
PNX_BOARD_ROBOT_CONFIG = configs/boards/f407_c_board/robot.json
PNX_BOARD_FAMILY       = stm32f4
```

IOC 中的关键硬件资源是：

- MCU：`STM32F407I(E-G)Hx`，Cortex-M4；
- CAN：`CAN1`、`CAN2`，均为 bxCAN Classic；
- USART：`USART1`、`USART3`、`USART6`；
- DMA：USART1 RX/TX、USART3 RX、USART6 RX/TX；
- USB：`USB_OTG_FS`，device-only；
- 没有 SPI、PWM、ADC、FDCAN；
- CAN、USART、USB 的 NVIC 由 IOC 管理；
- 系统时钟 168 MHz，APB1 timer clock 84 MHz，APB2 timer clock 168 MHz，
  USB 48 MHz。

`board.json` 的内容刻意保持很小：

- DMA policy 为 `default`；
- Cache line 为 1；
- DMA 可访问区为 `0x20000000..0x20020000`；
- GPIO input/output、PWM channel 均为空；
- BMI088 和 LED 都是 `enabled: false`；
- 没有任何 `board::device` 逻辑绑定。

F4 的“没有 SPI/PWM/ADC”不是运行时开关，而是生成阶段的空集合，最终反映
为：SPI bus enum 为空、GPIO input/output enum 为空、PWM channel enum 为空、
ADC channel enum 为空，并从 product build graph 排除对应 BSP backend。

## 28. board.json 与 IOC 的边界：实际字段级规则

### 28.1 `board.json` 的 memory 节

当前生成器读取：

```json
"memory": {
  "dma_policy": "dedicated_section" | "default",
  "cache_line_size": 32,
  "dma_accessible_ranges": [
    { "start": "0x...", "end": "0x..." }
  ]
}
```

生成阶段会验证：

- `dma_policy` 只能是 `dedicated_section` 或 `default`；
- cache line 必须大于 0；
- cache line 必须为 2 的幂；
- 每个地址范围的 `end` 必须严格大于 `start`；
- 地址范围被生成到 `board::memory::dma_accessible_ranges`；
- `cache_line_size` 被生成到 `board::memory::cache_line_size`；
- policy 被转换为 `PNX_DMA_DEDICATED_SECTION`，再传给编译器。

这里存在一个重要层次区别：`board.json` 的范围只是“BSP 允许 DMA 的静态
内存事实”，不代表链接器一定把所有普通数组放进这些区域。H7 还需要 linker
script、`BSP_DMA_BUFFER` 和 dedicated section 三者共同成立；F4 默认策略
则依赖普通 SRAM 地址范围以及 CCM 不在范围内。

### 28.2 `board.json` 的 bindings 节

#### GPIO 输入

```json
"gpio_inputs": {
  "logical_role": {
    "pin": "pe12",
    "active_level": "high"
  }
}
```

生成器会：

1. 把 role 转成 C++ identifier；
2. 校验 pin 形如 `p[a-k][0-15]`；
3. 拆出 port 和 pin number；
4. 校验 active level 只能是 `low` 或 `high`；
5. 生成 enum、配置数组和 `app`/`board::device` 别名。

#### GPIO 输出

与 GPIO input 相同，但生成到 `bsp::gpio::output` 和 output config。active
level 由 BSP 的 `set_active` 解释，Device 不应再次反转。

#### PWM channel

```json
"pwm_channels": {
  "bmi088_heater": {
    "timer": "tim3",
    "channel": 4,
    "failsafe_timer": "tim6"
  }
}
```

生成器需要把 `timer/channel` 与 IOC 中实际发现的 PWM channel 对上；
`failsafe_timer` 还必须有 IOC 的 update DMA。`board.json` 不能写：

- HAL timer handle；
- DMA stream/channel/request；
- TIM period；
- F4 channel 字段名或 H7 request 字段名；
- “BMI088 温控策略”。

它只负责“逻辑角色绑定到 IOC 已配置资源”。

### 28.3 `board.json` 的 devices 节

`devices` 是生成逻辑绑定的来源之一，不是设备实现开关的唯一来源。

H7 的：

```json
"bmi088": {
  "enabled": true,
  "spi": "spi2",
  "acc_cs": "bmi088_acc_cs",
  "gyro_cs": "bmi088_gyro_cs",
  "gyro_drdy": "bmi088_gyro_drdy",
  "heater_pwm": "bmi088_heater"
}
```

最终生成 `board::device::bmi088` 的逻辑句柄。F4 的：

```json
"bmi088": { "enabled": false },
"led": { "enabled": false }
```

最终不生成这些设备绑定。Device 源码必须由 feature/board composition 排除，
不能只依靠运行时 `enabled` 字段，否则 F4 编译会尝试引用不存在的 enum。

## 29. params.json 的职责和危险边界

### 29.1 H7 params

H7 `params.json` 主要包含：

- `build.usbx = true`：允许生成 USBX 产品构建；
- `bindings.remoter_uart = uart5`、`referee_uart = usart1`：应用角色到
  已生成 UART port 的别名选择；
- `can.fdcan1.id_type = standard`：CAN filter ID 类型；
- CAN diagnostics 周期和窗口；
- AHRS、DMIMU、remoter、PS2、referee、test 的线程与协议参数；
- PS2 的 pin 参数目前仍存在于 params，但硬件 pin 的真正 IOC/board 边界
  需要特别审查，不能让 params 重复拥有 IOC 配置。

### 29.2 F4 params

F4 `params.json` 设置：

- `build.usbx = false`；
- `bindings.remoter_uart = none`、`referee_uart = none`；
- CAN1/CAN2 均 standard；
- remoter source 为 `none`；
- test report UART 为 USART1，启动自动运行。

需要注意：生成器仍会生成 `app::uart::dr16`、`vt03`、`ps2_uart`、`referee`
等别名，即使 F4 feature 被关闭。别名的存在不能当作功能存在；CMake feature
过滤和 `port_enabled` 才是实际能力判断。

### 29.3 参数不能取代能力检测

params 只应提供“用户选择的应用参数和角色选择”，不能：

- 声明一个 IOC 中没有的 SPI/PWM/ADC；
- 为 F4 创造 `fdcan3`；
- 指定 HAL handle；
- 复制 DMA stream/request；
- 通过 `enabled: true` 绕过空生成 enum；
- 代替 `board.json` 的静态内存事实。

生成器应对参数引用的逻辑资源做存在性校验，尤其是 F4 的 `none` 绑定和
H7/F4 不同的 CAN/UART 名称。

## 30. robot.json 的设备组合与构建传播

### 30.1 H7 robot tree

H7 当前启用了：

- DMIMU：`fdcan3`、Classic、CAN ID `0x04`、master ID `0x04`；
- 三个电机：
  - DJI GM6020，`fdcan2`、Classic、`0x205`；
  - DM DM4310，`fdcan1`、FD、`0x01`、MIT mode；
  - DJI GM6020，`fdcan1`、FD、`0x206`。

生成器据此生成：

- `robot::motors::motor_count = 3`；
- `has_dji = true`；
- `has_dm = true`；
- `has_lk = false`；
- `has_xv2 = false`；
- 每个 motor 的 `motors::config`，包含 bus、bus type、CAN ID、control mode；
- DMIMU 的 robot config；
- `MOTOR_DJI=1`、`MOTOR_DM=1`，从而决定对应源文件是否进入构建。

### 30.2 F4 robot tree

F4 只有：

```json
"devices": {
  "motors": { "list": [] }
}
```

因此 `robot_motor_count = 0`，CMake 会排除所有 motor Device 和 motor diagnose
源文件。F4 的 `HAS_MOTORS` 宏仍可能由其他生成逻辑设为 1，但它不能替代
`robot_motor_count`；这类“feature 宏存在”和“实际 robot device 数量为零”
的双层状态必须保持一致，否则会产生空 handler、错误注册或无意义的线程。

### 30.3 robot.json 的板间兼容规则

robot.json 中的 `can_bus` 必须引用该板生成的 bus enum：

- H7 可以使用 `fdcan1/2/3`；
- F4 只能使用 `can1/2`；
- H7 的 `can_type=fd` 只能配置到支持 FD 的 H7 bus；
- F4 所有 bus 只能 Classic；
- 共享 Device 不应把 robot.json 中的字符串直接硬编码成 H7 枚举默认值。

生成器目前会根据 model 前缀推导 `MOTOR_DJI/DM/LK/XV2`。这形成了一个明确
的单一事实来源：不要再添加第二份“电机协议可用列表”，否则 robot list 与
CMake 源文件选择会漂移。

## 31. `import_ioc.cmake` 的 IOC 解析边界

文件：`configs/cmake/import_ioc.cmake`。

它不是完整 CubeMX parser，而是按 key/value 行做受限解析，当前识别：

- FDCAN/CAN 外设；
- USART/UART/LPUART；
- SPI；
- TIM；
- USB OTG FS/HS；
- PWM channel；
- ADC regular conversion；
- DMA request；
- NVIC IRQ enable；
- timer clock；
- FDCAN frame format/filter 数量；
- UART/SPI 的 DMA 和 IRQ 能力。

### 31.1 排序与逻辑 ID 稳定性

CAN、UART 等硬件列表会做 natural sort，例如 `FDCAN2` 排在 `FDCAN10` 前面。
随后生成 enum 的顺序依赖解析列表。因此新增或删除 IOC 资源会改变 enum 的
底层数值，不能把 `static_cast<uint8_t>(bus)` 的值持久化、写入协议或当作
跨版本 ABI。

共享模块只能使用命名 enum/生成 alias，不能保存 enum 的数值到 Flash、CAN
配置文件或网络协议。

### 31.2 解析限制带来的风险

如果 CubeMX 改变输出 key 名称、转义形式、DMA request 命名或 ADC 参数命名，
解析器可能“没有发现资源”而不是报错，最终生成空 backend。每次 IOC 生成后
应检查：

- 解析出的硬件集合与 CubeMX Board tree 一致；
- 生成 bus/channel/port count 与预期一致；
- F4 的 CAN1/CAN2、USART1/3/6 被识别；
- H7 的 FDCAN1/2/3、SPI2/6、TIM3/12、ADC1 被识别；
- 需要 DMA 的资源确实发现了 DMA request。

### 31.3 时钟来源

PWM timer clock 由 `pnx_ioc_timer_clock_hz` 从 RCC key 推导：

- TIM1/8/9/10/11/15/16/17 使用 APB2 相关 key；
- TIM2/3/4/5/6/7/12/13/14/23/24 使用 APB1 相关 key。

这对 H7/F4 的 timer period 计算非常关键：不能把 H7 的 260 MHz 或 F4 的
84/168 MHz 以固定常量写进 PWM BSP。`board.json` 只能提供逻辑绑定，不能覆盖
IOC 的 timer clock。

## 32. `generate_config.cmake` 的完整产物链

顶层 `CMakeLists.txt` 在 `project()` 后执行：

```cmake
set(IOC "${PNX_BOARD_IOC}")
set(BOARD_CONFIG "${PNX_BOARD_DIR}/board.json")
set(PARAMS "${PNX_BOARD_PARAMS}")
set(ROBOT_CONFIG "${PNX_BOARD_ROBOT_CONFIG}")
set(OUT_DIR "${CMAKE_BINARY_DIR}/generated")
include(configs/cmake/generate_config.cmake)
```

生成器输出至少包括：

- `${build}/generated/config.hpp`：feature、逻辑 enum、board binding、memory
  facts、params 常量；
- `${build}/generated/robot_config.hpp`：机器人设备树、motor config、DMIMU
  config；
- `${build}/generated/bsp_bindings.cpp`：私有 HAL handle、DMA、CAN、SPI、
  USART、PWM、ADC 等 generated binding。

这些文件是构建输出，不能手工修改。任何修复都必须回到 IOC、board.json、
params.json、robot.json 或生成器本身。

### 32.1 生成的 config.hpp 组成

当前生成 `config.hpp` 包含几类命名空间：

```text
config::feature       编译 feature 宏和 constexpr bool
bsp::can              bus enum、bus config、count、filter/type
bsp::spi              bus enum、bus config、count
bsp::gpio             port/active/input/output、config arrays
bsp::pwm              channel、timer clock、count
bsp::adc              channel、count
bsp::usart            port、handle id、port config、DMA flags
app::uart             应用 UART 角色别名
app::gpio             应用 GPIO 角色别名
app::pwm              应用 PWM 角色别名
app::adc              应用 ADC 角色别名
board::memory         Cache line、DMA range、dedicated section
board::device         设备逻辑组合绑定
params::*             应用和模块参数常量
```

### 32.2 feature 与硬件资源是两套维度

例如 H7 当前可能同时有：

- `HAS_USB = 1`；
- `ENABLE_USBX = 1`；
- `HAS_BMI088_HEATER = 1`；
- `pwm_channel_count = 2`。

F4 当前则是：

- `HW_HAS_USB = 1`；
- `ENABLE_USBX = 0`；
- `HAS_BMI088_HEATER = 0`；
- `pwm_channel_count = 0`。

因此：

- `HW_HAS_USB` 表示 IOC 有 USB 控制器；
- `ENABLE_USBX` 表示 params/产品是否选择 USBX；
- `HAS_BMI088_HEATER` 表示应用/设备配置是否选择 heater；
- `pwm_channel_count` 表示 IOC/board 是否真的有 PWM channel。

任何 CMake 过滤逻辑不能只看其中一个变量。

## 33. 顶层 CMake 的 source filtering 细节

顶层 CMake 用 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 收集：

- BSP 的 `.c/.cpp`；
- `pnx_devices`、`pnx_modules`、`pnx_libs` 的 `.cpp`；
- `diagnose` 的 `.cpp`；
- generated `bsp_bindings.cpp`。

然后根据生成变量过滤源文件。

### 33.1 BSP backend 过滤

当：

- `ENABLE_USBX` 为 false：排除整个 BSP `usb` 目录；
- `pwm_channel_count == 0`：排除 `pwm` 和 `pwm_failsafe`；
- `spi_bus_count == 0`：排除 `spi`；
- `adc_channel_count == 0`：排除 `adc`。

这保证 F4 的空 SPI/PWM/ADC 不进入产品链接。但这也意味着共享源文件若无
feature guard 仍可能直接 include 相应头并引用不存在 enum；只排除 BSP 实现
不能自动修复 Device/Module 的头文件级依赖。

### 33.2 Device/Module 过滤

当前明确过滤：

- `HAS_DMIMU` 为 false：排除 DMIMU Device；
- motor count 为 0：排除全部 motor Device 和 motor diagnose；
- `MOTOR_DJI/DM/LK/XV2`：分别过滤对应协议实现；
- `ENABLE_DR16/VT03/PS2/PS2_UART`：分别过滤 remoter 源文件；
- `BOARD_HAS_BMI088` 为 false：排除 BMI088 Device；
- `HAS_AHRS` 和 `HAS_DMIMU` 都 false：排除 AHRS 和 IMU diagnose；
- PS2 关闭：同时排除 PS2 diagnose；
- `ENABLE_USBX` 为 false：USB BSP backend 不进入 build graph。

### 33.3 过滤规则的结构性风险

`file(GLOB_RECURSE)` 加字符串路径正则比较容易产生以下问题：

- 新增源文件的目录命名不符合正则，未被排除；
- Windows `\\` 与 Unix `/` 路径兼容处理漏掉某个分支；
- header 被排除但依赖源仍被收集；
- feature 宏与源过滤逻辑存在两份判断，发生漂移；
- 某个 Module 共享一个源文件，但该文件同时包含可选和必选实现，无法按
  子功能精确过滤。

建议对每个 board 保存一份 configure 阶段的“最终源文件清单”，至少验证
F4 的 build graph 不含 SPI/PWM/ADC/BMI088/LED/PS2，而 H7 的 build graph
包含其配置的资源。

## 34. CMake board 选择与 BSP branch family 校验

顶层正常入口只有：

```cmake
-DPNX_BOARD=h723_mc02
-DPNX_BOARD=f407_c_board
```

CMake 首先确认：

- `boards/<PNX_BOARD>/board.cmake` 存在；
- `boards/<PNX_BOARD>/board.json` 存在；
- board.cmake 定义 IOC、CubeMX dir、toolchain、linker、params、robot、family；
- 每个路径真实存在；
- `PNX_BSP_SOURCE_DIR/bsp/target.cmake` 存在；
- target manifest 的 `PNX_BSP_FAMILY` 与 board 的 family 相同。

当前 `pnx_bsp/bsp/target.cmake` 是：

```cmake
set(PNX_BSP_FAMILY "stm32h7")
```

因此当前工作树本身是 H7 BSP。选择 F4 board 时，必须把
`PNX_BSP_SOURCE_DIR` 指向 F4 V2 BSP worktree；否则 configure 应失败，而不应
让 H7 生成绑定和 F4 board 继续组合。

### 34.1 这项校验能防什么

- H7 public/private source 与 F4 IOC 混用；
- F4 bxCAN 实现错配到 H7 FDCAN generated binding；
- F4 linker/toolchain 与 H7 BSP family 混用；
- build directory 中残留上一块板的 generated config；
- `PNX_BSP_SOURCE_DIR` 指向错误分支但目录结构看似完整。

### 34.2 这项校验还不能防什么

- BSP family 正确但具体 branch commit 过旧；
- generated binding schema 与 backend 私有实现版本不匹配；
- `board.json` role 名称在 Device 中写错；
- IOC 有资源但 Board generated code 没有对应 handle；
- CMake 使用了旧 build directory 中的缓存变量；
- shared module 使用了未被 branch manifest 声明的旧 API。

因此 family 校验应进一步配合 schema/version 或 generated manifest 校验。

## 35. CMake preset 与 build directory 隔离

`CMakePresets.json` 定义四个产品 preset：

| preset | `PNX_BOARD` | build type |
| --- | --- | --- |
| `h723-debug` | `h723_mc02` | Debug |
| `h723-release` | `h723_mc02` | Release |
| `f407-debug` | `f407_c_board` | Debug |
| `f407-release` | `f407_c_board` | Release |

默认 generator 是 Ninja，默认 toolchain 是
`cmake/gcc-arm-none-eabi.cmake`，输出目录为 `build/${presetName}`。

正确的板间切换方式是使用独立 preset/build directory，而不是在一个已经
配置过 H7 的目录内只改 `PNX_BOARD`。否则以下缓存可能残留：

- `PNX_BOARD_CUBEMX_DIR`；
- linker script；
- `PNX_BSP_SOURCE_DIR`；
- compiler flags；
- generated config；
- STM32CubeMX target name；
- `CMAKE_CXX_COMPILER` 和 family-specific definitions。

`CMAKE_CONFIGURE_DEPENDS` 已包含 IOC、board.json、board.cmake、params、robot、
两个生成脚本和 BSP target manifest，这能触发重新 configure，但不能替代
不同 board 使用不同 binary directory。

## 36. 生成配置与 CubeMX generated Board 的所有权链

完整所有权链是：

```text
CubeMX .ioc
  -> CubeMX generated Board/C source and handles
  -> import_ioc.cmake 解析“有哪些资源/能力”

board.json
  -> 逻辑 role、active level、memory facts、device binding

params.json
  -> build switch、应用参数、角色别名、协议参数

robot.json
  -> 设备组合、motor model、bus、CAN ID、DMIMU choice

generate_config.cmake
  -> config.hpp + robot_config.hpp + bsp_bindings.cpp

CMake source filtering
  -> 最终 BSP/Device/Module source graph

CubeMX CMake + linker/toolchain
  -> final ELF/HEX/BIN
```

其中只有 CubeMX `.ioc` 和 CubeMX generated Board 代码拥有：

- 真实外设实例；
- pin alternate function；
- clock tree；
- DMA stream/channel/request；
- NVIC priority/IRQ；
- CAN/FDCAN mode；
- USB/ADC/timer configuration。

`generate_config.cmake` 可以“发现并绑定”这些事实，但不应重新定义它们。

## 37. 板间切换的建议验证步骤

### 37.1 H7 配置

```text
选择 h723-debug preset
确认 PNX_BOARD=h723_mc02
确认 target.cmake family=stm32h7
确认 generated config 包含 FDCAN/SPI/GPIO/PWM/ADC/4 UART
确认 generated bsp_bindings.cpp 使用 H7 CubeMX handles
确认源图包含 PWM/SPI/ADC/BMI088/LED/PS2
确认 linker 使用 pnx_STM32H723XG_FLASH.ld
```

### 37.2 F4 配置

```text
选择 f407-debug preset
确认 PNX_BOARD=f407_c_board
确认 PNX_BSP_SOURCE_DIR 指向 F4 V2 BSP
确认 target.cmake family=stm32f4
确认 generated config 只有 CAN1/2、USART1/3/6 和空 SPI/PWM/ADC/GPIO
确认 ENABLE_USBX=0
确认源图排除 SPI/PWM/ADC/BMI088/LED/PS2/DMIMU/motors
确认 linker 使用 STM32F407XX_FLASH.ld
确认 DMA range 为 F4 SRAM 且 Cache line=1
```

### 37.3 不能只看编译成功

每次板间切换还应检查：

- `compile_commands.json` 中 MCU define 和 include path 是否匹配；
- generated `config.hpp` 的 enum/count 是否匹配选中的 IOC；
- generated `bsp_bindings.cpp` 是否包含错误 family 的 HAL 类型；
- link map 中是否含被禁用模块；
- linker section 中 DMA buffer 是否落在可访问范围；
- H7 是否存在 dedicated `.dma_buffer`；
- F4 是否没有使用 CCM；
- USBX 源是否在 F4 product graph 被排除；
- build log 是否显示了正确的 board/family/BSP 路径。

## 38. JSON/CMake 级新增风险清单

### P0：board.json 与 IOC 资源不一致时的拒绝策略

例如 board.json 声明 `bmi088_heater.timer = tim3/channel=4`，但 IOC 删除
TIM3 CH4。生成器必须失败，而不是生成一个逻辑 enum 后让运行时返回错误。
同样，`failsafe_timer` 没有 update DMA 时也应在 configure 阶段失败。

### P0：robot.json 引用不存在的 bus

F4 若 robot.json 添加 `fdcan3`，当前生成器必须在 configure 阶段给出明确
错误。不能等到 C++ 生成、链接或运行时才失败。

### P1：params.json 的 `none` 传播

`remoter_uart = none`、`referee_uart = none` 需要明确对应“模块关闭”还是
“模块存在但不可用”。不能让 `none` 被静默转换为 enum 0，导致误用 USART1。

### P1：feature 宏和资源 count 的双重真相

`HAS_USB`/`ENABLE_USBX`、`HAS_MOTORS`/`robot_motor_count`、
`HAS_BMI088_HEATER`/`pwm_channel_count` 都是不同维度。生成器应让它们的
关系可验证，必要时在 configure 阶段拒绝矛盾组合。

### P1：生成文件 schema 版本

当前 `config.hpp` 和 `bsp_bindings.cpp` 没有明显的 schema/version 常量。
若 BSP 分支变更私有 binding 字段，旧 generated 文件可能仍能被 include，
但运行时/链接时出错。建议生成 manifest，包含 board、family、IOC hash、
generator schema version、BSP expected schema version。

### P2：JSON 数值与字符串类型

当前地址、CAN ID 等字段有字符串形式，如 `"0x04"`、`"0x24000000"`。生成器
需要在所有字段上统一校验字符串/数字类型、十进制/十六进制范围和溢出；否则
不同 CMake `string(JSON)` 版本或输入风格可能产生不一致结果。

### P2：IOC parser 的“静默缺失”

解析器找不到某种 key 时有些路径会返回空集合。对关键资源建议区分：

- 明确 IOC 没有资源；
- IOC 有资源但 parser 未识别；
- board.json 要求资源但 IOC 没配置。

这三者必须有不同诊断，不能全部表现为空 enum。

## 39. 板间适配的最终判定标准

只有在以下条件同时成立时，才能认为“板间统一不仅是头文件统一，而且是
完整构建与生成链路统一”：

1. H7/F4 每块板都使用独立 build directory 和匹配的 BSP family。
2. IOC、board.json、params.json、robot.json 的职责没有交叉覆盖。
3. 所有 JSON 引用的逻辑资源都能在 configure 阶段验证存在。
4. F4 空资源在生成 enum、CMake source graph 和共享模块组合中都被正确处理。
5. generated config/binding 只在 build 目录生成，并与 IOC、board、BSP schema
   一致。
6. CMake 最终源图、include path、compile definitions、linker script 和
   generated bindings 均与目标板一致。
7. 共享 Device/Module 只使用逻辑绑定和公共 API，不依赖 H7/F4 私有名字。
8. H7 DMA Cache 与 F4 DMA memory rejection 在完整模块组合中都经过测试。
9. robot.json 的 CAN bus/type/ID 在两块板的真实能力范围内。
10. Flash、USB identity、硬件枚举等未完成项没有被 build pass 掩盖。

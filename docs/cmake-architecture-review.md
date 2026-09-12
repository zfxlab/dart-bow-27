# 启动架构与 CMake 维护性审查

## 本次整理结果

保持原有文件布局，没有新增 CMake 文件、模板目录或框架层。

- generate_config.cmake：辅助函数集中在文件开头；统一浮点、整数、布尔参数输出，合并 23 处重复默认值块；用一个本地输出函数替代写文件、读回、替换占位符、再次写文件的流程。内容未变化时保留输出时间戳。当前约 2117 行。
- 根 CMakeLists.txt：电机与遥控器的重复路径筛选改成直接循环；补齐 app 头文件路径收集；OpenOCD 配置与源文件清单改为内容变化时更新。当前约 303 行。
- import_ioc.cmake：增加重复包含保护，明确只输出硬件事实，不承担应用配置语义。
- export_editor_context.cmake：PWM 与其他资源共用数组导出循环；本地辅助函数使用明确前缀；相同快照不重写，失败时仍保留旧文件。
- usbx_integration.cmake：明确输入与目标修改范围；禁用 USBX 的启动钩子也按内容变化更新。
- cmake/gcc-arm-none-eabi.cmake：继续保持薄板型选择入口，不搬入板级编译参数。

前一轮结构整理验证：H7/F4 的四个生成 C++ 文件逐字节一致，两板 Debug 构建及 13 项回归通过。本轮默认数据与构建矩阵的后续验证见下文；源码选择和 CubeMX 适配仍可继续整理。

## 结论

当前两板构建机制能够继续使用，无需重写为另一套构建框架。父 CMake 的职责基本合理；主要维护压力集中在约 2191 行的 generate_config.cmake，以及生成器、GUI Schema 和板型参数之间重复维护的默认值。

建议先统一配置语义与默认值，再按职责小范围拆分生成器。不要为了减少文件行数引入注册中心、运行时 MCU 选择或大量细碎 CMake 文件。

## 本次完成的启动边界

```text
CubeMX main.c
  ├─ HAL、时钟、外设初始化
  └─ Core/Src/app_threadx.c: MX_ThreadX_Init
       └─ tx_kernel_enter
            └─ tx_application_define（CubeMX Azure RTOS 文件）
                 └─ Core/Src/app_threadx.c: App_ThreadX_Init
                      └─ app/app.cpp: app_start
                           └─ 按 test.auto_run_on_boot 调用 diagnose_start
                                └─ diagnose/app.cpp: 选择具体测试
```

- H7/F4 均恢复编译自己的 Core/Src/app_threadx.c。
- Core 只通过 USER CODE 区声明和调用 app_start；H7 原本已有调用，F4 补齐相同接口。
- app/app.cpp 不再包含 ThreadX 头文件或实现 ThreadX 入口，仅负责应用启动。
- diagnose/app.cpp 负责诊断组成，不拥有应用或内核入口。
- main、外设初始化、tx_application_define 及其现有栈检查逻辑保持不变。
- App_ThreadX_Init 在内核初始化阶段执行，此处适合创建线程、初始化应用资源；不应直接运行永久业务循环。
- app_start 当前仍只有诊断入口。以后增加业务启动函数，明确选择业务或诊断，避免重复初始化同一模块。

## CMake 实际做什么

这里有两个不同的“生成器”：Ninja 是 CMake 的构建系统生成器；generate_config.cmake 是本工程把 JSON/IOC 转为 C++ 的配置脚本。它们不是同一个东西。

| 环节 | 输入 | 职责与输出 |
| --- | --- | --- |
| CMakePresets.json | 用户选中的 preset | 设置 PNX_BOARD、Debug/Release、构建目录和工具链入口 |
| boards/<board>/board.cmake | PNX_BOARD | 指定 IOC、CubeMX CMake、链接脚本、参数路径、MCU 家族、OpenOCD target |
| cmake/gcc-arm-none-eabi.cmake 与板级 toolchain.cmake | PNX_BOARD | 选择编译器、CPU/FPU/ABI、链接脚本等编译链接参数 |
| 根 CMakeLists.txt | 板型与 BSP manifest | 验证板型和 BSP 家族匹配；执行配置生成；组装可执行目标 |
| import_ioc.cmake | CubeMX IOC | 提取已经配置的 UART/CAN/SPI/PWM/ADC/GPIO、DMA、IRQ 等事实；不配置芯片、不替代 CubeMX |
| generate_config.cmake | IOC、board.json、params.json、robot.json | 检查绑定、计算 feature、生成枚举/常量/私有 HAL 绑定和电机配置 |
| CubeMX cmake/stm32cubemx | 所选板生成源码 | 添加 HAL、启动汇编、ThreadX、USBX 等目标与源码 |
| usbx_integration.cmake | build.usbx 与 CubeMX 目标 | 替换两个 USB 应用模板；关闭 USBX 时裁剪库和描述符，并生成关闭状态的启动钩子 |
| 根 CMake 源文件筛选 | feature 与设备配置 | 从收集到的源码中排除未选择驱动/测试；限定 BSP 私有头文件可见范围 |
| CMake → Ninja | 最终目标依赖图 | 产生编译、链接规则；实际编译由 ARM GCC 执行 |
| export_editor_context.cmake | 板型、IOC、board.json | 给 GUI 导出只读资源，不读取应用参数，也不是固件的配置输入 |

Configure 阶段产生 build/<preset>/generated 下的 config.hpp、bsp_bindings.hpp/.cpp、robot_config.hpp；USBX 关闭时另有 usbx_disabled.c。构建目录还保存源文件清单、compile_commands.json 和 pnx-openocd.cfg。Build 阶段按这些规则编译并链接 ELF。它不会自动烧录，也不会自动运行诊断。

## generate_config.cmake 内部职责

1. 读取 board 内存策略、固定设备接线和 IOC 外设事实。
2. 根据 robot 选择 BMI088、LED、DMIMU 和电机驱动，根据 params 选择遥控器、USBX 和诊断。
3. 将 UART/CAN/SPI/GPIO/PWM/ADC 角色解析为真实、已配置资源，检查必要的方向、DMA 等条件。
4. 生成公共类型与逻辑名称，让共享代码通过 app::、board::device::、params:: 访问配置。
5. 在私有生成源中声明 HAL 句柄、生成句柄查找和绑定，避免共享模块依赖 MCU HAL。
6. 生成电机型号、控制模式、ID 等机器人组成信息。
7. 同时把 feature、资源数量等变量留在调用方 CMake 作用域中，用于裁剪源码。

它不负责 HAL 外设初始化、不执行 IMU 温控或姿态算法、不运行 test。test 仅影响生成结果与启动选择。

## 维护风险与优先级

| 优先级 | 当前问题 | 后果 | 建议 |
| --- | --- | --- | --- |
| 已处理 | 默认值多处维护 | 已集中到 configs/defaults.json；板型 params 为用户覆盖 | 修改默认数据后重新打包编辑器 |
| 高 | generate_config.cmake 同时计算规则、维护状态、拼接 C++ 字符串 | 一个新增参数可能需要修改多个远隔段落，作用域和顺序依赖不直观 | 先列清输入/输出变量；再分成参数解析与 feature、硬件绑定、代码输出三个主要职责，仍由一个入口调用 |
| 已处理 | 构建矩阵缺少可运行脚本 | 已补 tests/v2-validation/validate.py，并实际运行两板 Debug 矩阵 | 后续改动复用该脚本 |
| 中 | GLOB_RECURSE 收集全部源码，再靠路径正则排除 | 新目录/新驱动可能被意外编译；改路径可能破坏裁剪 | 优先把 app/diagnose 选择改为明确源码列表，再处理驱动；不必一次为每个目录建库 |
| 中 | 根据所有头文件目录推导 include 路径 | 部分未选模块头仍可见；BSP 私有目录识别依赖 /src 结尾 | 保留现有 public/private 边界，逐步显式列出公开 include，验证私有路径不外泄 |
| 中 | 根 CMake 读取并修改 CubeMX target 的 SOURCES/LINK_LIBRARIES | CubeMX 改变目标结构后 USB 模板替换可能失效 | 将修改集中在已有 usbx_integration.cmake；每次再生成检查 USBX 开关两种构建 |
| 中 | GUI 与生成器分别实现 test 依赖提示/校验 | 用户可能见到 GUI 通过、Configure 失败 | 用同一组合法/非法配置样例测试两者；无需现在建立通用规则引擎 |
| 中 | PS2 的 GPIO/SPI 绑定展开混在设备与板级解析之间 | 模块参数边界与其他模块不一致 | 后续单独决定外接设备 binding 形式；不在本次启动迁移中扩大范围 |
| 低 | 构建清单由 file(WRITE) 重写 | 可能触发不必要的依赖重建 | 输出内容不变时避免改时间戳，或使用 configure_file 模板 |

## 配置默认值与构建矩阵（已落实）

`configs/defaults.json` 是默认参数的唯一维护入口，包含公共值、两板覆盖值和串口推荐值。CMake 直接读取；编辑器编译时从同一文件生成打包快照，Schema 只保留结构、类型和范围。修改默认数据后需重新打包编辑器。

合并顺序为公共默认值、板型默认值、用户 params，按字段递归覆盖；数组整体替换。缺少一个字段不会丢失同模块的其他默认常量。缺省 USBX、板载消费者、遥控器和测试均关闭，不自动分配诊断 binding。串口推荐值不等于已启用的绑定。

未启用的 BMI088/AHRS、DMIMU、USB、遥控器、裁判系统和 CAN 诊断使用内部默认常量，忽略遗留调参值；不改写用户 JSON。PS2 仅在选用时解析。已声明的硬件 binding 仍需指向真实资源。UART/CAN 测试仅在开启时要求 test_uart/test_can，USB 和 IMU 测试要求对应消费者开启。

复现命令：

```powershell
python tests/v2-validation/validate.py
python tests/v2-validation/validate.py --configure-only
python tests/v2-validation/validate.py --build-type Release
```

两板默认均使用 pnx_bsp，运行前需切换到匹配板型的 BSP 分支，并通过 --board 选择该板。并行保留两套源码时，可以显式传入 --h7-bsp / --f4-bsp；脚本不再默认指定本地 worktree。依赖 CMake、Ninja 和 ARM GCC 在 PATH 中。每板覆盖 minimal、uart、can、usb_on、usb_off、bmi_off、quaternion、tactical 八种配置，另有四项缺少依赖的预期失败检查。临时 params/robot、日志、ELF 和 results.json 位于 build/v2-validation 下，不修改正在使用的板型配置；复用板型构建目录以避免重复编译整个 HAL。

本次两板共 16 个 Debug 完整镜像构建通过，8 项依赖缺失检查通过，编辑器 14 项测试通过。验证还检查了部分 AHRS 参数覆盖、F4/H7 温控默认差异及三个启动入口各只有一个定义。未进行硬件测试。以上落实并取代本文旧审查中“默认值未统一”和“矩阵脚本不存在”的状态。

## 推荐的下一轮整理顺序

1. 先补可重复的 H7/F4 配置与构建矩阵：默认关闭、单独 UART/CAN、USB 开关、BMI088 开关、两种 solver。
2. 统一默认数据与缺省语义。模块未启用时，不要求它的无关参数或诊断 binding。
3. 收敛根 CMake 的源码选择，让 app 与 diagnose 的编译组成一眼可见。
4. 最后整理生成器，先移动完整职责块，不同时改变 API、默认值或 JSON 格式。
5. 对长 C++ 输出段采用模板文件；避免 CMake 字符串中大量转义和分号占位符。

第一、二项已完成，后续从第三项继续。无需把生成器拆成十几个文件；优先减少重复规则、隐含作用域和跨文件同步修改。

## 本次验证范围

H7 Debug 与 F4 Debug 均完成编译链接。新增的 Core 调用在 USER CODE 区，未修改 main 或 HAL 生成初始化段。已检查两个 ThreadX 入口的定义位置；未启动调试、烧录或验证实际调度。当前结果不替代 USB 枚举、IMU 数据等硬件测试。

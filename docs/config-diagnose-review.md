# 两板配置与诊断审查

## 当前默认策略（0.4，替代下文旧默认选择）

两板 USBX、BMI088、LED、DMIMU、遥控器和所有测试均默认关闭。原有 Robot 电机列表保持不动；不再预置 test_uart、test_can、chassis 或报告串口。温控推荐参数保持两板各自取值，板级固定接线不变。

开启 UART/CAN 测试时，GUI 提示加入相应 binding 并提供可搜索的添加入口；取消测试保留 binding。USB/IMU/遥控器/裁判/GPIO/电机提示对应依赖。生成器仅对开启的测试要求资源；缺少 test 分组等同所有测试关闭。测试仍通过 app_start 引用，main 不变。

两板最小默认固件编译链接通过：F4 Debug FLASH 49040 B、RAM 62832 B；H7 Release FLASH 50088 B。13 项回归通过，含开启/关闭与缺少 binding 的真实生成器用例。隔离无头浏览器验证 CAN 提示、搜索、固定别名及新增消息。尚无硬件测试。

## F4 默认配置依据

参考工程：`C:/01_Workspace/RM/RM26_F4_referee/RM26_F4`。

- `User/BSP/Src/bsp_usart.cpp:36` 将 SBUS 双缓冲交给 USART3；`Core/Src/usart.c:110` 配置 100000 波特率、9 位字长、偶校验、仅接收（实际有效数据 8 位）。当前 F4 IOC 与生成源码相符。`bindings.remoter_uart=usart3`，`remoter.source=dr16`。
- `User/Task/Src/TaskChassis.cpp:333` 等位置用 huart6 发送裁判协议模拟帧；`Core/Src/usart.c:139` 为 115200、8N1。这里是裁判数据模拟发送端的证据，不是旧工程裁判接收服务的证据。按用户要求，当前 F4 裁判服务绑定 USART6。
- F4 串口诊断改用 USART1，921600、8N1，避免与裁判服务同时接管 USART6。现有 report_uart 保持 USART1；若另接独立报告服务，应避免与二进制诊断协议混发。
- 补齐 F4 的 AHRS、DMIMU、遥控器离线超时、裁判线程和 test 参数；保留 F4 的 AHRS 休眠 1 tick 与独立温控参数。删除误加的 F4 `can.fdcan1` 空配置。未复制 H7 的物理总线、ADC 或 PS2 引脚。

## 配置界面

- H7/F4 各自使用板型默认快照，恢复默认不会改变 robot 电机组成。
- PS2 参数仅在 `remoter.source=ps2` 时显示、校验和生成，位于 remoter 分组内；不再要求额外开启 ps2.enabled。切换后已保存的 PS2 字段可以保留，非 PS2 模式不读取它们。
- BMI088 位于最后，标注不要修改、使用板型默认值，普通输入只读。恢复默认仍可使用。F4 90% 与 H7 6% 的预热默认值保持各自配置；没有把 H7 温控参数覆盖到 F4。

## diagnose 与 test

启动链保持 `App_ThreadX_Init -> app_start -> diagnose_start`，没有改 main 文件。
`test.auto_run_on_boot` 控制自动启动；H7 当前为 false，F4 为 true。关闭时不创建诊断线程，可以显式调用 diagnose_start。

| 参数 | 诊断内容 |
| --- | --- |
| imu | 已选 BMI088/AHRS 或 DMIMU |
| usart | test_uart 主机二进制收发 |
| usb | USBX CDC 主机收发，需 build.usbx |
| remoter | 已选遥控器及映射；PS2 使用自身诊断入口，避免重复初始化 |
| referee_ui | 裁判接收与 UI，需有效裁判串口 |
| can | test_can 的初始化和接收计数、最近 ID/长度；不自动发送帧 |
| gpio_leds | 固定 led_r/g/b 角色的输出循环 |
| motor_demo | 现有三电机测试配方 |

CAN 状态在 `demo_debug_instance.can`。H7 test_can=fdcan1，F4 test_can=can2。ready 只代表初始化成功，rx_count 增加才表示收到帧；目前不提供 CAN 发送回环通过判定。

F4 已有 RGB GPIO 角色，因此启用 gpio_leds。H7 板载灯使用 SPI，未声明 RGB GPIO 输出，所以默认关闭这个测试；没有借用 BMI088 片选作为测试输出。要测试其他 GPIO，先在 board.json 声明固定角色并适配诊断配方。ADC、独立 SPI/PWM/EXTI 的完整专项测试仍未实现，BMI088 诊断只间接覆盖其中部分链路。

## 硬编码与条件编译结论

- 本次检索 diagnose、pnx_modules、pnx_devices 的 C++ 文件，未发现具体 UART/CAN 枚举或 HAL 句柄绑定；`bus::none` 是合法的未配置值。
- diagnose/app.cpp 原有头文件与调用两处重复条件编译已改为编译期条件选择。保留 AHRS 中 BMI088/DMIMU 的类型与源文件裁剪，以及 C/C++ 兼容保护；不能简单全部换成运行时 if。
- 仍有明确配方常量：GPIO 测试固定三色灯角色、遥控器监测周期 5 tick/超时 500 tick、三电机诊断配方和 UI 绘制内容。这些是诊断行为，不是 MCU 外设硬编码；后续需要通用专项测试时再参数化。
- H7 的 bsp_can_diag 仍是 FDCAN 专属扩展，不能用 F4 的 can_diag.enabled=true 推断它已运行；本次新增 test.can 使用两板公共 API。
- 生成器仍有历史串口默认值与专用 PS2 引脚展开逻辑，后续可继续统一为显式角色；本次没有扩大 BSP 抽象层或修改 CubeMX 源码。

## 验证

- F4 Debug 完整编译链接通过，FLASH 120352 B，RAM 81304 B。
- H7 Release 默认配置通过；另用独立构建保留 diagnose_start，验证关闭自动启动时所有选中诊断仍可完整链接。
- 插件 9 项测试通过，包含两板 GPIO 别名生成、PS2 条件字段、板型默认值。
- 未烧录或进行实际串口、CAN、遥控器、裁判通信验证。GUI 的新分组未进行人工 VS Code 验收。
- 插件安装包：`../pnx_config_editor/pnx-config-0.3.0.vsix`。

# 板端诊断

`diagnose/` 是当前默认固件入口，不是机器人业务示例。`app_start()` 调用 `diagnose_start()`；在 [app.cpp](app.cpp) 中选择要启动的检测。

当前已有：

- `imu/`：BMI088 AHRS 与可选 DMIMU 的状态和在线检测。
- `motor/`：当前配置的测试电机注册、发送和在线检测。
- `remoter/`：遥控器输入、离线和映射结果。
- `referee_ui/`：裁判系统接收与 UI 更新。
- `gpio/`：板级逻辑 GPIO 输出诊断；F4 默认循环 RGB LED。
- `usart/`、`usb/`：主机收发与协议校验。

诊断状态集中在 `common/demo_debug.hpp`，可用调试器 Watch 查看。开始机器人应用时，应替换 `app_start()`，不要把诊断逻辑带入业务控制循环。

注意：电机诊断会发送测试命令；上电前确认机构安全。CAN、ADC、SPI、PWM、EXTI、Flash、DWT 目前没有独立诊断入口。


Current test selection and required bindings are documented in [configuration](../docs/configuration.md). H7/F4 BSP and CAN capability differences are documented in [H7/F4 unification](../docs/concepts/h7-f4-unification.md).

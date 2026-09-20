# 飞镖迁移交接

## 工作边界

本轮原定工作 45 分钟，2026-09-19 15:26:34 开始，最晚 16:11:34 停止
（Asia/Shanghai）。用户随后要求可以停止、不再增加检测，因此实际在
16:07:50 停止，累计约 41 分 16 秒。迁移任务暂停，保留以下结果与未完成事项。

- 工作分支：`codex/dart-v2-migration`，改动尚未提交或推送。
- 新架构基线：`refactor/migration` / `origin/refactor/migration`，
  `237b4a2f3baf26fd32f59fe67f3e4ec90c8201d1`，开始时已核对远端。
- 旧应用基线：`oldframe` / `origin/oldframe`，
  `7ad4f98a2da1b151394db211ba0a6a0b0458a24d`。
- 先完整阅读 `docs/` 文档、AGENTS、README 与配置说明，再进行迁移。
- 范围是 H723 飞镖应用。没有更改已有 BSP、Device、Module、Lib 子模块；
  没有刷写或连接机构执行动作，也没有宣称 F407 完成飞镖迁移。

## 当前可交接成果

应用分为 control、launcher、motor、sensor、vision、host 和 monitor，入口仍是
`app_start()`。使用现有 ThreadX、消息、PID、DM 电机、遥控、裁判、GPIO、PWM、
CAN、UART 与 USB 公共 API，没有另造框架。详见 [迁移说明](dart-migration.md)。

`params.dart.enabled` 控制应用源文件和启动选择。旧标定、拉力、时序、线程参数和
PID 初值在 H723 `params.json` 的 dart 部分。电机组成在 `robot.json`；逻辑绑定
在 `board.json`；硬件参数在 IOC。应用生成头只写到 build，不手改生成文件。

CubeMX 已真正再生成 H723 Core：恢复 TIM1_CH3/PE13 的 50 Hz 舵机输出、
PE0/PE14 输入、USART10 TX DMA 和串口速率，CAN 与扩展过滤匹配旧协议。
CubeMX CLI 改出的路径错误 CMake/.mxproject 已恢复原模板（没有新增源文件名）；
Drivers 的纯换行改动也已恢复。没有手写 Core 外设代码伪造缺失资源。

保留旧固定标定、16 镖参数、距离插值表、`[3,4,5,8]` 镖序、32 kg 预拉、
G4 八字节拉力协议、视觉 13/13/25 字节格式和上位机协议。
额外提供各模块 debug 与 `dart::motor::pidtuning`，便于 Watch 和 RAM 调参。
新增功能仅包括现有 XV2 驱动缺少的旧限流速度分片命令、应用多圈展开和应用业务。

## 已完成验证

| 检查 | 结果与范围 |
| --- | --- |
| H723 Debug 全镜像 | 链接通过；FLASH 175708 B，DTCMRAM 85888 B，RAM_D1 6720 B |
| H723 Release 全镜像 | 链接通过；FLASH 110440 B，DTCMRAM 85880 B，RAM_D1 6720 B |
| 应用严格编译 | 12 个应用翻译单元，实际产品参数下 `-Wall -Wextra -Werror -fsyntax-only` 通过 |
| 板绑定 | `test_board_bindings.py` 6 项通过，检查 IOC、实际 Core、JSON 的 PWM/GPIO/UART/CAN 一致性 |
| 电机迁移 | `test_motor.py` 通过：ARM 编译期已知报文、44 组旧/新帧比较、边界与 1201 个多圈轨迹采样 |
| 上位机兼容 | `host_protocol_test.py` 通过：命令枚举、14 个规范化旧业务函数等价及真实 CRC 编译期向量 |
| 配置生成 | `test_app_config.py` 通过：启用、关闭源排除、时间戳稳定、缺 USB/遥控、零周期和非法镖序 |
| 可选路径完整构建 | 独立 robot 输入关闭 LED 后链接通过；`dart.enabled=false` 的模板诊断入口镜像也链接通过 |
| 真实业务 ARM 模拟 | `control_runtime_test.py` 76 项通过，执行生产 launcher/control/configuration 源码，覆盖遥控、瞄准、标定、门融合、装填发射、错误态和 tick 回绕 |
| 真实接收 ARM 模拟 | `io_runtime_test.py` 233 项通过（CRC 关闭 117 / 开启 116）；运行生产 sensor/vision 回调及 sensor 循环，覆盖碎片、连帧、CRC、双包发送、busy/未连接、拉力超时与恢复 |
| 电机控制 ARM 模拟 | `motor_runtime_test.py` 58 项通过；生产 motor 的 PID 热调/ref_override/reset/模式、扳机两段定时、边沿重触发和 tick 回绕；链接既有 PID 与限幅实现 |
| 上位机 ARM 模拟 | `host_runtime_test.py` 24 项通过；生产 host/service/configuration/CRC，输出 62 帧（64 次尝试），覆盖 busy 重试、超时、CRC、两次 GET_TABLE 超容量批次和 SET_SEQUENCE 只应用一次 |
| 子模块边界 | BSP、Devices、Modules、Libs 无改动 |

业务模拟只替代 ThreadX 时间，链接时去除未运行的线程和通信段；不包含调度、
真实外设和机械动作。接收模拟的 transport、time、message 和 GPIO 使用 stub，
没有模拟实际 DMA、USB 枚举或调度。Release 仍有模板已有 USBX `ux_port.h` 数组边界告警，
没有通过更改共享库或关闭警告掩盖它。

四个 ARM 执行脚本合计 391 项断言。电机测试替代 PWM 接口，上位机测试替代
UART、锁、时钟和调度；这些边界不能被计为实板或真实并发验证。

复现命令（先确认 `pnx_bsp` 属于 H7）：

```powershell
cmake --preset h723-debug
cmake --build --preset h723-debug --parallel 8
cmake --preset h723-release
cmake --build --preset h723-release --parallel 8
python tests/dart/test_board_bindings.py
python tests/dart/test_motor.py
python tests/dart/host_protocol_test.py
python tests/dart/test_app_config.py
python tests/dart/control_runtime_test.py
python tests/dart/io_runtime_test.py
python tests/dart/motor_runtime_test.py
python tests/dart/host_runtime_test.py
```

模拟依赖已局部安装在 `build/migration/python`；新环境运行
`python -m pip install --target build/migration/python unicorn`。

本地证据：

- `build/migration/h723-debug-final.log`、`h723-release-final.log`。
- `build/migration/no-led-build.log`、`no-led-build-final.log`、`dart-disabled-build.log`：关闭可选功能的链接日志。
- `build/migration/control_emulation/result.json`。
- `build/migration/io_emulation/legacy_crc_off/result.json`、`crc_on/result.json`。
- `build/migration/motor_emulation/result.json`、`host_emulation/result.json`。
- `build/dart-config-test/` 的配置输入和逐例日志。
- `build/dart-migration/` 的 CubeMX 再生成日志及原文件快照。
- `build/h723-debug/pnx_embedded.elf`、`build/h723-release/pnx_embedded.elf`。

## 必须继续确认的事实

1. **gantry 在旧工程本来就没有注册。** 本次保持 absent；第二发及后续装填会
   停在 gantry 步骤，`launcher::debug.gantry_missing=true`。恢复时先确认硬件
   型号、地址和需求，再按 robot 配置使用已有电机能力，不能伪造到位反馈。
2. **旧自动门控默认不放行。** `legacy_game_gate=true` 保持旧注释掉比赛状态
   更新的效果。联调需要自动发射时明确选择 false，再验证裁判 game_status=4、
   门状态、视觉稳定与每次开门两发；默认配置不能被描述为全自动流程已可用。
3. **启动会有旧同步带 20 rad/s 指令。** `initial_syn_spd=20` 保留旧一次发送，
   后续控制周期覆盖；该镜像不是静止诊断镜像。方向、零点、反馈多圈连续性和
   扳机 1750/1670 μs 行为均未上板确认。
4. **UART5 保留 100000/9N2。** 这是旧工程和模板实际设置；需确认 DR16 接收。
   USART1 裁判、UART7 拉力、USART10 上位机当前为 115200。
5. **PID 0x45 的 D 项沿用既有语义。** 当前和旧共享库只有纯 position 模式才
   算普通 D；没有私改库修复。切换 pidtuning.mode 会改变语义，见
   [电机调参说明](dart-motor-validation.md)。
6. **CRC 与持久化未扩大范围。** 视觉 RX 校验默认关闭以兼容旧端；可通过
   `vision.verify_crc` 选择。上位机/Watch 改动只在 RAM 生效；长期配置写回 JSON，
   没有绕过架构加入固件 Flash 写接口。
7. 板上还需检查实际线程栈余量、优先级时序、消息更新、UART DMA、USB 枚举、
   CAN 两帧发送、拉力单位和整套机械流程；编译和 CPU 模拟不覆盖这些项目。

## 下一次接手顺序

先看本文件、迁移说明和电机调参文档，再看 `git diff` 与 `git status`。
优先复核本次明确保留的旧行为，确认 gantry 和自动门控的目标状态。
然后用 Debug 的 startup/control/launcher/motor/sensor/vision/host/monitor debug
逐层核对，再做实际联调；不要把观察到的硬件差异通过硬编码到 BSP 中解决。
新参数写到所属 JSON/IOC，硬件变化先 CubeMX 再生成，再重新构建。

本工作区的新 `app/dart/`、`tests/dart/` 和文档尚未加入提交，接手时需一起保留。

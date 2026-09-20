# 飞镖电机迁移与调参

来源：`origin/oldframe` 提交 `7ad4f98a2da1b151394db211ba0a6a0b0458a24d`。
实现为 `app/dart/motor.cpp`、`dart_motor.hpp` 和两个应用内纯操作
`motor/motor_ops.hpp`。没有修改 `pnx_bsp`、`pnx_devices` 或 `pnx_libs`。

## 电机及运行行为

- 同步带使用现有 `motors::dm4310`，yaw 使用现有 `motors::dm8009p`，
  通过现有 `motors::motor_service` 注册、在线程中使能、周期发送。
  型号对应关系、CAN 总线和地址由 `robot.json` 生成。
- 同步带在应用中展开现有驱动的 `[-π, π]` 位置反馈，保持旧多圈位置语义。
  每次新反馈才展开；相邻采样的实际转动需要小于 π。CAN 中断更新的反馈
  在短暂关闭中断后复制，然后在线程中做控制和消息发布。
- 左右副弦沿用旧 X_V2 `0xC6` 限电流速度命令，扩展 ID 的 Classic CAN
  两片传输。现有 XV2 设备不具备这套传输和限流操作，因此只在应用中补充
  这一操作。地址和正方向来自 `robot.json`，不在 BSP 中加入设备语义。
- 闭环拉力输出取负作为 RPM；手动仍使用 0.03 死区和 ±500 RPM；
  旧 100 kg 限制保留，超过时只阻止继续拉紧。未添加恢复状态机。
- 舵机沿用 1750 μs 打开、1670 μs 复位，各保持 1000 tick 后停止 PWM。
  启动时维持复位 PWM，释放命令上升沿启动一次打开再复位动作。
- **旧启动同步带速度 20 rad/s 保留**，由
  `params.dart.motor.initial_syn_spd` 明确配置；DM 使能完成后发送一次，
  随后首个控制周期覆盖。该行为来自旧工程，并未完成本次硬件验证。
- **gantry 保持未启用**：旧 `TaskMotors::MotorsInit()` 的注册/使能代码已被注释。
  新代码不注册该电机，`motor_fdb.gantry_online` 和 watch 始终为 false，
  不生成虚假位置到位反馈；依赖龙门架的后续装填需要另行确认设备组成。

电机线程周期、优先级、PID 初值、速度/电流/拉力限制与 PWM 动作时间由
`params.json` 的 `dart.motor` 生成。`motor::init()` 返回的是线程创建结果；
实际设备初始化结果需要查看下面的 watch。

## Watch 与 PID tuning

两个长期有效、可在调试器中直接访问的符号：

- `dart::motor::debug`：初始化阶段/状态、循环和接收计数、DM 使能失败及错误码、
  位置/速度 `ref`、`fdb`、多圈数、副弦指令及反馈、PWM 阶段和发送失败计数。
- `dart::motor::pidtuning.syn`、`.string_l`、`.string_r`：各自的
  `kp`、`ki`、`kd`、`max_out`、`max_iout`、`mode`、`ref_override`、`ref`、`reset`。

激活的 PID 每周期读取这些调参值；修改只在 RAM 中生效，不写回 JSON 或 Flash。
同步带 PID 只在 `synbelt_mode=position` 时工作；副弦 PID 只在
`string_able=true` 时工作。`ref_override=true` 仅替换该已激活 PID 的目标值，
不会改变发射状态、切换模式或自行打开副弦闭环。`reset=true` 会在该 PID
下次运行时清除已有状态，随后自动置回 false；改变 `mode` 也会重置状态。

`debug.syn`、`.string_l`、`.string_r` 提供 `ref/fdb/err/out`。
副弦 PID `out` 与实际发送的 `string_l_spd_ref` / `string_r_spd_ref` 符号相反，
后者还体现旧拉力上限处理。副弦反馈只有驱动实际返回数据时更新；旧应用没有
轮询这些反馈，因此接收计数为零不能解释为静止或在线。

**PID 模式保持旧行为**：默认 `mode=0x45`（position、integral_limit、
trapezoid_integral），同步带仍为 `kp=10, ki=0, kd=10`，左右拉力为
`kp=120, ki=0, kd=0`。旧库和当前共享库都只有在 `mode == position`
时才计算普通 D 项，所以默认组合模式中的同步带 `kd=10` 实际不生效。
本迁移未修改库修正这一语义。显式把 `pidtuning.*.mode` 改为 `1` 会采用
库的纯位置式 PID，并同时失去组合模式中的梯形积分和积分限制选项；
这是一项控制语义变更，不能当作默认等效迁移。PID 不接受 dt，改变控制周期
会改变 ki、kd 的实际效果。

## 已完成验证

```powershell
python tests/dart/test_motor.py
cmake --build build/h723-debug --target CMakeFiles/pnx_embedded.dir/app/dart/motor.cpp.obj
```

两项均通过。脚本使用真实生产头文件中的 constexpr 函数，并从指定旧分支
读取原始 CAN 分片循环及多圈展开语句，只替换 HAL 投递为测试帧捕获。
ARM GCC 的 `static_assert` 检查：

- 已知 +500 RPM 命令字节、两个地址、两个正方向、正负速度、正负零；
- 小数截断、速度编码 255/256/65535 的字节边界；
- 44 组新旧报文逐字节一致；
- ±π 严格边界、1201 个连续正反转多圈样本与旧实现和预期轨迹一致。

这些是 **ARM 编译期计算与目标对象编译**，没有宿主运行、板上执行、刷写、
CAN 总线测量或机构动作验证；不能据此宣称电机方向、零点或实际发射已验证。

另有 `python tests/dart/motor_runtime_test.py` 在 Cortex-M7 模拟器中执行生产
`motor.cpp` 的 `update_pid` 和 `update_trigger`，链接既有 PID 与限幅代码，
58 项断言通过：覆盖热调、ref_override、reset、模式切换、两段 1000 tick、
持续信号不重复触发、新边沿重启、tick 回绕及 PWM 错误计数。
PWM 接口使用测试桩；没有执行 DM 初始化、真实线程调度或机构动作。
结果保存在 `build/migration/motor_emulation/result.json`。

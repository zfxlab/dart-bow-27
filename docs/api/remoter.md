# Remoter

`remoter` 把 DR16、VT03、同步 PS2 手柄或旧串口 PS2 接收器的输入整理为统一的 `remoter::state`，让机器人应用把“遥控器按键怎么对应机器人动作”留在自己的映射函数中。

## 注意

- 在 `params.json` 中设置 `remoter.source` 为 `dr16`、`vt03`、`ps2` 或 `ps2_uart`，然后重新执行 CMake configure。当前配置生成器一次只启用其中一种来源。
- DR16 和串口 PS2 接收器使用 `bindings.remoter_uart`；VT03 使用
  `bindings.vt03_uart`。选择 VT03 时该 UART 必须在 IOC 中配置 RX DMA。
- 同步 PS2 的 GPIO/SPI backend 与引脚在顶层 `ps2` 配置中选择；其 binding 由 `build/<preset>/generated/bsp_bindings.hpp` 在编译期生成。

## 先定义机器人的 command

可以不要让底盘、云台或发射机构直接判断 `remoter::state` 里的某个遥控器按键。先定义只描述机器人意图的上层 command，例如“前进、转向、射击、松弛”。机器人其余逻辑只使用这个 command。

然后为每种遥控器分别写一个 map 函数，把统一的 `remoter::state` 转成相同的 `robot_command`。更换遥控器时，只需要更换初始化时注册的 map 函数，控制逻辑不需要跟着改。

```cpp
#include "config.hpp"
#include "remoter.hpp"

namespace robot::application
{
namespace
{

struct robot_command
{
    float forward = 0.0f;
    float turn = 0.0f;
    bool shoot = false;
    bool relax = true;
};

void use_command(const robot_command& command) noexcept
{
    // 把 command 复制或发布到你的机器人应用；这里省略具体交接方式。
    // 不要在这里直接执行耗时控制。
}

void map_dr16(const remoter::state& input) noexcept
{
    robot_command command{}; // 默认安全：停止、松弛
    if (!input.offline && input.active_source == remoter::source::dr16)
    {
        command.forward = input.right_y;
        command.turn = input.right_x;
        command.shoot = input.mouse_left;
        command.relax = input.right_sw == remoter::sw_state::low;
    }
    use_command(command);
}

void map_vt03(const remoter::state& input) noexcept
{
    robot_command command{};
    if (!input.offline && input.active_source == remoter::source::vt03)
    {
        command.forward = input.right_y;
        command.turn = input.right_x;
        command.shoot = input.button;
        command.relax = input.pause;
    }
    use_command(command);
}

void map_ps2_uart(const remoter::state& input) noexcept
{
    robot_command command{};
    if (!input.offline && input.active_source == remoter::source::ps2_uart)
    {
        command.forward = input.right_y;
        command.turn = input.right_x;
        command.shoot = remoter::is_held(input.ps2_uart_buttons, remoter::ps2_uart_button::r1);
        command.relax = remoter::is_held(input.ps2_uart_buttons, remoter::ps2_uart_button::select);
    }
    use_command(command);
}

void map_ps2(const remoter::state& input) noexcept
{
    robot_command command{};
    if (!input.offline && input.active_source == remoter::source::ps2)
    {
        command.forward = input.right_y;
        command.turn = input.right_x;
        command.shoot = remoter::is_held(input.ps2_buttons, remoter::ps2_button::r1);
        command.relax = remoter::is_held(input.ps2_buttons, remoter::ps2_button::select);
    }
    use_command(command);
}

remoter::update_callback configured_mapping() noexcept
{
    if constexpr (::config::feature::enable_dr16)
    {
        return remoter::update_callback::bind<&map_dr16>();
    }
    else if constexpr (::config::feature::enable_vt03)
    {
        return remoter::update_callback::bind<&map_vt03>();
    }
    else if constexpr (::config::feature::enable_ps2)
    {
        return remoter::update_callback::bind<&map_ps2>();
    }
    else if constexpr (::config::feature::enable_ps2_uart)
    {
        return remoter::update_callback::bind<&map_ps2_uart>();
    }
    return {};
}

types::status init_remoter() noexcept
{
    remoter::config cfg{};
    cfg.on_update_callback = configured_mapping();
    return remoter::service::instance().init(cfg);
}

} // namespace
} // namespace robot::application
```

`remoter::service::init()` 会读取生成的 `config::feature::enable_*` 开关并启动已选择的来源。`configured_mapping()` 也读取同一组开关，因此你把 `remoter.source` 改为另一种遥控器并重新生成后，注册的 map 函数会一起切换。

当前实现中，第一次成功 `init()` 后再次调用 `init()` 不会替换回调。若要切换遥控器或映射，修改配置并重新构建、烧录，不要尝试在运行中重新初始化服务。

## 输入状态

`remoter::state` 是模块提供的统一输入。最常用的成员如下：

| 成员 | 用处 |
| --- | --- |
| `offline` | 遥控器是否离线。离线时应输出安全 command。 |
| `active_source` | 当前输入来源：`dr16`、`vt03`、`ps2`、`ps2_uart` 或 `none`。 |
| `right_x`、`right_y`、`left_x`、`left_y` | 已归一化的摇杆量。 |
| `left_sw`、`right_sw` | DR16 的三档开关状态。 |
| `mouse_left`、`mouse_right`、`key` | DR16 / VT03 的鼠标和键盘输入。 |
| `button`、`pause` | VT03 的扳机和暂停输入。 |
| `ps2_buttons` | 同步 PS2 手柄当前按键位图；用 `is_held()` 与 `ps2_button` 判断单个按键。 |
| `ps2_uart_buttons` | 旧串口 PS2 接收器当前按键位图；用 `is_held()` 与 `ps2_uart_button` 判断单个按键。 |

回调在 Remoter 的合并线程中运行，且在模块发布状态之后同步调用。传入的 `state` 引用只在本次调用中有效；不能在回调中等待、加锁或做耗时工作。若控制线程与回调不在同一线程，应把 command 通过应用自己的消息通道或同步方式交接。

## 常见错误

- 在底盘、云台等控制代码里直接判断 `mouse_left`、`ps2_uart_buttons` 等具体遥控器字段。这样换遥控器时会牵动所有控制逻辑。
- 忘记先检查 `offline`，导致掉线后继续使用上一次输入。
- 只改 `params.json`，没有重新执行 CMake configure；`enable_dr16` 等生成开关和 UART 绑定不会自动更新。
- 第二次 `init()` 试图更换 `on_update_callback`。当前实现会保留第一次成功初始化时的回调。

## 相关内容

- [配置](../configuration.md)
- [通用回调](../concepts/interrupt-callback.md)

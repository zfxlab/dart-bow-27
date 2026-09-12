# bsp::pwm

`bsp::pwm` 启动已配置 PWM 通道，并设置占空比、周期或脉宽。

## 注意

修改周期会影响同一计时器上的所有 PWM 通道；若只想改一个输出，优先设置占空比或脉宽。

## Header / Namespace

- Header：`bsp_pwm.hpp`
- Namespace：`bsp::pwm`

## 初始化

在启动阶段依次调用 `init()`、`start()`，再设置输出。为 PWM 通道在板卡配置中定义角色名后，应用代码使用 `app::pwm::<角色>`。

```json
{ "bindings": { "pwm_channels": { "servo": { "timer": "tim12", "channel": 2 } } } }
```

这是 `board.json` 中的配置片段；应用代码只使用生成的 `app::pwm::servo`。

对必须在控制线程停止或调试断点时自动归零的输出，可以给 PWM
角色绑定一个独立的 failsafe timer：

```json
{
  "bindings": {
    "pwm_channels": {
      "protected_output": {
        "timer": "tim3",
        "channel": 4,
        "failsafe_timer": "tim6"
      }
    }
  }
}
```

`failsafe_timer` 必须在 IOC 中配置 Update DMA；它不能与 PWM 输出使用同一个
timer，也不能同时保护多个 PWM 角色。它的预分频、周期、DMA stream/channel/request
均由 IOC 管理，当前通用安全上限为 250 ms。每次设置非零占空比或脉宽时，BSP 会自动刷新 failsafe；超时后 DMA
直接向受保护通道的 CCR 写零，不需要 CPU 或中断执行。应用和设备层不需要调用额外的
续租接口。

```cpp
// 使用片段：启动配置为舵机输出的 PWM 并输出 50% 占空比
#include "bsp_pwm.hpp"

constexpr auto output = app::pwm::servo;

types::status start_output() noexcept
{
    types::status status = bsp::pwm::init(output);
    if (status != types::status::ok)
    {
        return status;
    }
    status = bsp::pwm::start(output);
    if (status != types::status::ok)
    {
        return status;
    }
    return bsp::pwm::set_duty(output, 0.5F);
}
```

## 核心类型

| 类型 | 用途 |
| --- | --- |
| `channel` | 生成配置中的 PWM 通道标识。 |

## 常用接口

| 接口 | 用途与注意事项 |
| --- | --- |
| `init(channel)` | 检查通道及定时器时钟配置。 |
| `start(channel)` / `stop(channel)` | 启动或停止该 PWM 输出。 |
| `set_duty(channel, ratio)` | 设置 0.0 到 1.0 的占空比。 |
| `set_period_us(channel, us)` | 设置定时器周期；同一计时器上的所有 PWM 通道都会受影响。 |
| `set_pulse_width_us(channel, us)` | 设置当前通道的高电平脉宽。 |

## 常见错误

- 把 `set_period_us()` 当成单通道操作，意外改变同一定时器的其他通道。
- 传入小于 0、超过 1 或非有限值的占空比。
- 直接控制已由其他模块管理的 PWM 通道。

## 相关内容

- [配置](../configuration.md)

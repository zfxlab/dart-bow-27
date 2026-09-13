# Referee

`referee` 接收并解析裁判系统串口数据，把当前需要的比赛、机器人、功率和伤害等数据提供给机器人应用。

## 注意

- referee线程实现按header分包，但不会主动发送信息给上层；需要裁判系统数据时需要在上层逻辑中注册裁判系统回调函数并自己publish
- 在 `params.json` 的 `bindings.referee_uart` 选择裁判系统接入的 UART，这个绑定会生成 `app::uart::referee`；当前 H723 profile 使用 `usart1`，F407 profile 使用 `usart6`。详见[配置](../configuration.md)。
- 服务自己占用该 UART 的 DMA 接收。不要再用 `bsp::usart` 或另一个模块初始化、接收同一个串口。


## 最小接入

在自己的应用线程初始化阶段调用一次 `referee::service::instance().init()`。服务会创建内部接收线程，并启动裁判系统 UART 的 receive-to-idle DMA。

下面是使用片段，不是完整程序。它在同一个回调中分别接收比赛状态和飞镖信息；机器人控制代码随后应从自己的应用状态或消息通道中读取这些值。

```cpp
#include "config.hpp"
#include "referee.hpp"

#include <cstdint>

namespace robot::application
{
namespace
{

struct referee_state //定义自己需要的裁判系统信息
{
    GameStatus_t game_status{};
    DartInfo_t dart_info{};
};

referee_state latest_referee{};

//回调函数
void on_referee_update(const referee::packet_store& packets,
                       const referee::update_info& update) noexcept
{
    switch (update.command_id)
    {
    case RefereeID::GameStatus:
        latest_referee.game_status = packets.game_status;
        break;

    case RefereeID::DartInfo:
        latest_referee.dart_info = packets.dart_info;
        break;

    default:
        break;
    }
}

types::status init_referee() noexcept
{
    referee::config cfg{};
    cfg.thread_priority = params::referee::thread_priority;
    cfg.on_update_callback = referee::update_callback::bind<&on_referee_update>();
    return referee::service::instance().init(cfg);
}

} // namespace
} // namespace robot::application
```

`config` 是初始化时传入的设置；通常只需要设置 `on_update_callback`。`uart_port` 默认就是生成的 `app::uart::referee`，不需要在应用代码中填写具体 UART。`config` 本身可以是局部变量：第一次成功初始化时，服务会复制它。

`on_referee_update()` 在 Referee 的接收线程中同步执行，不能等待、加锁或做耗时计算。回调参数只在这次调用内有效；如要在其他线程使用数据，应复制需要的字段，并通过应用自己的消息通道或同步方式交接。上例的 `latest_referee` 只展示复制字段；不能让另一个线程无同步地同时读写它。

## 能直接读取什么

`referee_protocol.hpp` 定义了裁判协议中的数据结构和命令 ID。

注意，协议中定义某个包，不表示当前服务已经解析它。只有 `packet_store` 中已有对应成员、且实现实际处理的包，才会被写入并能通过本模块读取。收到 CRC 正确但尚未支持的包时，回调仍会执行，但存储内容不会更新；CRC 或长度错误的帧不会触发回调，计数会记录在 `status()` 中。

`update` 用来说明这次回调对应的包：

| 字段 | 用处 |
| --- | --- |
| `command_id` | 本次裁判包的命令 ID，例如 `RefereeID::GameRobotStatus`。用它区分不同的包，并读取 `packets` 中对应的数据。 |
| `got_valid_frame` | 本帧是否通过 CRC 和长度检查。当前实现只有有效帧才调用回调，因此这里目前总是 `true`。 |
| `is_known_packet` | 当前服务是否支持并已保存这个包。若为 `false`，不要把 `packets` 中的旧数据当作本次包的数据。 |

`service::instance().packets()` 也会返回这份存储，但它会被接收线程持续写入；当前实现没有为跨线程读取提供同步保护。应用不要在其他线程中直接读取它，应在回调中复制所需字段后再交接。

`status().online` 在收到第一帧有效数据后变为 `true`。注意，当前实现没有超时离线判断，因此它不会自动变回 `false`；不要把它当作完整的掉线检测。

## 常见错误

- 在同一 UART 上同时启动 `referee` 和自定义 USART 接收。该 UART 只能由一个接收者管理。
- 在 `on_update_callback` 中直接执行控制循环、等待 ThreadX 对象，或使用其他可能阻塞的接口。
- 认为 `packet_store` 包含裁判协议的全部数据。需要其他包时，先确认它是否已有对应成员，并由当前实现解析。
- 第二次调用 `init()` 试图替换回调或优先级。当前实现会直接返回 `ok`，保留第一次成功初始化时的配置。

## 相关内容

- [配置](../configuration.md)
- [通用回调](../concepts/interrupt-callback.md)

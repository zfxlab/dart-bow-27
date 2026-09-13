# bsp::can

`bsp::can` 提供已在板卡配置中声明的 CAN 总线初始化、发送和中断接收回调入口。

## 注意

只在需要直接收发自定义 CAN 协议时调用，例如自定义上下板通信。电机、DMIMU 等已有设备会在内部使用 CAN，应用不需要直接操作 `bsp::can`。

## Header / Namespace

- Header：`bsp_can.hpp`
- Namespace：`bsp::can`

`bus`、`bus_type` 和 `id_type` 由生成的 `config.hpp` 提供。
## 初始化

在启动阶段或应用线程中，先注册接收回调，再初始化总线。`init()` 会配置过滤器、启用中断并启动 FDCAN

以下示例假设已在 `params.json` 配置语义总线和收发 ID：

```json
{
  "bindings": {
    "can_buses": {
      "chassis": {
        "bus": "fdcan2",
        "rx_header": "0x201",
        "tx_header": "0x200"
      }
    }
  }
}
```

重新执行 CMake configure 后，生成 `app::can::chassis`、`app::can::chassis_rx_header` 和 `app::can::chassis_tx_header`。

```cpp
// 使用片段：放在应用的初始化阶段。
#include "bsp_can.hpp"

#include <cstdint>

namespace robot::application
{
namespace
{

constexpr bsp::can::bus chassis_bus = app::can::chassis;
constexpr std::uint32_t chassis_feedback_id = app::can::chassis_rx_header;
constexpr std::uint32_t chassis_command_id = app::can::chassis_tx_header;

//回调函数
void on_can_frame(bsp::can::bus bus, const bsp::can::rx_frame& frame) noexcept
{
    if (bus == chassis_bus && frame.id == chassis_feedback_id && frame.len == 8U)
    {
        // 只做快速的收帧处理；不要阻塞。
    }
}

types::status init_custom_can() noexcept
{
    const auto callback =
        bsp::can::rx_callback::bind<&on_can_frame>(); //绑定回调函数
    types::status status = bsp::can::register_rx_callback(chassis_bus, callback);
    if (status != types::status::ok)
    {
        return status;
    }

    return bsp::can::init(chassis_bus);
}

types::status send_chassis_command() noexcept
{
    constexpr std::uint8_t command[] = {0x01U, 0x02U, 0x03U};
    return bsp::can::transmit(
        chassis_bus, chassis_command_id, command, sizeof(command));
}

} // namespace
} // namespace robot::application
```

对同一总线再次调用成功过的 `init()` 会直接返回 `ok`，不会重新配置它。CAN Classic / CAN FD 类型由生成配置决定，应用不再传入第二份类型信息。

## 核心类型

### `rx_frame`

接收回调拿到的一帧 CAN 数据。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `id` | `uint32_t` | 实际收到的报文 ID。 |
| `len` | `uint8_t` | `data` 中的有效字节数。 |
| `id_kind` | `id_type` | 这帧使用标准 ID 还是扩展 ID。 |
| `format` | `bus_type` | 这帧是 Classic CAN 还是 CAN FD。 |
| `bit_rate_switch` | `bool` | 这帧 CAN FD 是否使用比特率切换。 |
| `data` | `uint8_t[64]` | 报文数据；Classic CAN 最多使用前 8 字节。 |

`const rx_frame&` 只在本次回调调用期间有效。如需在线程中继续处理，应复制需要的数据，或使用适合 ISR 的交接方式。

### `rx_callback`

| 项目 | 内容 |
| --- | --- |
| 类型 | `core::callback<void(bus, const rx_frame&)>` |
| 第一个参数 | 接收到报文的总线。 |
| 第二个参数 | 本次接收的 `rx_frame`。 |
| 可绑定目标 | 静态函数，或长期存在对象的成员函数。 |
| 调用上下文 | FDCAN HAL 中断路径。 |

回调运行在所选后端的 CAN HAL 中断路径（H7 为 FDCAN，F4 为 bxCAN）。它不能等待、获取 ThreadX mutex、调用可能阻塞的 BSP API，或执行耗时解析。回调目标由调用者持有；BSP 不管理其生命周期。

### `bus`、`bus_type`、`id_type`

| 类型 | 调用者如何使用 |
| --- | --- |
| `bus` | 优先使用 `app::can::chassis` 这类语义总线名；它的值是生成的 `bsp::can::bus`。 |
| `bus_type` | `classic` 或 `fd`。由生成配置决定；接收帧的 `format` 字段也使用这个类型。 |
| `id_type` | `standard` 或 `extended`。它是生成的接收过滤器 ID 类型。 |

此模块没有应用层的运行时 `config` 结构体；过滤器 ID 类型来自生成配置。Classic、FD 以及 FD 是否开启 BRS 由 CubeMX 在 `board.ioc` 中的 `FrameFormat` 决定。接收 FIFO 也是板级设置：BSP 直接使用 IOC 中启用的 FIFO0 或 FIFO1，应用和 JSON 都不需要、也不能选择它。

## 常用接口

| 接口 | 用途与注意事项 |
| --- | --- |
| `init(bus)` | 启动已配置总线。仅在启动或线程中调用；成功返回 `ok`。 |
| `transmit(bus, id, data, len)` | 把一帧加入发送 FIFO。在线程中调用；不等待发送完成，`ok` 不代表对方收到。Classic CAN 最多 8 字节，CAN FD 最多 64 字节。 |
| `register_rx_callback(bus, callback)` | 添加接收回调。应在 `init()` 前注册；回调目标必须长期存在。所有回调都会收到该总线上的每帧数据。 |
| `unregister_rx_callbacks(bus)` | 清空该总线的全部回调。不能与接收中断并发调用。 |
| `restart(bus)` | 在线程中停止并重新启动已初始化总线。 |
| `err_sem(bus)` | 获取 Classic CAN 的部分错误信号量；通常只有自己的错误恢复线程才需要。 |
| `bus_enabled()`、`configured_bus_type()`、`filter_id_type_of()` | 查询生成配置。普通应用通常不需要调用。 |

`receive()` 和 `handle_error()` 是 HAL 回调桥接入口；上层应用不应直接调用。

上方的 `init_custom_can()` 和 `send_chassis_command()` 就是最小使用片段。

## 常见错误

- 在回调里解析完整协议、等待信号量或发送 CAN。回调处于中断路径，应把后续工作交给线程。
- 先启动总线、后注册回调，或运行中清空回调；当前实现不与 IRQ 同步。
- 把局部对象的成员函数注册为回调；回调保存的是非拥有引用。
- 把 `transmit()` 的 `ok` 当作对端已收到；它只表示进入发送 FIFO。

## 相关内容

- [配置](../configuration.md)
- [通用回调](../concepts/interrupt-callback.md)

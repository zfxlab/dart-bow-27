# bsp::usb

`bsp::usb` 提供 USB CDC device

## Header / Namespace

- Header：`bsp_usb.hpp`
- Namespace：`bsp::usb`

## 初始化

先在 `params.json` 中启用 `build.usbx`，再在启动阶段调用一次 `init()`。配置必须至少提供一个 RX 或发送结果回调；回调目标必须长期存在。

USBX 栈与控制器启动由 ThreadX 的 `MX_USBX_Device_Init()` 启动钩子完成，
`bsp::usb::init()` 只创建应用收发线程，不重复初始化 PCD 或启动控制器。
`init()` 返回 `ok` 不表示主机已枚举；`connected()` 表示 CDC 实例存在且
USBX device 已 configured，不额外要求主机打开串口或设置 DTR。
`period_ticks` 必须大于零，RX 数据仅在回调期间有效。

H7/F4 使用相同公共接口和收发实现。私有 USBX 适配的构建选择、描述符和
PCD 边界见 [USB 统一调用链](../usb-v2-integration.md)。

```cpp
// 使用片段：接收 USB 字节流，并一次提交两条应用协议数据
#include "bsp_usb.hpp"

namespace robot::application
{
namespace
{

void on_usb_rx(const std::uint8_t* data, std::uint16_t len) noexcept
{
    // data 是任意长度的字节块，不保证是一条完整协议报文。
    if (len == 0U)
    {
        return;
    }

    // 如果需要发不同的包：两条数据各自带包头和长度
    constexpr std::uint8_t two_packets[] = {
        0xA1U, 0x02U, 0x10U, 0x11U, // 第一包：包头、长度、数据
        0xA2U, 0x01U, 0x20U,        // 第二包：包头、长度、数据
    };
    // on_usb_rx 运行在 USB 读 ThreadX 线程中，因此可以调用 try_send()。
    const types::status status = bsp::usb::try_send(two_packets, sizeof(two_packets));
    if (status == types::status::busy)
    {
        // 待发送槽位已满；把数据交给应用自己的发送队列，稍后重试。
    }
}

void on_usb_tx(const bsp::usb::tx_result& result) noexcept
{
    if (!result.success())
    {
        // 记录发送失败。
    }
}

types::status init_usb() noexcept
{
    bsp::usb::config config{};
    config.on_rx_callback = bsp::usb::rx_callback::bind<&on_usb_rx>();
    config.on_tx_result_callback = core::callback<void(const bsp::usb::tx_result&)>::bind<&on_usb_tx>();
    return bsp::usb::init(config);
}

} // namespace
} // namespace robot::application
```

> **`try_send()`：**它不是立即把数据写到 USB 的阻塞发送接口，而是把数据复制到 BSP 的一个待发送位置，再唤醒 USB 写线程。它只能在 ThreadX 线程中调用，不能在 ISR 中调用；当前一次最多 512 字节，且只有一个待发送槽位。写线程取走这一包后，应用才能接受下一包；若槽位已满，`try_send()` 返回 `busy`。需要连续发送多个包时，应由应用自己的发送队列或发送线程保存数据并重试。返回 `ok` 只表示已接收这次发送请求，最终结果由 `on_tx_result_callback` 给出。

一次提交多条数据不会保留 USB 报文边界：接收端仍可能一次收到两包、或分多次收到一包。因此每条应用协议数据都要有自己的包头、长度等可解析信息。

## 核心类型

| 类型 | 用途 |
| --- | --- |
| `config` | 读、写线程优先级、轮询周期和两个回调。 |
| `tx_result` | 写线程完成一次发送后的请求长度、实际长度和结果。 |
| `runtime_state` | 当前连接、读写忙状态和统计信息。 |

## 常用接口

| 接口 | 用途与注意事项 |
| --- | --- |
| `init(config)` | 创建 USB 读写 ThreadX 线程；首次成功后再调用只返回 `ok`，不会替换回调。 |
| `try_send(data, len)` | 仅能在 ThreadX 线程中调用；会复制数据，当前实现单次最多 512 字节。BSP 没有发送队列，唯一待发送槽位被占用时返回 `busy`。 |
| `connected()` | 查询 USB CDC 是否已连接。 |
| `state()` | 查询运行状态和最近一次发送结果。 |

## 常见错误

- 在中断或 ThreadX 启动前调用 `try_send()`；会返回 `invalid_context`。
- 把 RX 回调的每一块数据当作一条完整报文；CDC 是字节流。
- 以为 `try_send()` 返回 `ok` 表示电脑已收到；实际发送结果在 `on_tx_result_callback` 中给出。

## 相关内容

- [配置](../configuration.md)

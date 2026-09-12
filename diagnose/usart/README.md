# USART 诊断

此诊断在生成的 `app::uart::test_uart` 上以 `bsp::dma::buffer` 启动 receive-to-idle 接收；主机发送固定 `host_packet`，板端校验后回复 `device_packet`。状态在 `demo_debug_instance.usart`。

默认诊断入口已调用 `diagnose::usart::start()`。用调试器确认 `ready`、`rx_count`、`tx_count` 和 `error_count`；主机脚本位于 `../tools/run_usart_demo.py`。

两板在各自 `params.json` 中设置 `bindings.uart_ports.test_uart`：H7 为 `uart7`，F4 为 `usart1`。更换测试串口只需修改该绑定；所选串口需要 IOC 配置 RX/TX DMA。波特率仍由 IOC 管理，当前两板测试串口均为 921600、8N1。

```powershell
python diagnose/tools/run_usart_demo.py --port COM7 --baud 921600
```

它每次验证所选的一路串口，不会遍历所有 UART。接口说明见 [USART API](../../docs/api/bsp-usart.md)。

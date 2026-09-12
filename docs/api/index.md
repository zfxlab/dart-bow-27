# API Reference

这里按层列出可供应用使用的接口。BSP 是直接操作已配置板载外设的一层；通常先在[配置](../configuration.md)中确认外设和引脚。

## BSP

| 模块 | 用途 |
| --- | --- |
| [CAN](bsp-can.md) | 初始化 CAN、发送报文和接收回调。 |
| [ADC](bsp-adc.md) | 读取板上配置的模拟量。 |
| [DMA 缓冲区](bsp-dma.md) | 为 ADC、SPI、USART 的 DMA 传输准备合法缓冲区。 |
| [DWT 时间](bsp-dwt.md) | 获取高精度时间间隔，或做短暂忙等延时。 |
| [EXTI 外部中断](bsp-exti.md) | 为已配置的 GPIO 输入注册中断回调。 |
| [GPIO](bsp-gpio.md) | 读取输入、控制输出。 |
| [PWM](bsp-pwm.md) | 启动 PWM 并设置占空比、周期或脉宽。 |
| [SPI](bsp-spi.md) | 用阻塞、中断或 DMA 方式收发 SPI 数据。 |
| [USART](bsp-usart.md) | 发送串口数据，或启动 receive-to-idle DMA 接收。 |
| [USB CDC](bsp-usb.md) | 使用 USB 虚拟串口收发字节流。 |

## PnX Modules

这些模块组合了底层外设和设备，供机器人应用直接使用。

| 模块 | 用途 |
| --- | --- |
| [AHRS](ahrs.md) | 启动板载 BMI088 姿态解算，或理解外置 DMIMU 的独立输出。 |
| [Referee](referee.md) | 接入裁判系统，读取当前已支持的比赛和机器人状态数据。 |
| [Remoter](remoter.md) | 将遥控器输入映射为机器人自己的 command。 |

## Devices

设备层封装具体硬件协议；它们通常由应用创建或由模块内部使用。

| 设备 | 用途 |
| --- | --- |
| [Motors](motors.md) | 创建已配置电机，发送控制命令并读取反馈。 |

## Libs

| 内容 | 用途 |
| --- | --- |
| [PnX Libs](libs.md) | 查找基础工具库的 namespace 和入口头文件。 |

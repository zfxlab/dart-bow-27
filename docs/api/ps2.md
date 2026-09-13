# PS2 手柄

ps2手柄的spi版本有自带的MODE选择，目前直接屏蔽一种按键方式，通过mode切换（也就是如果遥控器没反应，按MODE后再试试）

先在 CubeMX 配置引脚，再在 `configs/boards/<board>/params.json` 选择同步 PS2 作为来源和底层通信方式：

```json
{
  "remoter": {
    "source": "ps2"
  },
  "ps2": {
    "enabled": true,
    "backend": "gpio",
    "cmd": "pb5",
    "data": "pb4",
    "clk": "pb3",
    "cs": "pb2"
  }
}
```

GPIO backend 用于SPI引脚不够的情况，使用GPIO模拟SPI通信 CubeMX 配置如下：CMD、CLK、CS/ATT 是 `GPIO_Output`、Push-Pull、No pull；其中 CLK 与 CS 初始为 High，CMD 初始为 High。DATA 是 `GPIO_Input`、No pull。

硬件 SPI 示例：

```json
{
  "remoter": {
    "source": "ps2"
  },
  "ps2": {
    "enabled": true,
    "backend": "spi",
    "spi": "spi3",
    "cs": "pb2"
  }
}
```

SPI模式需配置为**Master, 2-line, Mode 3, LSB first**. 
CMD —— MOSI, DATA —— MISO, CLK —— SCK, CS/ATT 接GPIO output
Remoter 源每 10 ticks 轮询一次。

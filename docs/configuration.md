# Configuration

## 板级事实与机器人选择（V2）

`boards/<board>/board.json` 只描述开发板固定接线、板载器件绑定和静态内存事实。同一块 PCB 更换机器人时不应修改它。IOC 仍拥有引脚复用、外设实例、时钟、DMA 和中断配置。

板载 BMI088、SPI LED 的 `board.json.devices` 条目只保留固定绑定，不再有 `enabled`。本机器人是否使用它们在 `configs/boards/<board>/robot.json` 中选择：

```json
{
  "devices": {
    "bmi088": { "enabled": true },
    "led": { "enabled": true }
  }
}
```

省略或设为 `false` 表示不启用对应消费者；固定板级绑定仍保留。启用没有板级绑定的器件会在生成阶段报错。F4 当前未描述 BMI088 的固定绑定，不能把这一点理解为 PCB 上一定没有 BMI088。

`params.json` 保存运行参数、应用使用哪个外接接口及诊断选择；`robot.json` 保存设备组成和协议地址。外接设备换接口属于应用接线选择，不应反写板载固定接线。`configs/boards/<board>/` 是板型对应的默认应用配置位置，不表示其中内容是 PCB 固有属性。

`test.motor_demo` 默认关闭，仅启用现有 `motor1/motor2/motor3` 专用诊断例程；H7 示例配置显式开启。它不控制电机驱动是否编译，也不替代应用创建设备和注册服务。任意电机名称和数量应由自己的应用使用生成的 `robot::motors` 配置。

切板使用不同构建目录和对应 BSP 分支：H7 为 `refactor/V2-h7`，F4 为 `refactor/V2-f4`。`PNX_BOARD` 不会自动 checkout 子仓库。开发时可用 `PNX_BSP_SOURCE_DIR` 指向另一工作树；MCU 家族不匹配会在配置时失败。

pnx_template中已经写好了大部分常用的 .json 配置，
**更具体的功能见[配置参考](configuration-reference.md)。**


配置顺序：
1. STM32CubeMX
2. 通过 `PNX_BOARD` 或对应 CMake preset 选择 `boards/<board>/`
3. 修改 `configs/boards/<board>/` 中的应用配置与设备树
- 不要修改 `build/<preset>/generated/` 中的生成文件

## `boards/<board>/<board>.ioc`：工程文件

使用 STM32CubeMX 修改

## `boards/<board>/`
当前选中的 profile 会通过 `boards/<board>/board.json` 为工程配置开发板的**固定配置**，**大部分情况下不会修改**：
例如 BMI088 使用的 SPI、CS 与 DRDY 引脚，以及需要超时归零保护的 PWM 角色与
其独立 failsafe timer。PWM 和 failsafe timer 的周期、模式及 DMA 配置仍由 IOC 管理。
这些配置对于同一块开发板（mc02或C板）通用，特殊情况下可以配合`.ioc`进行修改：例如需要的引脚被占用，需修改常用配置才能满足开发需求。

## `configs/boards/<board>/params.json`：绑定外设与构建选择

这里放置和**MCU外设相关的配置与构建功能选择**：是否需要USB、遥控器类型、使用哪个串口、遥控器类型、AHRS等service线程优先级和运行参数。

例如，使用 DR16 时需要的配置：

```json
{
  "bindings": {
    "remoter_uart": "uart5",
    "vt03_uart": "usart1",
    "uart_ports": { "host_link": "uart7" }, 
    "spi_buses": { "custom_sensor": "spi6" }
  },
  "remoter": { "source": "dr16" }
}
```

`bindings.spi_buses` 为应用**自定义SPI取名**。上例生成
`app::spi::custom_sensor`，在上层应用中不应出现spi6具体spi实例

`bindings.uart_ports` 同样为**自定义串口取名**，上例生成
`app::uart::host_link`，在上层应用中不应出现uart7具体串口实例

## `configs/boards/<board>/robot.json`：机器人设备

描述当前机器人连接的"device"层设备：例如每台电机的具体配置、DMIMU

```json
{
  "devices": {
    "motors": {
      "list": [
        {
          "name": "motor1",
          "model": "dji_gm6020",
          "can_bus": "fdcan2",
          "can_type": "classic",
          "can_id": "0x205"
        }
      ]
    }
  }
}
```


**更多配置片段见[配置参考](configuration-reference.md)。**


# 生成结果和生效步骤

顶层 `CMakeLists.txt` 在 **CMake configure** 阶段运行 `configs/cmake/generate_config.cmake`。它会读取以上配置文件并生成：


| 文件 | 内容 |
| --- | --- |
| `build/<preset>/generated/config.hpp` | 外设枚举、板级绑定、功能开关和 `params::` 常量 |
| `build/<preset>/generated/robot_config.hpp` | 当前电机和 DMIMU 的 C++ 配置常量 |
| `build/<preset>/generated/bsp_bindings.cpp` | 从 IOC 得到的 ADC、PWM 与 HAL 句柄绑定 |


# 注意事项

配置只能**生成常量**或**编译进驱动**，具体任务需要在上层逻辑中**初始化**并**调用**，例如：

- `devices.motors.list` 中的每台电机都会生成配置并决定要编译哪些电机协议，电机仍然需要进行register等初始化行为

#TODO：`test.thread_priority` 和 `test.auto_run_on_boot` 当前会生成到 `config.hpp`，但仓库内没有运行代码读取它们；修改这两个字段目前不会改变运行行为。

`test.gpio_leds=true` 仅用于启用板级 RGB GPIO 诊断。它要求 `board.json`
提供 `led_r`、`led_g`、`led_b` 三个逻辑输出角色；F407 profile 将它们映射到
PH12、PH11、PH10。该字段不改变 IOC 的引脚配置。

# 快速查找

| 我想修改 | 应该修改 |
| --- | --- |
| 板载 BMI088、LED 的固定连接 | 当前板卡 profile 的 `boards/<board>/board.json`，并确认与 IOC 一致 |
| 是否构建 USBX、遥控器类型与 UART、服务运行参数 | `configs/boards/<board>/params.json` |
| 电机型号、CAN 总线、CAN ID、初始模式 | `configs/boards/<board>/robot.json` 的 `devices.motors` |
| 查看生成后的 C++ 名称和当前结果 | `build/<preset>/generated/config.hpp`、`robot_config.hpp`；只读，不修改 |

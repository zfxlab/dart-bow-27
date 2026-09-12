# 配置参考

 `configs/boards/<board>/params.json` 和 `robot.json` 是当前板型的详细配置，完整步骤见[配置](configuration.md)。

## `params.json`：

### `build` ：是否编译

| 字段 | 示例 | 作用 |
| --- | --- | --- |
| `build.usbx` | `true` |编译 USBX 与 USB CDC BSP |

```json
{
  "build": { "usbx": true }
}
```

### `bindings`：给应用使用的外设取名字

绑定生成 `app::` 下的 C++ 常量，应用层直接使用自定义的名字，不应该再出现具体的"usart1/fdcan1/tim3_ch4"等可能散落在各处的外设实例

```json
{
  "bindings": {
    "remoter_uart": "uart5",
    "vt03_uart": "usart1",
    "referee_uart": "usart1",
    "uart_ports": {
      "host_link": "uart7"
    },
    "spi_buses": {
      "custom_sensor": "spi2"
    },
    "adc_channels": {
      "battery_voltage": { "adc": "adc1", "channel": 1 }
    },
    "can_buses": {
      "chassis": {
        "bus": "fdcan2",
        "rx_header": "0x201",
        "tx_header": "0x200"
      },
      "supercap": "fdcan1"
    }
  }
}
```

| 字段 | 生成结果 | 注意 |
| --- | --- | --- |
| `remoter_uart` | `app::uart::dr16`、`app::uart::ps2_uart` | DR16 与串口 PS2 接收器使用它。 |
| `vt03_uart` | `app::uart::vt03` | VT03 使用它；当 `remoter.source=vt03` 时必须是 IOC 中带 RX DMA 的 UART。 |
| `referee_uart` | `app::uart::referee` | Referee 服务使用它。 |
| `uart_ports.<角色>` | `app::uart::<角色>` | 自定义应用串口角色。角色必须是合法 C++ 名称，且不能与内置角色重名。 |
| `spi_buses.<角色>` | `app::spi::<角色>` | 自定义 SPI 总线角色。 |
| `adc_channels.<角色>` | `app::adc::<角色>` | 目标必须是 IOC 中的单通道常规 ADC 转换。 |
| `can_buses.<角色>` | `app::can::<角色>` | 可以只写总线字符串，也可写对象并附带可选 `rx_header`、`tx_header`。后两项会生成同名 `_rx_header`、`_tx_header` 常量。 |


### CAN 与诊断

```json
{
  "can": {
    "fdcan1": { "id_type": "standard" },
    "fdcan2": { "id_type": "extended" }
  },
  "can_diag": {
    "enabled": true,
    "sample_period_ms": 1000,
    "window_size": 60
  }
}
```

1. `can.<FDCAN>.id_type` : 
   1. `standard` / `std` 标准帧
   2. `extended` / `ext` 拓展帧
   3. FD or Classic 直接由.ioc决定，不需要在这配置
2. `can_diag` = `true` 会创建诊断采样定时器，可以统计各类CAN错误，方便debug。
   1. 配置一般用默认的就行。`window_size` 必须在 1 到 3600。

### AHRS、DMIMU、遥控器与 Referee

```json
{
  "ahrs": { //一般不需要改
    "imu_offset_x": 0.0,
    "imu_thread_priority": 3,
    "temp_thread_priority": 4,
    "target_temp": 45.0
  },
  "dmimu": { //一般不需要改
    "communication_mode": "active",
    "offline_timeout_ticks": 100,
    "thread_priority": 3,
    "receive_wait_ticks": 1,
    "request_period_ticks": 1
  },
  "remoter": {
    "source": "ps2",
    "thread_priority": 2,
    "rx_timeout_ticks": 100,
    "offline_timeout_ticks": 120,
  },
  "referee": {
    "thread_priority": 8 
  }
}
```

| 分组 | 关键字段 | 用处 |
| --- | --- | --- |
| `remoter` | `source`、线程优先级、超时、串口 PS2 死区 | **`source` 只能是 `dr16`、`vt03`、`ps2`、`ps2_uart` 或 `none`**；`ps2` 的引脚和 backend 在顶层 `ps2` 配置。|
| `referee` | `thread_priority` | 生成线程优先级|
| `ahrs` | `imu_offset_x`、两个线程优先级、`target_temp` | 生成 `params::ahrs`，作为 AHRS 默认配置。 |
| `dmimu` | `communication_mode`、离线超时、线程优先级、接收等待、请求周期 | 生成 `params::dmimu`。模式只能是 `active` 或 `request`；超时与接收等待必须大于 0，`request` 模式的请求周期也必须大于 0。 |


### USB 与测试参数

```json
{
  "usb": { //一般不需要改
    "read_thread_priority": 5,
    "write_thread_priority": 5,
    "period_ticks": 2
  },
}
```

- `usb` 三项生成 `params::usb`，是 `bsp::usb::config` 的默认收发线程参数。


## `robot.json`：当前机器人安装的设备

### 配置电机

`devices.motors.list` 中每一项就是一台电机。配置生成器据此选择要编译的协议，并生成 `robot::motors::<name>` 和 `robot::motors::<name>_model`。电机仍需在应用中创建、注册到 `motor_service` 并启动，详见[Motors](api/motors.md)。

```json
{
  "devices": {
    "motors": {
      "dm": {
        "id_base": "0x01",
        "master_id_base": "0x05",
        "max_motors": 4
      },
      "list": [
        {
          "name": "chassis_left",
          "model": "dji_gm6020",
          "can_bus": "fdcan2",
          "can_type": "classic",
          "can_id": "0x205",
          "control_mode": "relax"
        },
        {
          "name": "joint",
          "model": "dm_dm4310",
          "can_bus": "fdcan1",
          "can_type": "fd",
          "can_id": "0x01",
          "control_mode": "mit"
        }
      ]
    }
  }
}
```

| 字段 | 要求 |
| --- | --- |
| `name` | 必填。用于生成 C++ 名称，例如 `chassis_left`；应使用合法 C++ 标识符。 |
| `model` | 当前支持：`dji_m2006`、`dji_m3508`、`dji_gm6020`、`dji_xroll`、`dm_dm4310`、`dm_dm8009p`、`lk_lk8016`、`lk_lk9025`。 |
| `can_bus` | 必填，且必须存在于 `board.ioc`。 |
| `can_type` | 必填，`classic` 或 `fd`；必须与板卡该总线的实际能力一致。 |
| `can_id` | 必填，必须落在该总线配置的标准 / 扩展 ID 范围内。 |
| `control_mode` | 可选，省略时为 `relax`。可写 `relax`、`current`、`torque`、`mit`、`pos_speed` / `position_speed`、`speed` / `velocity`、`multi`。实际型号是否支持该模式，仍应在应用中用 `supports()` 判断。 |
| `motors.dm` | DM 协议的 ID 基值、主机 ID 基值和最大电机数；生成 `robot::motors::dm::*`。当前 DM handler 用它们匹配配置和反馈。 |

### 配置外置 DMIMU

```json
{
  "devices": {
    "dmimu": {
      "enabled": true,
      "can_bus": "fdcan3",
      "can_type": "classic",
      "can_id": "0x04",
      "master_id": "0x04"
    }
  }
}
```

`enabled: true` 才会生成 `HAS_DMIMU` 和 `robot::imu::dmimu`。其余四项在启用时都必填：

- `can_bus` 必须存在于 IOC；
- `can_type` 当前只能是 `classic`，并且必须与板卡总线配置一致；
- `can_id`、`master_id` 都必须在 `0x00` 到 `0xFF`；
- DMIMU 服务的通信模式、超时和线程参数仍写在 `params.json` 的 `dmimu` 分组。

## 修改后如何确认

重新执行：

```powershell
cmake --preset h723-debug
cmake --build --preset h723-debug
```

Configure 阶段会检查名称、IOC 外设、CAN 类型和 ID 范围。成功后可只读查看：

- `build/<preset>/generated/config.hpp`：`params.json` 的生成结果；
- `build/<preset>/generated/robot_config.hpp`：`robot.json` 的生成结果。

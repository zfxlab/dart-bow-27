# 项目结构

| 目录 | 职责 |
| --- | --- | 
| `boards/<board>/` | 每块板完整的 STM32CubeMX/HAL/ThreadX/USBX 工程、IOC、启动文件、链接脚本、工具链和 `board.json`。 |
| `configs/boards/<board>/` | 对应板型的应用参数与机器人设备配置。 |
| `configs/cmake/` | 读取所选板型 IOC/JSON 并生成构建期绑定的公共生成器。 |
| `pnx_bsp/` | (Submodule) 对 HAL 外设的小型封装 |
| `pnx_devices/` | (Submodule) 电机、IMU、LED、UI 等基于 BSP 的具体设备与接口 | 
| `pnx_modules/` | 	(Submodule) AHRS、遥控器、裁判系统等服务线程 |
| `pnx_libs/` |(Submodule) msg、CRC、控制、滤波等通用库 | 
| `diagnose/` | 用于测试的单元 | 

四个 submodule 都由顶层 CMake 直接编译。

## 分层关系

```mermaid
flowchart TB
    subgraph Config[配置与生成阶段]
        IOC[boards/board-name/board-name.ioc\nCubeMX peripheral]
        BoardProfile[boards/board-name/\n板卡 profile、CubeMX 与 board.json]
        RobotConfig[configs/boards/board-name/\nparams.json 与 robot.json]
        Generator[configs/cmake/\n配置生成器]
        Generated[build/preset/generated/\n只读 C++ 绑定]

        IOC --> Generator
        BoardProfile --> Generator
        RobotConfig --> Generator
        Generator --> Generated
    end

    subgraph Runtime[ ]
        App[Application\ndiagnose/ 或机器人应用]
        Modules[pnx_modules/\nAHRS、remoter\referee...]
        Devices[pnx_devices/\nmotor、IMU、LED、UI]
        BSP[pnx_bsp/\nCAN、UART、SPI、DMA、PWM...]
        Board[boards/board-name/\nCubeMX、HAL、ThreadX、USBX、IRQ]

        App --> Modules
        Modules --> Devices
        Devices --> BSP
        BSP --> Board
    end

    Generated --> App
    Generated --> Modules
    Generated --> Devices
    Generated --> BSP

    Libs[pnx_libs/\n消息、数学、滤波、运行时间测量]
    App -. 共享工具 .-> Libs
    Modules -. 共享工具 .-> Libs
    Devices -. 共享工具 .-> Libs
    BSP -. 共享工具 .-> Libs

    App -. 当前诊断可直接使用设备 .-> Devices
    Modules -. 当前 AHRS 直接依赖 BMI088 .-> Devices
```

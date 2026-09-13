# PnX_template

PnX_template 是一个基于 STM32 和 ThreadX 的嵌入式下位机模板，整合了板级外设、常用设备和可复用的基础库。

文档最后更新：2026-09-13

## 当前架构与构建入口

H723 与 F407 共用同一套 Template、Lib、Device 和 Module，但使用不同的 BSP 分支，并在构建时通过不同 CMake preset 选择板型。`PNX_BOARD` 只选择 `boards/<board>/`，**不会自动切换 `pnx_bsp` 的 Git 分支**；分支与板型不匹配时 CMake 会拒绝配置。

=== "H723 Debug"

    ```powershell
    git -C pnx_bsp switch stm32h7
    cmake --preset h723-debug
    ```

=== "F407 Debug"

    ```powershell
    git -C pnx_bsp switch stm32f4
    cmake --preset f407-debug
    ```

切换 BSP 前先妥善处理该子模块中的本地修改；两个 preset 使用独立构建目录。需要同时保留两套 BSP 时，可用 `PNX_BSP_SOURCE_DIR` 指向匹配分支的另一工作树。

架构边界、公共接口要求和 CAN/DMA 等差异见 [H7/F4 统一架构](concepts/h7-f4-unification.md)。

## 建议阅读顺序

1. [项目结构](project-structure.md)：架构思想与结构
2. [H7/F4 统一架构](concepts/h7-f4-unification.md)：两板如何共享上层以及必须保留的差异
3. [配置](configuration.md)：config 是什么，怎么用
4. [启动流程](startup.md)：程序的启动顺序，应用代码应该从哪里开始运行
5. [创建应用线程](thread.md)：从 `app_start()` 创建自己的控制线程
6. [API Reference](api/index.md)：按需要选择模块，参考真实接口

## 按功能查找

- [使用 Motor](api/index.md)：电机的注册、控制和状态读取
- [使用 IMU 或 AHRS](api/index.md)：姿态数据的获取和 AHRS Service
- [使用 Remoter](api/index.md)：遥控器数据接收和使用
- [使用 CAN、USART等外设](api/index.md)：通信外设的初始化和收发接口
- [使用 DMA 缓冲区](api/bsp-dma.md)：为需要 DMA 收发的 ADC、SPI 或 USART 准备合法的长期缓冲区
- [使用 USBX](api/index.md): 如何收发和视觉通信的USB数据
- [查询 API](api/index.md)：按 BSP、设备、模块和通用库查找公开接口

## Concepts

- [H7/F4 统一架构](concepts/h7-f4-unification.md)：公共 BSP 契约、开发要求，以及 DMA、CAN、EXTI、PWM 和 USB 差异
- [通用回调](concepts/interrupt-callback.md)：了解为什么要用回调，模块怎样把事件交给上层，以及回调中的代码约束

## 给 Agent 的资料

[给 Agent 的项目资料](agent-context.md)：在没有本地 `AGENT.md` 时，提供项目结构、配置、启动、API 与诊断资料的阅读顺序。

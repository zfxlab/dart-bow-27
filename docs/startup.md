# 启动流程

应用代码从 `app_start()` 开始

## 实际顺序

```text
复位
  -> main()
  -> HAL / CubeMX 外设初始化
  -> MX_ThreadX_Init()
  -> tx_kernel_enter()
  -> App_ThreadX_Init()
  -> app_start()
```

`App_ThreadX_Init()` 与 `MX_ThreadX_Init()` 由所选板的 `Core/Src/app_threadx.c` 实现并参与编译。两板在 USER CODE 区调用 `app/app.cpp` 的 `app_start()`；CubeMX 外设初始化和 `tx_application_define` 保持不变。

当前 `app_start()` 在 `params.dart.enabled=true` 时调用 `dart::start()`；未选择飞镖应用时，仍仅在 `test.auto_run_on_boot=true` 时调用 `diagnose_start()`。机器人业务在 `app/` 中组织；不要把永久业务循环放入 ThreadX 初始化回调。

飞镖的启动线程依次初始化应用消息、传感器、上位机、视觉、控制、发射机构、电机和可选 LED 监控。`dart::startup.stage=9` 表示线程创建完成；电机使能在线程中执行，结果另看 `dart::motor::debug`。文件分工、各线程和配置入口见[飞镖迁移说明](dart-migration.md)。

## 从哪里开始写

在 `app_start()` 中只做启动工作，例如创建应用线程、初始化需要的模块：

```cpp
extern "C" void app_start()
{
    robot::application::start();
}
```

耗时循环、周期控制和持续运行的逻辑应放在线程中，不要让 `app_start()` 自己阻塞。

各模块的 `init()` 会按自身实现创建所需资源和后台线程；应用只需按 API 要求初始化并保存仍在使用的对象。

## 修改启动入口时注意

- 不要在 `main()` 或 `app_threadx.c` 里加入机器人业务逻辑。
- 不要在 ThreadX 启动前使用依赖 HAL 外设的模块。
- 回调、线程控制块、线程栈以及仍被模块使用的配置对象，必须保持有效的生命周期。
- 修改 `boards/<board>/<board>.ioc` 后，先重新生成该板 CubeMX 代码，再执行 CMake configure 和 build。

相关内容：[`项目结构`](project-structure.md)、[`配置`](configuration.md)、[`创建应用线程`](thread.md)。

Current authoritative startup ownership and CMake review: [architecture review](cmake-architecture-review.md). ThreadX entries reside in board Core/Src/app_threadx.c; app/app.cpp owns app_start only.

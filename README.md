# PnX_template

PnX_template 是运行在 STM32 和 ThreadX 上的机器人模板：CubeMX 管硬件，JSON 管项目配置，BSP、Device 和 Module 提供可复用能力，`diagnose/` 用于板端检测。

H723 与 F407 共用 Template、Lib、Device 和 Module，但必须选择匹配的 `pnx_bsp` 分支和 CMake preset。`PNX_BOARD` 不会自动切换子模块分支。

H723 Debug：

```powershell
git -C pnx_bsp switch stm32h7
cmake --preset h723-debug
cmake --build --preset h723-debug --parallel 1
```

F407 Debug：

```powershell
git -C pnx_bsp switch stm32f4
cmake --preset f407-debug
cmake --build --preset f407-debug --parallel 1
```

Release 对应 `h723-release` 与 `f407-release`。切换 BSP 前应先处理子模块内的本地修改；板型与 BSP family 不匹配时 CMake 会拒绝配置。

完整说明见[文档首页](docs/index.md)和 [H7/F4 统一架构](docs/concepts/h7-f4-unification.md)。当前固件入口是 [app/app.cpp](app/app.cpp) 的 `app_start()`；开始编写机器人应用时从这里接入。

当前 H723 配置启用从 `oldframe` 迁移的飞镖应用（`params.dart.enabled=true`）。
配置、代码分类与 Watch 入口见[飞镖迁移说明](docs/dart-migration.md)，
验证范围和待完成事项见[迁移交接](docs/dart-handoff.md)。F407 不选择该应用。

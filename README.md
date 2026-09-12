# PnX_template

PnX_template 是运行在 STM32 和 ThreadX 上的机器人模板：CubeMX 管硬件，JSON 管项目配置，BSP / Device / Module 提供可复用能力，`diagnose/` 用于板端检测。

使用入口在 [docs/index.md](docs/index.md)。修改配置后执行：

```powershell
cmake --preset h723-debug
cmake --build --preset h723-debug --parallel 1
```

F407 使用 `f407-debug`；两个 preset 都只通过 `PNX_BOARD` 选择板型，构建前需让
`pnx_bsp` checkout 位于匹配的 V2 branch。

当前固件入口是 [diagnose/app.cpp](diagnose/app.cpp) 的 `app_start()`；开始写机器人应用时，在这里替换为自己的应用入口。

仓库结构、配置生成规则和面向 Agent 的阅读顺序见 [docs/agent-context.md](docs/agent-context.md)。

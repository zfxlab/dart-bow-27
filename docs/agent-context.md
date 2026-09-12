# Agent Context

This page is a short project index for coding agents. It does not replace the task request. Before changing code or documentation, inspect the current implementation that owns the behavior.

## Read first

1. `docs/project-structure.md` — directory responsibilities and dependency direction.
2. `docs/configuration.md` and `docs/configuration-reference.md` — editable configuration and generated outputs.
3. `docs/startup.md` — the real `app_start()` / ThreadX startup path.
4. `docs/api/index.md` — user-facing API pages and usage examples.

## Where to look by task

| Task | Start here |
| --- | --- |
| Board, peripheral, CAN, or generator changes | `boards/<board>/<board>.ioc`, `boards/<board>/board.json`, `configs/cmake/`, configuration docs |
| Robot device configuration | `configs/boards/<board>/robot.json`, generated `robot_config.hpp`, the matching Device API and implementation |
| Communication or callbacks | `docs/concepts/interrupt-callback.md`, the matching BSP/Module API, then its public header and implementation |
| Board diagnostics | `diagnose/README.md`, the target diagnostic directory, then its implementation |
| CAN diagnostics | `pnx_bsp/can/README.md`, `pnx_bsp/can/src/bsp_can.cpp`, and `bsp_can_diag.cpp` |

## Project rules

- Treat current source behavior as authoritative. Do not infer API behavior from filenames or old docs.
- `build/<preset>/generated/` is CMake output. Never edit it by hand.
- After changing `boards/<board>/<board>.ioc`, regenerate that board's CubeMX code, then run CMake configure and build.
- After changing JSON configuration, rerun CMake configure so generated headers are refreshed.
- Keep examples focused on framework users; avoid private helpers and implementation detail.
- `diagnose/` is the current default application entry. Replace `app_start()` when starting robot application code; do not carry diagnostic test commands into the control program.
- Verify initialization order, callback context, blocking behavior, ownership, and object lifetime from the implementation before documenting or changing them.

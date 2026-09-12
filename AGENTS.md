# PNX BSP H7/F4 V2 Refactor

This file records the current architecture, decisions, migration state, and
validation status for the H7/F4 V2 work. Keep it current; do not use it as a
chronological log.

## Architecture constraints

- STM32H7 is the structural and public-contract baseline unless a real MCU
  difference prevents it.
- H7 and F4 backends are selected at build time. Do not add BSP-wide virtual
  interfaces, factories, registries, service locators, or runtime MCU
  selection.
- Keep the BSP API small and direct. Prefer namespace functions and the
  existing simple types.
- Do not copy F4 telemetry, recovery state machines, generation counters,
  rollback logic, guarded operations, or lifecycle machinery into the shared
  contract without a demonstrated current requirement.
- Do not hard-code board pins, logical-device peripheral choices, HAL handles,
  DMA address ranges, or cache-line size in general BSP logic.
- `board.json` owns logical board bindings and static memory facts that the IOC
  cannot naturally express. It is not a protocol or BSP-behaviour language.
- `board.json.devices` declares fixed onboard wiring, without `enabled` flags.
  `robot.json.devices.bmi088.enabled` and `.led.enabled` select consumers;
  missing selections mean disabled. An absent board entry means no described
  binding, not proof that the PCB physically lacks that component.
- BSP `src` include directories are private to BSP translation units and the
  generated binding source. Shared consumers receive public include paths only.
- CubeMX `.ioc` and generated Board code own peripheral instances, pins,
  alternate functions, clocks, DMA, NVIC, CAN/FDCAN mode, and USB/ADC/timer
  configuration.
- `params.json` and `robot.json` own application/module parameters and robot
  device composition. Do not duplicate IOC hardware configuration there.
- Generated configuration must be treated as read-only build output. V2 will
  generate into the build directory rather than the source tree.
- Shared public BSP headers must not expose STM32 HAL handle types or
  MCU-family symbols. HAL callback bridges and generated handle bindings are
  private implementation details.
- Preserve necessary argument, generated-binding, DMA-memory, and HAL-result
  checks. Do not layer repeated validation or speculative recovery on top.
- Never edit CubeMX-generated source by hand to simulate a missing IOC
  resource. Update/regenerate the IOC ownership first.

## Repository and branch mapping

| Repository | V2 branch | Purpose |
| --- | --- | --- |
| `pnx_template` | `refactor/V2` | Shared parent, board selection, generator, build and documentation |
| `pnx_bsp` H7 | `refactor/V2-h7` | H7 direct implementation |
| `pnx_bsp` F4 | `refactor/V2-f4` | F4 direct implementation, based on `origin/F4_version_bsp` |
| `pnx_devices` | `main` unless a proven contract change is required | Shared Device layer |
| `pnx_modules` | `main` unless a proven contract change is required | Shared Module layer; current main already contains measured F407 stack corrections |
| `pnx_libs` | `main` unless a proven contract change is required | Shared callbacks, status, DMA attributes and utilities |

There is no separate `pnx_frame` repository in this workspace or in V2 scope.
Do not change the branch naming scheme midway through the refactor.

## Target build and board flow

```text
PNX_BOARD=<profile>
        +
matching pnx_bsp V2 branch
        +
CubeMX IOC + board.json + params.json + robot.json
        -> build-local generated config and private bindings
        -> shared public BSP contract
        -> H7 or F4 implementation
        -> CMake build
```

- `pnx_template/refactor/V2` will contain/select both H723 and F407 profiles.
- `PNX_BOARD` is the only normal user-facing board selection. Each
  `boards/<board>/` profile contains its IOC, CubeMX Board tree, linker/toolchain
  files, `board.cmake`, and `board.json`; its application configuration lives
  under the matching `configs/boards/<board>/` directory.
- `PNX_BSP_SOURCE_DIR` is an advanced development override for validating a
  second BSP worktree. Normal builds use `pnx_bsp/`, and configure fails when
  its branch family manifest does not match the selected board.
- Validation/demo image selectors must not become the product subsystem
  composition architecture.
- Each selected board's Core/Src/app_threadx.c owns App_ThreadX_Init and
  MX_ThreadX_Init. Its USER CODE block calls app_start from app/app.cpp.
  Parent CMake retains the CubeMX ThreadX template and tx_application_define.
  `test.motor_demo` selects only the existing three-motor diagnostic recipe,
  independently of the motor drivers selected by `robot.json`.

## Contract decisions

### Common rules

- Use generated logical identifiers in applications, Devices, and Modules.
- Put HAL handles and reverse handle lookup behind private bindings.
- Keep common status meanings and fail with `not_configured` when a selected
  logical resource is absent. Do not report success for absent hardware.
- Keep callback targets non-owning and document their execution context.

### Module contracts

| Module | V2 decision |
| --- | --- |
| DWT | Keep the already matching H7/F4 public contract. |
| GPIO | Use the H7 generated input/output and active-level contract. |
| EXTI | Use the H7 typed GPIO-input plus `core::callback` contract; raw HAL dispatch stays private. |
| DMA | Use H7 `buffer`/`buffer_view` and generated memory facts. H7 performs cache maintenance; F4 cache maintenance is a no-op, while F4 still rejects CCM/non-DMA memory. |
| PWM | Use H7 `init/start/stop/set_duty/set_period_us/set_pulse_width_us` semantics and generated channel binding. Timer period remains shared by all channels on that timer. An optional generated `pwm_failsafe` binding may make any PWM channel fail closed through a separate timer/update-DMA path; BSP code must not identify the channel by device semantics. |
| SPI | Use the H7 bus contract, including blocking full-duplex and configured IT/DMA operations. Device chip select is generated GPIO, not a SPI-specific board enum. |
| USART | Use the H7 `core::callback`, `bsp::dma::buffer_view`, line configuration, transmit and receive-to-idle contract. Do not migrate F4 delivery modes or telemetry into common API. |
| CAN | Use the H7 common `init(bus)`, `restart`, `transmit`, `rx_frame`, and `core::callback` contract. F4 implements Classic bxCAN internally and reports Classic frame fields; no runtime backend/capability framework. |
| ADC | Use the H7 contract when the selected IOC contains ADC channels. A board with no ADC configuration does not select an ADC implementation. |
| USB | Include USB in V2 and use the simpler H7 callback/`try_send` contract. Keep only F4-internal lifecycle handling required by its USBX/HAL implementation. |
| Flash | Deferred until a safe, board-neutral data partition and erase/program contract are explicitly chosen. Do not expose firmware Flash as writable merely to make headers match. |

H7 CAN diagnostics, H7 heater lease, and F4 indicator/fault diagnostics are
branch-specific implementation/extensions unless a shared consumer proves a
common need.

## Board configuration boundary

Allowed in `board.json`:

- logical device or role to configured peripheral/channel/GPIO mapping;
- logical PWM role to a separately configured failsafe timer; the IOC still
  owns that timer's period, DMA stream/channel/request and mode;
- active level and other static board wiring facts;
- cache-line size and DMA-accessible memory ranges;
- a small number of static board facts not naturally represented by IOC.

Forbidden in `board.json`:

- bxCAN versus FDCAN or HAL type names;
- standard/extended frame policy, FD/BRS policy, FIFO/filter strategy;
- blocking/IT/DMA transfer policy;
- USB queue depth, recovery policy, telemetry or state-machine settings;
- duplicate clock, pin alternate-function, DMA stream, or NVIC configuration.

## Known MCU and board differences

- H723 uses Cortex-M7, FDCAN, D-cache, DMA-inaccessible TCM and H7 memory
  domains. F407 uses Cortex-M4, bxCAN, no D-cache coherency requirement, and
  DMA-inaccessible CCM.
- H7 currently binds the BMI088 heater PWM role to the generic `pwm_failsafe`
  implementation using TIM6 update DMA. The BSP implementation has no BMI088
  or heater dependency. F4 has the same implementation structure and private
  binding contract. The current F4 IOC selects PWM channels on TIM1/TIM4/TIM8;
  its board profile does not yet declare a logical PWM/failsafe consumer.
- F4 Flash erase sectors and programming granularity differ from H7. This is
  why Flash remains deferred.
- H7 and F4 use different USB peripheral/version details; those remain private.
- The authoritative F4 IOC is
  `boards/f407_c_board/f407_c_board.ioc`. Its hardware configuration is
  migrated from `RM26_F4.ioc` (20 IPs and 50 pins, including SPI1, its DMA,
  timers, I2C3 and USART2). The current generated Board tree contains these
  peripherals and passes the 2026-09-11 full-image build matrix. This supersedes
  the earlier stale/minimal Board-tree assessment.

## Current migration state

GPIO diagnosis now consumes app::gpio::test_gpio, selected by
params.bindings.gpio_outputs.test_gpio from fixed board output roles.
The legacy test.gpio_leds switch is retained; it no longer requires three
LED roles. The output alternates active/inactive every 500 ms. GUI provides
an add-binding search action. Both board builds and missing-alias checks
pass; editor tests pass. No hardware test was performed.

The pnx_devices/led implementation is the SPI WS2812 consumer, not a common
GPIO/SPI LED backend. Parent CMake excludes its entire source directory when
HAS_LED is false. Its functions contain no HAS_LED preprocessor branches,
and the unused generated C++ macro has been removed. F4 indicator LEDs use
configured GPIO roles directly through diagnose/gpio; no shared LED adapter
or MCU-selection macros are introduced. Current application sources do not
call the SPI LED API, so no additional call-site guards are needed.

Configuration defaults now have one maintained source: configs/defaults.json.
CMake merges common, board and user values by leaf. The editor bundles a
build-time snapshot from that file; schema no longer duplicates defaults.
Inactive modules ignore dormant tuning parameters without rewriting user JSON.
Diagnostic aliases are required only for selected tests. Recommendations do
not allocate UART bindings. PNX_PARAMS_OVERRIDE is an advanced validation
input, analogous to PNX_ROBOT_CONFIG_OVERRIDE.
Local F4 worktree overrides have been removed from build caches. Both matrix
BSP path defaults now resolve to pnx_bsp; select a matching branch and --board,
or supply explicit paths when validating two checkouts.
The repeatable tests/v2-validation/validate.py matrix uses isolated inputs
under build/v2-validation: eight cases per board plus four missing-dependency
checks. All 16 Debug images and eight negative checks pass; 14 editor tests
pass. No hardware validation is implied. See docs/cmake-architecture-review.md.


CMake cleanup keeps the existing flat file layout. Generator helpers are
centralized locally, typed parameter rendering and output writing are shared,
and unchanged generated content preserves timestamps. Root motor/remoter
source filters use explicit loops; editor resource export and USBX disabled
hooks also avoid rewriting unchanged outputs. Before/after H7/F4 generated
C++ files match byte-for-byte, both Debug builds pass, and 13 editor/generator
regressions pass including stable output timestamps. Default data is now unified in configs/defaults.json. See docs/cmake-architecture-review.md.

ThreadX entry lives in each board Core/Src/app_threadx.c; USER CODE calls
app_start. app/app.cpp contains only application startup and optional diagnosis.
diagnose/app.cpp selects tests. H7/F4 Debug builds pass after restoring Core
ownership. Main is unchanged. Architecture and maintenance assessment:
docs/cmake-architecture-review.md. The current tests/v2-validation script covers the cases listed above.

Editor 0.4.0 uses minimal per-board defaults: USBX, BMI088/LED/DMIMU consumers,
remoter and all test selections are disabled. Existing robot motor lists are
preserved. Default params have no diagnostic aliases or report UART. Missing
test fields mean false; selected UART/CAN tests require explicit test_uart /
test_can bindings. GUI offers searchable add-binding actions beside missing
requirements. Disabling a test keeps user bindings. Other tests require their
USBX, robot consumer, remoter/referee or board GPIO dependencies. No none CAN
alias is synthesized. The unused test_report alias is removed. Main remains
unchanged; app_start honors auto_run_on_boot. USART diagnostic source is
excluded when its test is off.

F4 VT03/referee recommendations both use USART6; DR16 uses USART3. CAN fields
come from IOC resources and offer standard/extended IDs. Remoter off/disabled
remain compatible aliases displayed as none. PS2 fields are shown only for
the selected PS2 remoter. BMI088 tuning remains per-board and read-only last.
F4 Debug and H7 Release minimal builds pass; selected UART/CAN generation and
scoped compilation pass on both boards. Thirteen tests and a headless browser
binding-prompt check pass. No hardware verification was performed.

The optional VS Code editor MVP lives in `../pnx_config_editor/`. It edits existing
params/robot JSON through versioned WorkspaceEdit operations. The independent
`configs/cmake/export_editor_context.cmake` invokes the existing IOC parser in
script mode and exports read-only board resources, without reading application
JSON or changing firmware build inputs. The firmware never depends on editor
caches. Validation and field provenance: `../pnx_config_editor/docs/editor-mvp-validation.md`.
Editor 0.2 uses a JSON Schema/H7-default snapshot and supports dynamic UART,
CAN, SPI and ADC application aliases already accepted by the generator.
IOC parsing additionally exports configured GPIO input/output lists; ADC
editor choices reuse the existing single-regular-conversion list. Arbitrary
new application JSON fields are preserved but do not acquire firmware
semantics merely by being added in the editor. Board GPIO wiring stays in
board.json. Editor 0.2.2 adds params.bindings.gpio_inputs/gpio_outputs aliases
to those fixed board roles, generating typed app::gpio names on both MCUs. H7 defaults restoration never changes robot motor composition.
Editor 0.2.1 groups built-in and custom aliases into one Bindings section and
preserves the visible row, focus and resource search filters across form refreshes.

| Scope | State | Notes |
| --- | --- | --- |
| Stage A architecture audit | done | H7/F4 parent, BSP, generator, IOC, linker, public headers and consumers reviewed |
| Stage B V2 design | approved | Decisions in this file are authoritative |
| Stage C branches and living document | done | V2 branches created; this file established |
| Directory and dual-board build | done | Both profiles configure and link complete Debug and Release images against their matching BSP branch |
| Generated binding shape | done | IOC discovery and dynamic IDs feed private PWM/SPI/USART/CAN HAL bindings; generated files remain build-local |
| GPIO / EXTI | done | H7/F4 public headers match; F4 implementation uses generated pin roles and a private HAL callback bridge |
| DMA / memory | done | Shared buffer/view and generated range validation; H7 line size 32, F4 line size 1 and CCM excluded |
| PWM | done | Shared public contract and generated channel binding; H7 binds TIM3_CH4 to TIM6. Current F4 IOC selects TIM1/TIM4/TIM8 PWM; no logical board failsafe consumer is declared. |
| SPI | done | Shared public contract and private generated handles; current F4 SPI1 and DMA generated sources compile/link in the product matrix. F4 board.json has no onboard SPI device binding yet. |
| USART | done | Shared H7 callback/buffer-view contract and backend; IOC-generated UART/RX-DMA/TX-DMA private bindings replace both branches' hard-coded handle maps; F4 delivery modes/telemetry removed |
| CAN | done | Shared H7 callback/frame/restart contract; IOC-generated private handles; H7 FDCAN and F4 Classic bxCAN remain branch-private backends without F4 telemetry/capability API |
| ADC | done | Shared HAL-free public contract and private generated binding; current F4 IOC has no ADC channels, so the generic backend is present but not selected |
| USB | implementation done | Shared callback/state/try-send contract and matching USB sources; parent CMake replaces the two CubeMX USB application templates without editing generated files. USBX startup, descriptor parsing, controller startup and CDC callbacks share the existing private `bridge_usb.c`; `bridge_usb.h` connects the generated PCD binding and I/O implementation. Each BSP USB directory has four files. Both boards now select USBX for diagnostics by explicit user request. See `docs/usb-v2-integration.md`. |
| Flash | deferred | V2 public write API and product implementation removed; requires an explicit data-partition/contract decision before reconsideration |

## Planned implementation order

1. Directory/profile integration and dual-board CMake.
2. Common generated schema plus GPIO/EXTI.
3. DMA and memory policy.
4. PWM.
5. SPI.
6. USART.
7. CAN.
8. ADC.
9. USB.
10. Flash decision and implementation, if authorized.

Each loop must read this file and the two branch implementations, make only
the scoped changes, build/test the affected targets, self-review, and update
only durable facts in this file before proceeding.

## Validation status

| Validation | Status |
| --- | --- |
| H7 Debug compile | pass on 2026-09-09 with device-neutral generated PWM failsafe binding |
| H7 Release compile | pass on 2026-09-09 with generic PWM failsafe; FLASH 124628 B, DTCMRAM 80160 B |
| F4 configure | pass on 2026-09-08 with `PNX_BOARD=f407_c_board`, its authoritative IOC, and build-local generation |
| F4 Debug compile | pass on 2026-09-09; full image linked with FLASH 32936 B and RAM 49616 B; shared PWM/failsafe sources also pass Cortex-M4 syntax compilation |
| F4 Release compile | pass on 2026-09-09; full image linked with FLASH 19528 B and RAM 49616 B |
| F4 GPIO/EXTI compile | pass on 2026-09-08 with Cortex-M4 flags and generated F407 config |
| F4 DMA compile | pass on 2026-09-08 with Cortex-M4 flags and generated F407 memory policy |
| F4 PWM contract/build selection | pass on 2026-09-11 in the full image build matrix with IOC-generated TIM1/TIM4/TIM8 channels; hardware not tested |
| F4 SPI contract/build selection | pass on 2026-09-11 in the full image build matrix with regenerated SPI1/DMA sources; no logical onboard SPI consumer selected |
| F4 USART scoped compile | pass on 2026-09-08 for the shared Cortex-M4 backend and generated F407 private bindings |
| F4 CAN scoped compile | pass on 2026-09-08 for the Classic bxCAN backend and generated F407 private bindings; public header matches H7 |
| F4 ADC contract/build selection | pass on 2026-09-08; public header matches H7 and zero-channel IOC omits the ADC backend from the build graph |
| F4 USB scoped compile | pass on 2026-09-08 for the simplified Cortex-M4 backend and CDC bridge; product build remains intentionally disabled by `build.usbx=false` |
| V2 USB unified startup | pass on 2026-09-10 after consolidation into the existing bridge: H7 `h723_mc02` Debug/Release link; Release FLASH 124764 B, DTCMRAM 80208 B. Both BSP USB implementations pass scoped `-Werror` compilation; ARM simulation exercises real generated descriptors, FIFO/DCD binding and CDC deactivate. Enabled/disabled source-selection checks pass. No F4 product configure/build or hardware enumeration performed. |
| V2 generator regression inputs | pass on 2026-09-10: H7 default and F4 default, Classic CAN motor, and DMIMU combinations generate; F4 `fdcan3` and FD requests fail at generation as required |
| DMIMU configuration syntax compile | pass on 2026-09-10 for H7 `fdcan3` and F4 `can1` generated transport configurations; this is not a link or hardware claim |
| Shared consumer build matrix | pass on 2026-09-11: H7 default Release and DMIMU-only Debug; F4 default Release, Classic motor Debug and DMIMU Debug. DMIMU-only final images retain the DMIMU service and contain no BMI088 service/device symbols. Classic motor case checks compilation/linking, not motor instantiation or bus traffic. Run `python tests/v2-validation/validate.py`. |
| Board/application boundary | pass on 2026-09-11: toggling H7 onboard consumers preserves generated board binding headers; partial USB/test parameters retain per-field defaults; shared compile commands omit BSP private include directories. |
| F4 GPIO diagnostic syntax compile | pass on 2026-09-10 for generated active-high LED roles `led_r=PH12`, `led_g=PH11`, and `led_b=PH10`; hardware observation not run |
| Host tests | H7 parent has no current host suite; F4 branch records 37/37 before V2 |
| H7 hardware | not run |
| F4 hardware | not run for V2; historical F4 PS2 evidence is not V2 validation |

Do not turn historical branch documentation into a current V2 pass claim.

## PWM discovery

- PWM binding validation must accept HAL TIM_CHANNEL_1 == 0; both backends
  use IS_TIM_CHANNELS rather than rejecting zero. BMI088 now reports PWM
  start/duty failures through its existing temperature-control error state.
- F4 TIM7 failsafe IOC was corrected to memory-to-peripheral DMA and
  Prescaler=8400-1, Period=2500-1 (250 ms at 84 MHz). CubeMX regeneration is
  pending: current tim.c still has peripheral-to-memory DMA and PSC=26000-1.
  Regenerate before the next F4 full build/flash. Only scoped H7/F4 PWM and
  BMI088 compilation was performed for this correction; heating is untested.

- CubeMX may represent single-channel PWM as TIMx.Channel plus an explicit
  SH.S_TIMx_CHn PWM Generation signal. The importer recognizes this form as
  well as Channel-PWM entries, deduplicates channels, and does not treat a bare
  Channel entry or input-capture signal as PWM. TIM10_CH1 now resolves on F4.

## Current diagnostic selection

- params.ahrs.solver selects exactly one compile-time AHRS implementation:
  quaternion_ekf (default for both profiles) or tactical_ekf. The selected
  solver supplies the published quaternion, angles and cumulative yaw.
  Tactical diagnostics update only when tactical is selected; inactive
  temporary DWT solver timing stays zero. Both selections compile/link on
  F4 Debug and H7 Release; runtime switching is not supported.

- TEMP_IMU_DWT profiling fields in AHRS telemetry and diagnose imu_unit are
  temporary and must be removed after measurement. They use bsp::dwt::delta_s
  (CYCCNT) for read, quaternion EKF, tactical EKF and full processing-round
  elapsed microseconds. Full-round min/max/mean/count exclude DRDY wait and
  post-update sleep, but include preemption and instrumentation overhead.
  F4 Debug compiles; no measurements of this instrumented image yet.

- After the PC5 fix, read-only live F4 measurements confirmed both actual
  gyro_ready_count and update_count increasing while the diagnose snapshot
  froze. AHRS stayed ready at priority 3; update runtime was approximately
  0.8-1.1 ms with a faster DRDY stream, starving lower-priority monitoring
  and temperature control. params.ahrs.loop_sleep_ticks now selects a
  post-update sleep: F4 uses 1 tick, H7 defaults to 0. Sensor events still
  trigger acquisition and accumulated events are drained as before; the
  resulting F4 solve rate is lower. Hardware verification of this fix is pending.

- F4 BMI088 gyro INT1 is physically PC5, per the board pin table. The old
  PG3 binding and absent EXTI NVIC enable prevented post-calibration sampling.
  board.json and IOC now select PC5 falling-edge EXTI9_5 with pull-up, matching
  the shared gyro active-low interrupt configuration. CubeMX regenerated the PC5 GPIO and EXTI9_5 handler/enable;
  hardware verification is pending. Diagnose exposes imu_gyro_ready_count and
  imu_update_count to distinguish interrupt delivery from successful solving.

- F4 live read-only OpenOCD observation: CPU running; TIM10 CR1=1,
  CCER=1, CCMR1=0x68, PSC=167, ARR=999, CCR1=100. PF6 is AF3;
  60 asynchronous samples saw changing TIM10 CNT and both PF6 logic levels.
  This establishes active timer/pin output, not heater current or thermal power.
  The running board is outputting 10% despite source boost being restored to 6%.
  At the user's stated full-on 0.58 W, ideal average power at 10% is 0.058 W.
  Heat_Power 5V and actual heater current have not been measured.

- BMI088 temperature tuning belongs to each board profile's params.bmi088,
  not board.json: boost/reboost duty, approach endpoints, hold duty ceiling,
  PID gains, output/integral limits and variable-integral scalars are generated.
  The shared boost/approach/hold algorithm, 125-tick period and temperature
  thresholds remain unchanged; only hold runs PID. H7 retains original values.
  F4 starts with 90% boost/reboost, 90%-to-30% approach, 90% hold ceiling and
  PID 650/0.08/10 with output/integral limits 900/300. The approach endpoint
  is an initial tuning choice, not measured; legacy F4 used a different period,
  so these PID gains do not establish equivalent thermal behavior.

- Message subscriptions may be created from App_ThreadX_Init. The shared
  msg::subscribe uses TX_NO_WAIT when tx_thread_identify returns null, since
  ThreadX rejects TX_WAIT_FOREVER during initialization with TX_WAIT_ERROR.
  Thread callers retain blocking lock acquisition. This fixes the IMU
  diagnostic's subscribe_failed (failure_mask=2) startup path on both MCUs;
  hardware retest is pending.

- F4's current CubeMX application startup registers a ThreadX stack-error
  callback before App_ThreadX_Init. Parent CMake propagates
  TX_ENABLE_STACK_CHECKING through stm32cubemx to the application and ThreadX;
  without it the registration returns TX_FEATURE_NOT_ENABLED and startup halts.
  F4 Debug rebuilt successfully; ELF inspection confirms registration now
  stores the callback and returns TX_SUCCESS. Hardware retest is pending.

- UART diagnostics use generated app::uart::test_uart only when test.usart
  is selected; defaults do not allocate it. USBX and tests default to off.
- H7/F4 Release compile and link pass on 2026-09-11 with both diagnostic entry
  points and USBX startup retained in the final ELF. H7 FLASH 128808 B,
  DTCMRAM 80296 B; F4 FLASH 54168 B, RAM 70720 B. Hardware tests are pending.

## Remaining risks and unresolved issues

- F407 generated peripheral sources now build. Runtime and hardware behavior
  still need board testing; a successful link does not establish enumeration,
  DMA operation or sensor availability.
- F4 now declares BMI088 on SPI1, PA4/PB0 chip selects, PC5 gyro DRDY,
  and TIM10_CH1 heater with TIM7 update-DMA failsafe. Its robot configuration
  enables BMI088. F4 Debug compiles and links (FLASH 153684 B, RAM 84032 B);
  BMI088 acquisition and heating have not been hardware-tested.
- Current F407 USB descriptors contain the ST example VID/PID `0x0483/0x5710`,
  and no identity-confirmed macro. Product identity remains unconfirmed;
  the user has explicitly enabled `build.usbx=true` for F4 diagnostics.
  Enumeration has not been hardware-tested; the descriptor identity was not changed.
- USB currently uses CubeMX-configured FIFO I/O on both boards (PCD DMA off).
  High-speed frameworks follow the configured PCD speed, not the MCU family
  or OTG_HS name. Enabling PCD DMA needs a USBX-wide cache-safe memory contract;
  the private startup currently rejects that unsupported mode.
- Flash needs an explicit reserved-data layout on both boards before a shared
  writable API can be accepted.

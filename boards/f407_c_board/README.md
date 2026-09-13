# DJI C-board STM32F407 profile

This directory owns the authoritative CubeMX IOC and generated F407 board
tree, startup, linker, ThreadX/USBX integration, `board.cmake` and static
`board.json` bindings. The matching BSP implementation is the
`pnx_bsp` `stm32f4` branch.

## Current hardware closure

The authoritative IOC is migrated from the RM26 F4 profile and configures:

- CAN1 and CAN2;
- SPI1 with DMA, I2C3, USART1/USART2/USART3/USART6 and their selected DMA
  resources;
- TIM1/TIM2/TIM4/TIM5/TIM8/TIM10 and CRC;
- USB OTG FS and the ThreadX/USBX board integration;
- GPIO safe states for the historical BMI088 chip-select pins and board LEDs.

The generated Board tree still reflects the pre-migration IOC. Regenerate it
with CubeMX before configuring or building this profile. `board.json` remains
the separate logical-device binding layer; its BMI088 device remains disabled
until the regenerated Board tree and bindings have been validated.

## Adding a peripheral

Add SPI, PWM, ADC, timer and DMA resources through
`f407_c_board.ioc`, regenerate the CubeMX board tree, and then add the
logical role mapping to `board.json`. Do not restore the historical manual
SPI1 or TIM1 register initialization in the F4 BSP.

For a PWM output that must fail closed when its control thread stops or the
CPU is halted at a debugger breakpoint, give its `board.json` PWM role a
separate `failsafe_timer`. That timer must have an IOC Update DMA configured
memory-to-peripheral with word alignment. Its timer period remains owned by
the IOC. The common `pwm_failsafe` BSP implementation automatically writes
zero to the protected PWM CCR when the timer expires.

F407 DMA uses a fixed stream/channel mapping rather than the H7 DMAMUX request
model. The generated private binding relies on CubeMX's
`TIM_DMA_ID_UPDATE` handle link, so this difference does not enter the public
PWM contract. Resolve all DMA stream conflicts in CubeMX before regeneration.

## Consumer boundary

Application, Device and Module code uses board-neutral BSP headers and
generated logical identifiers. It must not include this Board's generated
headers directly or name HAL handles, GPIO ports, DMA streams/channels, IRQs
or F407 peripheral registers.

## Regeneration and validation

Review both IOC and generated-source diffs after CubeMX regeneration. A newly
configured peripheral is not considered supported until its matching F4 BSP
path compiles and the relevant hardware behaviour is tested. Current V2 F4
validation does not claim BMI088, PWM failsafe, SPI or ADC hardware coverage.

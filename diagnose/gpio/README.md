# GPIO diagnostic

When `params.test.gpio_leds=true`, this diagnostic cycles the board's logical
`led_r`, `led_g`, and `led_b` outputs every 500 ms. The F407 profile maps them
to PH12, PH11, and PH10 respectively, with active-high polarity.

`diagnose::gpio::start()` is called by the diagnostic firmware only when the
feature is configured. Inspect `demo_debug_instance.gpio_unit` for BSP setup
failures; visual colour changes confirm the physical output path.

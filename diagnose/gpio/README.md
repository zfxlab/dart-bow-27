# GPIO output diagnostic

Set params.test.gpio_leds=true (legacy switch name) and select a board output:

```json
{
  "test": {"gpio_leds": true, "auto_run_on_boot": true},
  "bindings": {"gpio_outputs": {"test_gpio": "led_r"}}
}
```

The example selects the F4 red LED. On either board, test_gpio must reference
an output role already declared in board.json; that role owns the pin and
active level. No LED-specific role is required. The diagnostic toggles the
selected output active/inactive every 500 ms using app::gpio::test_gpio.
Disabling the test excludes the source and does not require the alias.
Inspect demo_debug_instance.gpio_unit; last_step is the commanded active
state (0/1), observed_count counts successful writes, not measured pin levels.
No SPI LED support or input sampling is implied.

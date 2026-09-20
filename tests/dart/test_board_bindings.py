"""Regression checks for the oldframe dart wiring and CubeMX output.

Run: python -m unittest discover -s tests/dart -p "test_*.py"
These checks do not claim hardware operation.
"""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "boards/h723_mc02"
CONFIG = ROOT / "configs/boards/h723_mc02"
IOC = dict(line.split("=", 1) for line in (BOARD / "h723_mc02.ioc").read_text().splitlines() if "=" in line)
BOARD_CONFIG = json.loads((BOARD / "board.json").read_text())
PARAMS = json.loads((CONFIG / "params.json").read_text())
ROBOT = json.loads((CONFIG / "robot.json").read_text())


def number(expression):
    """CubeMX timing values in this profile are integers or integer minus one."""
    expression = expression.replace(" ", "")
    if re.fullmatch(r"[0-9]+-1", expression):
        return int(expression[:-2]) - 1
    return int(expression)


def assignment(source, name):
    match = re.search(re.escape(name) + r"\s*=\s*([^;]+);", source)
    if match is None:
        raise AssertionError("Missing generated assignment: " + name)
    return match.group(1).strip()


class DartBoardBindings(unittest.TestCase):
    def test_trigger_pwm_has_real_50hz_timer_and_pin(self):
        self.assertEqual(BOARD_CONFIG["bindings"]["pwm_channels"]["trigger"], {"timer": "tim1", "channel": 3})
        self.assertEqual(IOC["PE13.Signal"], "S_TIM1_CH3")
        source = (BOARD / "Core/Src/tim.c").read_text()
        psc = number(IOC["TIM1.Prescaler"])
        arr = number(IOC["TIM1.Period"])
        clock = int(IOC["RCC.Tim1OutputFreq_Value"])
        self.assertEqual(clock / (psc + 1) / (arr + 1), 50)
        self.assertEqual(number(assignment(source, "htim1.Init.Prescaler")), psc)
        self.assertEqual(number(assignment(source, "htim1.Init.Period")), arr)
        self.assertIn("HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3)", source)
        self.assertIn("GPIO_AF1_TIM1", source)
        self.assertIn("MX_TIM1_Init();", (BOARD / "Core/Src/main.c").read_text())

    def test_polled_limits_keep_original_polarity(self):
        bindings = BOARD_CONFIG["bindings"]["gpio_inputs"]
        self.assertEqual(bindings["trigger_locked"], {"pin": "pe0", "active_level": "low"})
        self.assertEqual(bindings["launch_return"], {"pin": "pe14", "active_level": "high"})
        self.assertEqual(IOC["PE0.Signal"], "GPIO_Input")
        self.assertEqual(IOC["PE0.GPIO_PuPd"], "GPIO_PULLUP")
        self.assertEqual(IOC["PE14.Signal"], "GPIO_Input")
        source = (BOARD / "Core/Src/gpio.c").read_text()
        self.assertRegex(source, r"GPIO_InitStruct.Pin = TRIGGER_LOCKED_Pin;\s*GPIO_InitStruct.Mode = GPIO_MODE_INPUT;\s*GPIO_InitStruct.Pull = GPIO_PULLUP;")
        self.assertIn("LAUNCH_RETURN_Pin", source)

    def test_force_and_host_uart_have_matching_baud_and_dma(self):
        self.assertEqual(PARAMS["bindings"]["uart_ports"]["force_sensor"], "uart7")
        self.assertEqual(PARAMS["bindings"]["uart_ports"]["host"], "usart10")
        self.assertEqual(PARAMS["bindings"]["referee_uart"], "usart1")
        source = (BOARD / "Core/Src/usart.c").read_text()
        for peripheral, handle in [("UART7", "huart7"), ("USART10", "huart10"), ("USART1", "huart1")]:
            self.assertEqual(int(IOC[peripheral + ".BaudRate"]), 115200)
            self.assertEqual(number(assignment(source, handle + ".Init.BaudRate")), 115200)
            for direction in ["RX", "TX"]:
                key = next(key for key in IOC if key.startswith("Dma." + peripheral + "_" + direction + ".") and key.endswith(".Instance"))
                stream = IOC[key]
                dma_handle = "hdma_" + peripheral.lower() + "_" + direction.lower()
                self.assertEqual(assignment(source, dma_handle + ".Instance"), stream)
                self.assertTrue(IOC["NVIC." + stream + "_IRQn"].startswith("true"))
                self.assertIn("HAL_DMA_IRQHandler(&" + dma_handle + ");", (BOARD / "Core/Src/stm32h7xx_it.c").read_text())

    def test_motor_buses_are_classic_one_megabit(self):
        source = (BOARD / "Core/Src/fdcan.c").read_text()
        clock = int(IOC["RCC.FDCANFreq_Value"])
        for bus in ["FDCAN1", "FDCAN2", "FDCAN3"]:
            self.assertEqual(IOC[bus + ".FrameFormat"], "FDCAN_FRAME_CLASSIC")
            self.assertEqual(assignment(source, "h" + bus.lower() + ".Init.FrameFormat"), "FDCAN_FRAME_CLASSIC")
            bitrate = clock / int(IOC[bus + ".NominalPrescaler"]) / (1 + int(IOC[bus + ".NominalTimeSeg1"]) + int(IOC[bus + ".NominalTimeSeg2"]))
            self.assertEqual(bitrate, 1000000)
        self.assertEqual(PARAMS["can"]["fdcan3"]["id_type"], "extended")
        self.assertGreater(int(IOC["FDCAN3.ExtFiltersNbr"]), 0)
        self.assertEqual(number(assignment(source, "hfdcan3.Init.ExtFiltersNbr")), int(IOC["FDCAN3.ExtFiltersNbr"]))
        self.assertEqual(PARAMS["bindings"]["can_buses"]["string_bus"], "fdcan3")

    def test_remote_preserves_oldframe_uart5_line_settings(self):
        # This is the actual oldframe/template setup, not a generic DR16 preset.
        # Electrical compatibility still needs a real receiver test.
        self.assertEqual(PARAMS["bindings"]["remoter_uart"], "uart5")
        source = (BOARD / "Core/Src/usart.c").read_text()
        self.assertEqual(number(assignment(source, "huart5.Init.BaudRate")), 100000)
        self.assertEqual(assignment(source, "huart5.Init.WordLength"), "UART_WORDLENGTH_9B")
        self.assertEqual(assignment(source, "huart5.Init.Parity"), "UART_PARITY_NONE")
        self.assertEqual(assignment(source, "huart5.Init.StopBits"), "UART_STOPBITS_2")
        self.assertEqual(IOC["UART5.WordLength"], "WORDLENGTH_9B")
        self.assertEqual(IOC["UART5.StopBits"], "UART_STOPBITS_2")

    def test_robot_retains_active_oldframe_motor_wiring(self):
        motors = {entry["name"]: entry for entry in ROBOT["devices"]["motors"]["list"]}
        self.assertEqual(set(motors), {"synbelt", "yaw", "string_l", "string_r"})
        self.assertEqual((motors["synbelt"]["model"], motors["synbelt"]["can_bus"], int(motors["synbelt"]["can_id"], 0)), ("dm_dm4310", "fdcan1", 2))
        self.assertEqual((motors["yaw"]["model"], motors["yaw"]["can_bus"], int(motors["yaw"]["can_id"], 0)), ("dm_dm8009p", "fdcan2", 3))
        for name, address, positive_dir in [("string_l", 2, 0), ("string_r", 1, 1)]:
            self.assertEqual(motors[name]["can_bus"], "fdcan3")
            self.assertEqual(int(motors[name]["can_id"], 0), address)
            self.assertEqual(motors[name]["positive_dir"], positive_dir)


if __name__ == "__main__":
    unittest.main()

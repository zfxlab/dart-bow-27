#include "imu_demo.hpp"
#include "motor_demo.hpp"
#include "ps2_demo.hpp"
#include "referee_ui_demo.hpp"
#include "remoter_demo.hpp"
#include "usart_demo.hpp"
#include "usb_demo.hpp"

extern "C" void diagnose_start()
{
    diagnose::imu::start();
    diagnose::motor::start();
    diagnose::ps2::start();
    // diagnose::remoter::start();
    // diagnose::referee_ui::start();
    // diagnose::usart::start();
    // diagnose::usb::start();
}

extern "C" void app_start()
{
    diagnose_start();
}

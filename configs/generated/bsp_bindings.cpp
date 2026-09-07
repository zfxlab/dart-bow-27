// Generated from board/board.ioc. Do not edit.

#include "bsp_bindings.hpp"
#include "bsp_adc.hpp"
#include "bsp_pwm.hpp"
#include "bsp_spi.hpp"
#include "adc.h"
#include "spi.h"
#include "tim.h"

namespace bsp::pwm::detail {

bool binding_for(channel channel_id, binding& out) noexcept
{
    switch (channel_id)
    {
    case channel::tim12_ch2: out = { &htim12, TIM_CHANNEL_2 }; return true;
    case channel::tim3_ch4: out = { &htim3, TIM_CHANNEL_4 }; return true;
    default: return false;
    }
}

} // namespace bsp::pwm::detail

namespace bsp::adc::detail {

bool binding_for(channel channel_id, binding& out) noexcept
{
    switch (channel_id)
    {
    case channel::adc1_ch4: out = { &hadc1, ADC_CHANNEL_4, ADC_REGULAR_RANK_1, ADC_SAMPLETIME_1CYCLE_5 }; return true;
    default: return false;
    }
}

} // namespace bsp::adc::detail

namespace bsp::spi::detail {

bool binding_for(bus bus_id, binding& out) noexcept
{
    switch (bus_id)
    {
    case bus::spi2: out = { &hspi2, true, true, true }; return true;
    case bus::spi6: out = { &hspi6, false, false, false }; return true;
    default: return false;
    }
}

} // namespace bsp::spi::detail

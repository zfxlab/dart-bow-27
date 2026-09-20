#pragma once

#include "tx_api.h"

namespace dart
{
class delay
{
public:
    void reset()
    {
        started = false;
        delay_ok = false;
        start_tick = 0;
    }

    /**
     * @brief Check if the delay has been reached
     * 
     * @param delay_ticks 
     * @param delay_init 在调用时是否重置延时，默认为false
     * @return true 
     * @return false 
     */
    bool reached(ULONG delay_ticks, bool delay_init = false)
    {
        if (delay_init)
        {
            reset();
        }

        if (!started)
        {
            start_tick = tx_time_get();
            started = true;
        }

        delay_ok = (tx_time_get() - start_tick) >= delay_ticks;
        if (!delay_ok)
        {
            return false;
        }

        reset();
        return true;
    }

/**
 * @brief Check if the delay has been reached based on a trigger condition
 * 
 * @param delay_trigger 触发条件，只需要一次满足就开始计时
 * @param delay_ticks 
 * @param delay_init 在调用时是否重置延时，默认为false
 * @return true 
 * @return false 
 */
    bool reached(bool delay_trigger, ULONG delay_ticks, bool delay_init = false)
    {
        if (!started && !delay_trigger)
        {
            return false;
        }

        return reached(delay_ticks, delay_init);
    }

    /**
     * @brief check if the delay has been reached, with an enable condition to control whether to count the delay
     * 
     * @param delay_enable 开始延时的条件，需要持续满足，如果不满足则重置延时
     * @param delay_ticks 
     * @param delay_init 在调用时是否重置延时，默认为false
     * @return true 
     * @return false 
     */
    bool stable(bool delay_enable, ULONG delay_ticks, bool delay_init = false)
    {
        if (!delay_enable)
        {
            reset();
            return false;
        }

        return reached(delay_ticks, delay_init);
    }

    /**
     * @brief check if the delay has been reached, once the delay is reached, it will keep returning true until reset
     * 
     * @param delay_ticks 
     * @param delay_init 
     * @return true 
     * @return false 
     */
    bool latched(ULONG delay_ticks, bool delay_init = false)
    {
        if (delay_init)
        {
            reset();
        }

        if (delay_ok)
        {
            return true;
        }

        if (!started)
        {
            start_tick = tx_time_get();
            started = true;
        }

        delay_ok = (tx_time_get() - start_tick) >= delay_ticks;
        return delay_ok;
    }

    bool latched(bool delay_trigger, ULONG delay_ticks, bool delay_init = false)
    {
        if (delay_init)
        {
            reset();
        }

        if (delay_ok)
        {
            return true;
        }

        if (!started && !delay_trigger)
        {
            return false;
        }

        return latched(delay_ticks);
    }

    bool started_now() const { return started; }

private:
    bool started = false;
    bool delay_ok = false;
    ULONG start_tick = 0;
};

} // namespace dart

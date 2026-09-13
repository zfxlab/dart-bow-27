#include "config.hpp"

extern "C" void diagnose_start();

extern "C" void app_start()
{
    if constexpr (params::test::auto_run_on_boot)
    {
        diagnose_start();
    }
}


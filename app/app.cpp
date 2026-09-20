#include "config.hpp"
#if PNX_DART_ENABLED
#include "dart/start.hpp"
#endif

extern "C" void diagnose_start();

extern "C" void app_start()
{
#if PNX_DART_ENABLED
    (void)dart::start();
#else
    if constexpr (params::test::auto_run_on_boot)
    {
        diagnose_start();
    }
#endif
}


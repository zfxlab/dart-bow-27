#include "demo_debug.hpp"

extern "C" {
diagnose::debug::debug_instance_type demo_debug_instance{};
}

namespace diagnose::debug
{

debug_instance_type& debug_instance = demo_debug_instance;

void reset(link_state& state) noexcept
{
    state = {};
}

} // namespace diagnose::debug

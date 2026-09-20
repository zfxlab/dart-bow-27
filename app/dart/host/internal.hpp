#pragma once

#include "protocol.hpp"
#include "usertypes.hpp"
#include <cstddef>

namespace dart::host
{
bool pop_request(request& req) noexcept;
bool push_responses(const response* data, std::size_t count) noexcept;
types::status init_service() noexcept;
} // namespace dart::host

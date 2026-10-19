// SPDX-License-Identifier: GPL-3.0-or-later
//
// EnTT, with its checks reporting through our log (logging/crash.hpp) instead of a bare assert, so a
// failed one says what failed in the run's log and leaves a stack trace. Include this, never EnTT itself:
// every file must see the same setting. Release builds keep EnTT's default, no checks.
#pragma once

#ifndef NDEBUG
    #include "logging/crash.hpp"
    #define ENTT_ASSERT(condition, message) ((condition) ? void() : ::rct3::logging::check_failed(#condition, message))
#endif
#include "entt/entt.hpp"

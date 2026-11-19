// SPDX-License-Identifier: GPL-3.0-or-later
#include "random/entropy.hpp"

#include "core/types.hpp"
#include "random/generator.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <thread>

#if defined(_WIN32)
    #include <process.h>
#else
    #include <unistd.h>
#endif

namespace rct3::random
{

    namespace
    {

        /// Folds `value` into `pool`, so every source changes every bit of the result.
        void mix(u64& pool, u64 value)
        {
            pool ^= value;
            pool = split_mix(pool);
        }

        u64 process_id()
        {
#if defined(_WIN32)
            return static_cast<u64>(_getpid());
#else
            return static_cast<u64>(getpid());
#endif
        }

        /// Two draws from the system's random device. Constructing or reading it may fail (no device, a
        /// sandbox); then it adds nothing.
        void mix_device(u64& pool)
        {
            try
            {
                std::random_device device;
                const u64 high = device();
                const u64 low = device();
                mix(pool, (high << 32) ^ low);
            }
            catch (...)
            {
                // The other sources stand in for it.
            }
        }

    } // namespace

    Seed fresh_seed()
    {
        u64 pool = 0;
        mix_device(pool);
        mix(pool, static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count()));
        mix(pool, static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()));
        mix(pool, process_id());
        mix(pool, std::hash<std::thread::id> { }(std::this_thread::get_id()));
        const i32 on_stack = 0;
        mix(pool, reinterpret_cast<std::uintptr_t>(&on_stack));
        mix(pool, reinterpret_cast<std::uintptr_t>(&fresh_seed));
        // Two calls in one run still differ: the steady clock has moved on.
        mix(pool, static_cast<u64>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        return Seed { pool };
    }

} // namespace rct3::random

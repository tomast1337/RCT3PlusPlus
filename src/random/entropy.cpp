// SPDX-License-Identifier: GPL-3.0-or-later
#include "random/entropy.hpp"

#include "random/generator.hpp"

#include <chrono>
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
        void mix(std::uint64_t& pool, std::uint64_t value)
        {
            pool ^= value;
            pool = split_mix(pool);
        }

        std::uint64_t process_id()
        {
#if defined(_WIN32)
            return static_cast<std::uint64_t>(_getpid());
#else
            return static_cast<std::uint64_t>(getpid());
#endif
        }

        /// Two draws from the system's random device. Constructing or reading it may fail (no device, a
        /// sandbox); then it adds nothing.
        void mix_device(std::uint64_t& pool)
        {
            try
            {
                std::random_device device;
                const std::uint64_t high = device();
                const std::uint64_t low = device();
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
        std::uint64_t pool = 0;
        mix_device(pool);
        mix(pool, static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()));
        mix(pool, static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
        mix(pool, process_id());
        mix(pool, std::hash<std::thread::id> { }(std::this_thread::get_id()));
        const int on_stack = 0;
        mix(pool, reinterpret_cast<std::uintptr_t>(&on_stack));
        mix(pool, reinterpret_cast<std::uintptr_t>(&fresh_seed));
        // Two calls in one run still differ: the steady clock has moved on.
        mix(pool, static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        return Seed { pool };
    }

} // namespace rct3::random

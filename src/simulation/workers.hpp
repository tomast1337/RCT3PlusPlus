// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fixed set of threads that share out loops over many entities. Systems use it for work where each item
// only writes its own entity's components, so the result does not depend on how the work was shared.
#pragma once

#include "core/types.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rct3::simulation
{

    class Workers
    {
    public:
        /// `threads` helpers besides the calling thread; 0 runs everything on the caller.
        explicit Workers(u32 threads = std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 0);
        ~Workers();
        Workers(const Workers&) = delete;
        Workers& operator=(const Workers&) = delete;

        /// Calls `body(begin, end)` over ranges that together cover [0, count) once each, and returns when all
        /// are done. The calling thread takes a share.
        void for_ranges(usize count, const std::function<void(usize, usize)>& body);

    private:
        void work();
        void take_ranges();

        std::vector<std::jthread> threads_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::condition_variable done_;
        const std::function<void(usize, usize)>* body_ = nullptr;
        usize count_ = 0;
        usize next_ = 0; ///< Start of the next range to hand out
        usize chunk_ = 1;
        usize busy_helpers_ = 0;
        usize job_ = 0; ///< Counts jobs, so helpers wake once per job
        bool stopping_ = false;
    };

} // namespace rct3::simulation

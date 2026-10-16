// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fixed set of threads that share out loops over many entities. Systems use it for work where each item
// only writes its own entity's components, so the result does not depend on how the work was shared.
#pragma once

#include <condition_variable>
#include <cstddef>
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
        explicit Workers(unsigned threads = std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 0);
        ~Workers();
        Workers(const Workers&) = delete;
        Workers& operator=(const Workers&) = delete;

        /// Calls `body(begin, end)` over ranges that together cover [0, count) once each, and returns when all
        /// are done. The calling thread takes a share.
        void for_ranges(std::size_t count, const std::function<void(std::size_t, std::size_t)>& body);

    private:
        void work();
        void take_ranges();

        std::vector<std::jthread> threads_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::condition_variable done_;
        const std::function<void(std::size_t, std::size_t)>* body_ = nullptr;
        std::size_t count_ = 0;
        std::size_t next_ = 0; ///< Start of the next range to hand out
        std::size_t chunk_ = 1;
        std::size_t busy_ = 0; ///< Helpers still in the current job
        std::size_t job_ = 0;  ///< Counts jobs, so helpers wake once per job
        bool stopping_ = false;
    };

} // namespace rct3::simulation

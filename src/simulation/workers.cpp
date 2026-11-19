// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/workers.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cstdint>

namespace rct3::simulation
{

    namespace
    {
        constexpr usize kMinChunk = 64; ///< Items below which sharing out costs more than it saves
    }

    Workers::Workers(u32 threads)
    {
        for (u32 i = 0; i < threads; ++i)
            threads_.emplace_back([this] { work(); });
    }

    Workers::~Workers()
    {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        threads_.clear(); // Joins them while the mutex they wait on still exists
    }

    void Workers::take_ranges()
    {
        for (;;)
        {
            usize begin = 0;
            usize end = 0;
            {
                const std::scoped_lock lock(mutex_);
                if (next_ >= count_)
                    return;
                begin = next_;
                end = std::min(count_, begin + chunk_);
                next_ = end;
            }
            (*body_)(begin, end);
        }
    }

    void Workers::work()
    {
        usize seen = 0;
        for (;;)
        {
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || job_ != seen; });
                if (stopping_)
                    return;
                seen = job_;
            }
            take_ranges();
            {
                const std::scoped_lock lock(mutex_);
                --busy_helpers_;
            }
            done_.notify_one();
        }
    }

    void Workers::for_ranges(usize count, const std::function<void(usize, usize)>& body)
    {
        if (count == 0)
            return;
        if (threads_.empty() || count < kMinChunk * 2)
        {
            body(0, count);
            return;
        }
        {
            const std::scoped_lock lock(mutex_);
            body_ = &body;
            count_ = count;
            next_ = 0;
            chunk_ = std::max(kMinChunk, count / ((threads_.size() + 1) * 4));
            busy_helpers_ = threads_.size();
            ++job_;
        }
        wake_.notify_all();
        take_ranges();
        std::unique_lock lock(mutex_);
        done_.wait(lock, [&] { return busy_helpers_ == 0; });
        body_ = nullptr;
    }

} // namespace rct3::simulation

// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/workers.hpp"

#include <algorithm>

namespace rct3::simulation
{

    namespace
    {
        constexpr std::size_t kMinChunk = 64; ///< Items below which sharing out costs more than it saves
    }

    Workers::Workers(unsigned threads)
    {
        for (unsigned i = 0; i < threads; ++i)
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
            std::size_t begin = 0;
            std::size_t end = 0;
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
        std::size_t seen = 0;
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
                --busy_;
            }
            done_.notify_one();
        }
    }

    void Workers::for_ranges(std::size_t count, const std::function<void(std::size_t, std::size_t)>& body)
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
            busy_ = threads_.size();
            ++job_;
        }
        wake_.notify_all();
        take_ranges();
        std::unique_lock lock(mutex_);
        done_.wait(lock, [&] { return busy_ == 0; });
        body_ = nullptr;
    }

} // namespace rct3::simulation

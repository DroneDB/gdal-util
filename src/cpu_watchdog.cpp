/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "cpu_watchdog.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace gdalutil
{
    namespace
    {
        using std::chrono::nanoseconds;

        // The shortest wait: near the limit the budget left is small, and the measure costs more
        // than the time it could save
        constexpr std::chrono::milliseconds kShortestWait{10};

        // The cpu time of the process so far, user and kernel, all threads
        nanoseconds processCpuTime()
        {
#ifdef _WIN32
            FILETIME creation{}, ended{}, kernel{}, user{};
            if (!GetProcessTimes(GetCurrentProcess(), &creation, &ended, &kernel, &user))
                return nanoseconds(0);
            const auto ticks = [](const FILETIME &time) {
                return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
            };
            // FILETIME counts 100 ns
            return nanoseconds(static_cast<int64_t>((ticks(kernel) + ticks(user)) * 100));
#else
            timespec now{};
            if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) != 0)
                return nanoseconds(0);
            return std::chrono::seconds(now.tv_sec) + nanoseconds(now.tv_nsec);
#endif
        }

        // Ends the process at once: no exit handler, no destructor, no other thread goes on
        [[noreturn]] void endProcess(int code)
        {
#ifdef _WIN32
            TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
#endif
            std::_Exit(code);
        }

        unsigned parallelThreads()
        {
            return std::max(1u, std::thread::hardware_concurrency());
        }
    } // namespace

    CpuWatchdog::CpuWatchdog() : thread_([this] { run(); }) {}

    CpuWatchdog::~CpuWatchdog()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        thread_.join();
    }

    void CpuWatchdog::arm(std::chrono::seconds limit, std::function<void()> exceeded)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            armed_ = limit.count() > 0;
            start_ = processCpuTime();
            limit_ = limit;
            exceeded_ = std::move(exceeded);
        }
        changed_.notify_all();
    }

    void CpuWatchdog::disarm()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            armed_ = false;
            exceeded_ = nullptr;
        }
        changed_.notify_all();
    }

    void CpuWatchdog::run()
    {
        const unsigned threads = parallelThreads();
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;)
        {
            changed_.wait(lock, [this] { return stopping_ || armed_; });
            if (stopping_)
                return;
            const nanoseconds used = processCpuTime() - start_;
            if (used >= limit_)
            {
                // The lock stays held: the request cannot be disarmed, nor its response written
                if (exceeded_)
                    exceeded_();
                endProcess(kCpuLimitExitCode);
            }
            const nanoseconds soonest = (limit_ - used) / threads;
            changed_.wait_for(lock, std::max<nanoseconds>(soonest, kShortestWait));
        }
    }
} // namespace gdalutil

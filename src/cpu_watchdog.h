/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_CPU_WATCHDOG_H
#define GDAL_UTIL_CPU_WATCHDOG_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace gdalutil
{
    // The exit code of a process the watchdog ended
    inline constexpr int kCpuLimitExitCode = 4;

    // Bounds the cpu time of one request in a process that serves many (the server on Windows,
    // where the limit of the system counts the whole life of the process). A thread of its own
    // measures the cpu time the process used since the request was armed (all its threads
    // together); past the limit it runs the request's `exceeded` and ends the process with
    // kCpuLimitExitCode, without unwinding anything.
    //
    // The thread does not poll: the cpu time of the process grows at most as fast as the wall
    // clock times the threads the machine runs at once, so it sleeps for the remaining budget
    // divided by that number, the soonest the limit can be passed, and measures again.
    class CpuWatchdog
    {
      public:
        CpuWatchdog();
        ~CpuWatchdog();
        CpuWatchdog(const CpuWatchdog &) = delete;
        CpuWatchdog &operator=(const CpuWatchdog &) = delete;

        // Watches the cpu time from now. A limit of 0 watches nothing. `exceeded` runs on the
        // thread of the watchdog, which holds the watch while it runs: disarm waits for it.
        void arm(std::chrono::seconds limit, std::function<void()> exceeded);

        // Stops watching. When the limit was passed it does not return: the process is ending.
        void disarm();

      private:
        void run();

        std::mutex mutex_;
        std::condition_variable changed_;
        bool armed_ = false;
        bool stopping_ = false;
        std::chrono::nanoseconds start_{0};
        std::chrono::nanoseconds limit_{0};
        std::function<void()> exceeded_;
        // Last: it starts once the members above exist
        std::thread thread_;
    };
} // namespace gdalutil

#endif

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "thread_count.h"

#include <filesystem>
#include <system_error>

namespace gdalutil
{
    std::optional<size_t> threadCount()
    {
#ifdef __linux__
        std::error_code ec;
        size_t count = 0;
        for (std::filesystem::directory_iterator it("/proc/self/task", ec), end; !ec && it != end; it.increment(ec))
            count++;
        if (ec || count == 0)
            return std::nullopt;
        return count;
#else
        return std::nullopt;
#endif
    }
} // namespace gdalutil

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_THREAD_COUNT_H
#define GDAL_UTIL_THREAD_COUNT_H

#include <cstddef>
#include <optional>

namespace gdalutil
{
    // The number of threads of this process, where the system can tell (Linux: the entries of
    // /proc/self/task); empty elsewhere
    std::optional<size_t> threadCount();
} // namespace gdalutil

#endif

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_EXECUTOR_H
#define GDAL_UTIL_EXECUTOR_H

#include <memory>

#include "raster_protocol.h"

namespace gdalutil
{
    // Runs the requests the server reads. On POSIX systems each request runs in a worker, a
    // child process of its own that ends after the request, so whatever a hostile file does to
    // the memory of the parser (or to its own process) is gone with it and never reaches the
    // next file. On Windows the server runs the requests itself.
    class Executor
    {
      public:
        virtual ~Executor() = default;

        // Never throws: a request that cannot be run is a response that says why
        virtual ddb::raster::Response execute(const ddb::raster::Request &request) = 0;
    };

    // The executor of the platform
    std::unique_ptr<Executor> makePlatformExecutor();
} // namespace gdalutil

#endif

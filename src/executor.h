/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_EXECUTOR_H
#define GDAL_UTIL_EXECUTOR_H

#include <memory>

#include "confinement.h"
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

    // The executor of the platform. On POSIX systems each worker confines itself with the
    // confinement before it opens anything; the confinement outlives the executor. On Windows the
    // server confines itself once, before it serves (Confinement::startServer), and a watchdog
    // ends it when a request uses more cpu time than the confinement allows that request.
    //
    // `responseFd` is where the server writes its responses (Runtime::responseFd). A worker
    // closes it first: only the server answers the host, and a worker that a file took over
    // must have no way to write a line the host would read as a response. On Windows the
    // watchdog answers there with `limit` before it ends the server.
    std::unique_ptr<Executor> makePlatformExecutor(const Confinement &confinement, int responseFd);
} // namespace gdalutil

#endif

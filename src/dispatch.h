/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_DISPATCH_H
#define GDAL_UTIL_DISPATCH_H

#include "raster_protocol.h"

namespace gdalutil
{
    // Runs the operation of the request in this process and returns its response. Never throws:
    // a failure is a response with the code that tells the host what to do with it
    // (ddb::raster::classify).
    //
    // `progress` is where a long operation (cog, vector, geoproject) reports how far it is. The
    // request of `--request` has one; a request of `--serve` has none and is refused for these
    // operations: the long ones run in a process of their own, for as long as they take.
    ddb::raster::Response handleRequest(const ddb::raster::Request &request,
                                        const ddb::raster::ProgressCallback *progress = nullptr);
} // namespace gdalutil

#endif

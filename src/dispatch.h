/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_DISPATCH_H
#define GDAL_UTIL_DISPATCH_H

#include "confinement.h"
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

    // Confines this process as the request needs (confinement.h) and then runs it with
    // handleRequest. Call it first in a process that was started or forked for the request: it
    // restricts the process for good. A request the process cannot be confined for (strict mode
    // and a layer the host lacks, a folder that does not exist) is a failure that says why, and
    // nothing of it is run. `logWarning` is whether this process logs what best-effort mode could
    // not apply: a one-shot process does, a worker of a server does not (the server said so once).
    ddb::raster::Response handleConfined(const Confinement &confinement, const ddb::raster::Request &request,
                                         const ddb::raster::ProgressCallback *progress, bool logWarning);

    // Runs a probe and a thumbnail of rasters the helper writes itself (a GeoTIFF in a projected
    // coordinate system and a JPEG), so that what GDAL, PROJ and the encoders set up the first
    // time they are used is in place: a worker forked afterwards inherits it instead of setting
    // it up again for every request (2.4 ms of a photo thumbnail, 3.4 ms of a GeoTIFF one). It reads no
    // file of a user and leaves the process with the one thread it had. Never throws: a warm-up
    // that fails only leaves that cost to the workers.
    void warmUp();
} // namespace gdalutil

#endif

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_SERVER_H
#define GDAL_UTIL_SERVER_H

#include "confinement.h"
#include "executor.h"

namespace gdalutil
{
    // Reads the request lines of requestFd, runs each with the executor and writes its response
    // line to responseFd, in order, until the input ends (or the host stops reading). A line that
    // is not a request is answered, never ends the loop. Returns the exit code of the program.
    int serve(Executor &executor, int requestFd, int responseFd);

    // Runs the one request of the line, in this process, and writes its response line to
    // responseFd. Returns the exit code of the program: 0 when the response is a success, 1 when
    // it is a failure.
    //
    // The process confines itself for the request first (confinement.h), so call this in a
    // process that was started for the one request.
    int serveOne(const char *requestLine, int responseFd, const Confinement &confinement);
} // namespace gdalutil

#endif

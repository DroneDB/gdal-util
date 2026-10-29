/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_MEMORY_WATCH_H
#define GDAL_UTIL_MEMORY_WATCH_H

namespace gdalutil
{
    // Whether an allocation failed in GDAL during the request. When the sandbox limits the address
    // space of the process, GDAL does not throw: it reports CPLE_OutOfMemory and the operation
    // then fails with whatever error the missing buffer causes (a block it could not read, a
    // compressor without a buffer). The error that reaches the client must say the limit was
    // the reason, not "internal".
    namespace memory_watch
    {
        // Puts the watch in front of the handler of libddb (which keeps getting every message).
        // Call it once, after libddb is initialised.
        void install();

        // Whether GDAL reported a failed allocation since install()
        bool seen();
    } // namespace memory_watch
} // namespace gdalutil

#endif

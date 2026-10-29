/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "memory_watch.h"

#include <atomic>

#include "gdal_inc.h"
#include "log_forwarding.h"

namespace gdalutil::memory_watch
{
    namespace
    {
        // Set from whichever thread GDAL fails on, read when the request has failed
        std::atomic<bool> g_outOfMemory{false};

        void CPL_STDCALL watch(CPLErr errorClass, CPLErrorNum errorNum, const char *message)
        {
            if (errorNum == CPLE_OutOfMemory)
                g_outOfMemory.store(true, std::memory_order_relaxed);
            ddb::logfwd::gdalErrorHandler(errorClass, errorNum, message);
        }
    } // namespace

    void install()
    {
        CPLSetErrorHandler(&watch);
    }

    bool seen()
    {
        return g_outOfMemory.load(std::memory_order_relaxed);
    }
} // namespace gdalutil::memory_watch

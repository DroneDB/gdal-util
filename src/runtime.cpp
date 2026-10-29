/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "runtime.h"

#include <cstdlib>
#include <iostream>

#ifndef _WIN32
#include <csignal>
#endif

#include "fd_io.h"
#include "gdal_open_policy.h"
#include "process_init.h"

namespace gdalutil
{
    Runtime::Runtime() : responseFd_(claimStandardOutput())
    {
        if (responseFd_ < 0)
        {
            std::cerr << "gdal-util: cannot keep standard output for the responses" << std::endl;
            std::exit(EXIT_FAILURE);
        }
#ifndef _WIN32
        // A host that goes away must end a write with an error, not kill the process
        std::signal(SIGPIPE, SIG_IGN);
#endif
        // Logging goes to standard error (libddb's console is stderr), and GDAL and PROJ are
        // initialised here, once: the workers the server forks start with them ready. This
        // starts no thread (the server checks before every fork)
        ddb::registerProcess(false);
        // This program is where the files of users are opened: nowhere else in the library may
        ddb::gdalopen::enableUserFileOpens();
    }

    Runtime::~Runtime()
    {
        ddb::finalizeProcess();
    }
} // namespace gdalutil

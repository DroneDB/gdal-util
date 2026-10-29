/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// gdal-util: the program that reads the files of users with GDAL (PROTOCOL.md).
//
//   gdal-util --version          the version line
//   gdal-util --serve            answer the requests on standard input, one response line each
//   gdal-util --request '<json>' run one request and write its response line
//   gdal-util --sandbox-report   what confines the workers of the program

#include <iostream>
#include <string>

#include "executor.h"
#include "raster_protocol.h"
#include "runtime.h"
#include "server.h"

namespace
{
    constexpr int kUsageError = 2;

    void printUsage(std::ostream &out)
    {
        out << "usage: gdal-util --version | --serve | --request '<json>' | --sandbox-report" << std::endl;
    }

    int printVersion()
    {
        std::cout << "gdal-util " << GDAL_UTIL_VERSION << " protocol " << ddb::raster::kMajorVersion << "."
                  << ddb::raster::kMinorVersion << std::endl;
        return 0;
    }

    int printSandboxReport()
    {
        std::cout << "sandbox: not built in" << std::endl;
        return 0;
    }
} // namespace

// argv in UTF-8 on Windows too (the C runtime of the build converts it)
int main(int argc, char *argv[])
{
    const std::string mode = argc == 2 || argc == 3 ? argv[1] : "";

    if (argc == 2 && mode == "--version")
        return printVersion();
    if (argc == 2 && mode == "--sandbox-report")
        return printSandboxReport();
    if (argc == 2 && mode == "--serve")
    {
        const gdalutil::Runtime runtime;
        const auto executor = gdalutil::makePlatformExecutor();
        return gdalutil::serve(*executor, 0, runtime.responseFd());
    }
    if (argc == 3 && mode == "--request")
    {
        const gdalutil::Runtime runtime;
        return gdalutil::serveOne(argv[2], runtime.responseFd());
    }

    printUsage(std::cerr);
    return kUsageError;
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// gdal-util: the program that reads the files of users with GDAL (PROTOCOL.md).
//
//   gdal-util --version          the version line
//   gdal-util --serve            answer the requests on standard input, one response line each
//   gdal-util --request '<json>' run one request and write its response line
//   gdal-util --sandbox-report   what confines the workers of the program (confinement.h)

#include <iostream>
#include <string>

#include "confinement.h"
#include "executor.h"
#include "logger.h"
#include "raster_protocol.h"
#include "runtime.h"
#include "server.h"

namespace
{
    constexpr int kUsageError = 2;
    // The server does not start: strict mode, and this host lacks a layer of the sandbox
    constexpr int kSandboxRefused = 3;

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

    // The mode and one line per layer: what the workers of this host can be confined by. Exits 0
    // whatever is missing; whoever checks a host reads the lines.
    int printSandboxReport()
    {
        std::cout << gdalutil::Confinement().report() << std::flush;
        return 0;
    }

    // What a server says about its sandbox once, when it starts: a value of DDB_GDAL_SANDBOX it
    // does not know, the mode off, and the layers the host lacks. False when it must not serve.
    bool startsConfined(const gdalutil::Confinement &confinement)
    {
        if (!confinement.modeWarning().empty())
        {
            LOGW << confinement.modeWarning();
        }
        if (confinement.mode() == sandbox::Mode::Off)
        {
            LOGW << gdalutil::kSandboxEnv << "=off: the workers are not confined by the sandbox layers";
        }

        const gdalutil::Confinement::Outcome outcome = confinement.startServer();
        if (!outcome.refusal.empty())
        {
            std::cerr << "gdal-util: " << outcome.refusal << std::endl;
            return false;
        }
        if (!outcome.warning.empty())
        {
            LOGW << outcome.warning;
        }
        return true;
    }
} // namespace

// On Windows argv is in the ANSI code page, in which UTF-8 text arrives as other bytes: the client
// writes the request of --request in ASCII, every other character as a \u escape of JSON
// (PROTOCOL.md)
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
        const gdalutil::Confinement confinement;
        if (!startsConfined(confinement))
            return kSandboxRefused;
        const auto executor = gdalutil::makePlatformExecutor(confinement, runtime.responseFd());
        return gdalutil::serve(*executor, 0, runtime.responseFd());
    }
    if (argc == 3 && mode == "--request")
    {
        const gdalutil::Runtime runtime;
        const gdalutil::Confinement confinement;
        if (!confinement.modeWarning().empty())
        {
            LOGW << confinement.modeWarning();
        }
        return gdalutil::serveOne(argv[2], runtime.responseFd(), confinement);
    }

    printUsage(std::cerr);
    return kUsageError;
}

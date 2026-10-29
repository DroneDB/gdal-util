/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_CONFINEMENT_H
#define GDAL_UTIL_CONFINEMENT_H

#include <cstdint>
#include <string>
#include <vector>

#include "raster_protocol.h"
#include "sandbox/sandbox.h"

namespace gdalutil
{
    // The environment variable that chooses how strictly the processes that read files are
    // confined: strict, best-effort (the default) or off (README.md)
    inline constexpr const char *kSandboxEnv = "DDB_GDAL_SANDBOX";

    // What a process that reads the file of a request may do (the sandbox library applies it),
    // and the mode that says what to do when the host cannot confine it as asked.
    //
    // Reads: the folder of the file and the folders its request names (the source and the sidecar
    // of a drawing); the data of GDAL and PROJ, the data libddb ships and the program's own
    // folders. Writes: the folder of the output the request names, which the client made for it.
    // Limits: cpu time below the time the client waits, address space as a share of the memory of
    // the machine, the size of the files written, and the open files, each by the operation.
    class Confinement
    {
      public:
        // Reads the mode from the environment and collects what every request reads (the folders
        // of the data, of the program and of the libraries it loaded). Create it before the
        // process is confined.
        Confinement();

        sandbox::Mode mode() const { return mode_; }

        // The policy of a request. Never throws: a param that cannot be read adds nothing, and
        // the operation refuses the request later.
        sandbox::Policy policyFor(const ddb::raster::Request &request) const;

        // The policy of a server on Windows, which runs the requests itself: the limits that hold
        // for the life of the process, and no folder of a request
        sandbox::Policy serverPolicy() const;

        // What happened when a process confined itself
        struct Outcome
        {
            // Why the process must not go on, empty when it may (strict and a layer is missing,
            // or a folder of the policy cannot be confined)
            std::string refusal;
            // What is missing in best-effort mode, empty when nothing is: for the one warning a
            // process logs
            std::string warning;
        };

        // Confines the calling process for the request. Call it first in a process that was
        // just started or forked for the request, before it opens the file.
        Outcome apply(const ddb::raster::Request &request) const;

        // What a server does before it serves. Where it forks a worker for each request (POSIX)
        // it checks that this host offers the layers the workers will apply: the refusal of
        // strict mode when one is missing, the warning of best-effort mode. Where it runs the
        // requests itself (Windows) it confines itself, for its whole life.
        Outcome startServer() const;

        // What was wrong with the value of the environment variable (empty when nothing)
        const std::string &modeWarning() const { return modeWarning_; }

        // `--sandbox-report`: the mode and one line per layer
        std::string report() const;

      private:
        Outcome outcomeOf(const sandbox::Report &report) const;

        // Declared first: reading the mode fills it
        std::string modeWarning_;
        sandbox::Mode mode_;
        // The memory of the machine or of its container, found once before any worker is forked
        uint64_t memoryBytes_;
        std::vector<std::string> readPaths_;
    };
} // namespace gdalutil

#endif

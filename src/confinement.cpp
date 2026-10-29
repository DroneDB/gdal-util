/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "confinement.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "gdal_inc.h"
#include "mio.h"
#include "raster_helper.h"
#include "tool_run_support.h"

namespace gdalutil
{
    namespace
    {
        using namespace ddb;
        namespace op = raster::op;
        using std::chrono::seconds;

        constexpr uint64_t kMiB = 1024ULL * 1024;
        constexpr uint64_t kGiB = 1024 * kMiB;
        constexpr uint64_t kTiB = 1024 * kGiB;

        // The cpu time of a worker ends a little before the client gives up on it, so the worker
        // ends with the limit error and the client has not killed it as a hung one
        constexpr seconds kQuickCpuMargin{5};
        constexpr seconds kConversionCpuMargin{10};

        // What an operation may use. The address space is a share of the memory of the machine
        // (of its control group in a container), with a floor: the limit counts everything
        // mapped, the libraries and the stacks included, not only what is used.
        struct Limits
        {
            uint64_t cpuSeconds;
            unsigned memoryShareNumerator;
            unsigned memoryShareDenominator;
            uint64_t memoryFloorBytes;
            uint64_t fileBytes; // the largest file the operation writes
            uint64_t openFiles;
        };

        // probe: reads and writes nothing, the descriptors of a file and its sidecars
        // thumbnail: a WebP the client accepts up to 64 MiB (raster_helper.h)
        Limits probeLimits()
        {
            return {static_cast<uint64_t>((raster::kProbeTimeout - kQuickCpuMargin).count()), 1, 4, 2 * kGiB, 16 * kMiB, 256};
        }
        Limits thumbnailLimits()
        {
            return {static_cast<uint64_t>((raster::kThumbnailTimeout - kQuickCpuMargin).count()), 1, 4, 2 * kGiB,
                    raster::kMaxThumbnailBytes, 256};
        }
        // normalize: a GeoTIFF of up to 1e9 pixels; geoproject: the photo as a GeoTIFF
        Limits normalizeLimits()
        {
            return {static_cast<uint64_t>((raster::kConversionTimeout - kConversionCpuMargin).count()), 1, 2, 4 * kGiB,
                    128 * kGiB, 1024};
        }
        Limits geoprojectLimits()
        {
            Limits limits = normalizeLimits();
            limits.fileBytes = 16 * kGiB;
            return limits;
        }
        // cog and vector run as long as the build takes (the client ends them at its maximum run
        // time) and use several threads: the cpu time of a process is the sum of its threads. The
        // size of a file is bounded only to stop a runaway one.
        Limits buildLimits(uint64_t threads)
        {
            const uint64_t runtime = static_cast<uint64_t>(
                std::chrono::duration_cast<seconds>(
                    tools::toolMaxRuntime(raster::kGdalUtilMaxSecondsEnv, raster::kGdalUtilDefaultMaxRuntime))
                    .count());
            return {runtime * std::max<uint64_t>(threads, 1), 3, 4, 4 * kGiB, kTiB, 4096};
        }

        uint64_t hardwareThreads()
        {
            return std::max(1u, std::thread::hardware_concurrency());
        }

        // The request params are read by the protocol code, which throws on a bad one: the
        // operation answers that later, and the policy then just has no use of the param
        template <typename Read> auto tolerant(Read &&read) -> decltype(read())
        {
            try
            {
                return read();
            }
            catch (const raster::ProtocolError &)
            {
                return {};
            }
        }

        Limits limitsFor(const raster::Request &request)
        {
            if (request.op == op::kProbe)
                return probeLimits();
            if (request.op == op::kThumbnail)
                return thumbnailLimits();
            if (request.op == op::kGeoproject)
                return geoprojectLimits();
            if (request.op == op::kCog)
            {
                const auto params = tolerant([&] { return raster::cogParamsFromJson(request.params); });
                return buildLimits(std::max<uint64_t>(hardwareThreads(), static_cast<uint64_t>(std::max(0, params.threads.value_or(0)))));
            }
            if (request.op == op::kVector)
                return buildLimits(hardwareThreads());
            return normalizeLimits();
        }

        // ---- the memory of the machine

        uint64_t physicalMemoryBytes()
        {
#ifdef _WIN32
            MEMORYSTATUSEX status{};
            status.dwLength = sizeof(status);
            return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#else
            const long pages = sysconf(_SC_PHYS_PAGES);
            const long pageSize = sysconf(_SC_PAGE_SIZE);
            return pages > 0 && pageSize > 0 ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize) : 0;
#endif
        }

        // The memory a container may use (control groups 2 and 1), 0 when there is no limit
        uint64_t controlGroupMemoryBytes()
        {
#ifdef __linux__
            for (const char *file : {"/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory/memory.limit_in_bytes"})
            {
                std::ifstream in(file);
                uint64_t bytes = 0;
                if (in >> bytes && bytes > 0)
                    return bytes;
            }
#endif
            return 0;
        }

        uint64_t availableMemoryBytes()
        {
            const uint64_t physical = physicalMemoryBytes();
            const uint64_t group = controlGroupMemoryBytes();
            if (physical == 0)
                return group;
            return group == 0 ? physical : std::min(physical, group);
        }

        void applyMemoryLimit(sandbox::Policy &policy, const Limits &limits, uint64_t availableBytes)
        {
            policy.maxMemoryBytes = std::max(limits.memoryFloorBytes,
                                             availableBytes / limits.memoryShareDenominator * limits.memoryShareNumerator);
        }

        // ---- the folders a request reads

        std::string utf8(const fs::path &path)
        {
            return io::toUtf8(path);
        }

        // The folder that holds the file; a file in the root of the file system is allowed alone,
        // since the root would be everything
        fs::path folderOf(const fs::path &file)
        {
            const fs::path folder = file.parent_path();
            return folder == file.root_path() ? file : folder;
        }

        void add(std::vector<std::string> &paths, const fs::path &path)
        {
            if (path.empty())
                return;
            const std::string text = utf8(path);
            if (std::find(paths.begin(), paths.end(), text) == paths.end())
                paths.push_back(text);
        }

        void addList(std::vector<std::string> &paths, const char *list)
        {
#ifdef _WIN32
            const char separator = ';';
#else
            const char separator = ':';
#endif
            const std::string text = list != nullptr ? list : "";
            size_t start = 0;
            while (start <= text.size())
            {
                const size_t end = text.find(separator, start);
                add(paths, io::fromUtf8(text.substr(start, end == std::string::npos ? std::string::npos : end - start)));
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
        }

        // The folders of the files libddb ships, where its lookup finds them (next to the
        // program, or in the folder of the package): the databases of PROJ and the sensors, the
        // time zones and the data of GDAL
        void addDataFolders(std::vector<std::string> &paths)
        {
            for (const char *marker : {"proj.db", "sensor_data.sqlite", "timezone21.bin", "gdal/gdalvrt.xsd"})
                if (const fs::path found = io::getDataPath(marker); !found.empty())
                    add(paths, found.parent_path());
        }

        // The value of an environment variable, none when it is unset or empty
        std::optional<std::string> environment(const char *name)
        {
            auto value = io::getEnvUtf8(name);
            if (!value || value->empty())
                return std::nullopt;
            return value;
        }

        sandbox::Mode modeFromEnvironment(std::string *warning)
        {
            const auto value = environment(kSandboxEnv);
            return sandbox::parseMode(value ? value->c_str() : nullptr, warning);
        }

        // Where PROJ and GDAL look for their data when the host named it
        void addConfiguredData(std::vector<std::string> &paths)
        {
            addList(paths, CPLGetConfigOption("GDAL_DATA", nullptr));
            addList(paths, CPLGetConfigOption("PROJ_DATA", nullptr));
            addList(paths, CPLGetConfigOption("PROJ_LIB", nullptr));
            // The user directory of PROJ holds grids and its settings; it is read, never written
            // (PROJ_NETWORK is off, so nothing is downloaded into it)
            if (const auto user = environment("PROJ_USER_WRITABLE_DIRECTORY"))
                add(paths, io::fromUtf8(*user));
            else if (const auto data = environment("XDG_DATA_HOME"))
                add(paths, io::fromUtf8(*data) / "proj");
            else if (const auto home = environment("HOME"))
            {
                add(paths, io::fromUtf8(*home) / ".local" / "share" / "proj");
                add(paths, io::fromUtf8(*home) / "Library" / "Application Support" / "proj");
            }
        }

#ifdef __linux__
        // The folders of the libraries the program has loaded: the C library loads more of its
        // own (character sets, name services) from the folder it lives in, later on
        void addLoadedLibraryFolders(std::vector<std::string> &paths)
        {
            std::ifstream maps("/proc/self/maps");
            std::string line;
            while (std::getline(maps, line))
            {
                const size_t slash = line.find('/');
                if (slash == std::string::npos)
                    continue;
                std::string file = line.substr(slash);
                if (file.size() > 10 && file.compare(file.size() - 10, 10, " (deleted)") == 0)
                    continue;
                add(paths, io::fromUtf8(file).parent_path());
            }
        }
#endif

        // What every process reads whatever its request: the system, not the data of anyone
        void addSystemPaths(std::vector<std::string> &paths)
        {
            for (const char *path : {"/proc/self", "/dev/null", "/dev/urandom", "/etc/localtime", "/usr/share/zoneinfo"})
                add(paths, path);
        }

        // ---- the folders of a request

        // The file and the folders around it that the operation reads
        void addRequestReads(std::vector<std::string> &paths, const raster::Request &request)
        {
            add(paths, folderOf(request.path));
            // The drawing a conversion was made from, with its coordinate system file next to it
            if (request.op == op::kProbe)
            {
                const auto params = tolerant([&] { return raster::probeParamsFromJson(request.params); });
                if (!params.source.empty())
                    add(paths, folderOf(params.source));
                if (!params.sidecar.empty())
                    add(paths, folderOf(params.sidecar));
            }
            else if (request.op == op::kVector)
            {
                const auto params = tolerant([&] { return raster::vectorParamsFromJson(request.params); });
                if (!params.source.empty())
                    add(paths, folderOf(params.source));
            }
        }

        // The folder the operation writes in: the one the client made for the file it asks for,
        // or the folder itself for the operation whose output is one
        fs::path outputFolder(const raster::Request &request)
        {
            if (request.out.empty())
                return {};
            return request.op == op::kVector ? request.out : request.out.parent_path();
        }
    } // namespace

    Confinement::Confinement() : mode_(modeFromEnvironment(&modeWarning_)), memoryBytes_(availableMemoryBytes())
    {
        addDataFolders(readPaths_);
        add(readPaths_, io::getExeFolderPath());
        addConfiguredData(readPaths_);
#ifdef __linux__
        addLoadedLibraryFolders(readPaths_);
#endif
        addSystemPaths(readPaths_);
    }

    sandbox::Policy Confinement::policyFor(const raster::Request &request) const
    {
        sandbox::Policy policy;
        policy.readPaths = readPaths_;
        addRequestReads(policy.readPaths, request);
        if (const fs::path folder = outputFolder(request); !folder.empty())
            policy.writePaths.push_back(utf8(folder));

        const Limits limits = limitsFor(request);
        applyMemoryLimit(policy, limits, memoryBytes_);
        policy.maxCpuSeconds = limits.cpuSeconds;
        policy.maxFileBytes = limits.fileBytes;
        policy.maxOpenFiles = limits.openFiles;
        return policy;
    }

    sandbox::Policy Confinement::serverPolicy() const
    {
        // The cpu time of a process is for its whole life, and the server lives for many
        // requests: the cpu time of each request is the watchdog's (executor.cpp)
        sandbox::Policy policy;
        applyMemoryLimit(policy, normalizeLimits(), memoryBytes_);
        return policy;
    }

    Confinement::Outcome Confinement::outcomeOf(const sandbox::Report &report) const
    {
        Outcome outcome;
        if (!report.ok)
            outcome.refusal = report.error;
        else if (mode_ == sandbox::Mode::BestEffort)
            outcome.warning = report.warning();
        return outcome;
    }

    Confinement::Outcome Confinement::apply(const raster::Request &request) const
    {
        return outcomeOf(sandbox::apply(policyFor(request), mode_));
    }

    Confinement::Outcome Confinement::startServer() const
    {
#ifdef _WIN32
        return outcomeOf(sandbox::apply(serverPolicy(), mode_));
#else
        if (mode_ == sandbox::Mode::Off)
            return {};
        sandbox::Report report = sandbox::probe();
        const std::string missing = report.warning();
        if (mode_ == sandbox::Mode::Strict && !missing.empty())
        {
            report.ok = false;
            report.error = std::string(kSandboxEnv) + "=strict: " + missing;
        }
        report.mode = mode_;
        return outcomeOf(report);
#endif
    }

    std::string Confinement::report() const
    {
        return std::string("mode: ") + sandbox::modeName(mode_) + "\n" + sandbox::probe().describe();
    }
} // namespace gdalutil

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "dispatch.h"

#include <exception>
#include <functional>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>

#include "logger.h"
#include "memory_watch.h"
#include "mio.h"
#include "raster_ops.h"

namespace gdalutil
{
    namespace
    {
        using namespace ddb;
        using raster::ErrorCode;
        using raster::Request;
        using raster::Response;

        // The file the request names; the helper never resolves a path against its working
        // directory, which is whatever the host started it in
        const fs::path &requiredPath(const fs::path &path, const char *member)
        {
            if (path.empty())
                throw raster::ProtocolError(std::string("the request has no ") + member);
            if (!path.is_absolute())
                throw raster::ProtocolError(std::string("'") + member + "' is not an absolute path");
            return path;
        }

        json probe(const Request &request)
        {
            const auto params = raster::probeParamsFromJson(request.params);
            return raster::toJson(raster::probe(requiredPath(request.path, "path"), params));
        }

        json thumbnail(const Request &request)
        {
            const auto params = raster::thumbnailParamsFromJson(request.params);
            const fs::path &out = requiredPath(request.out, "out");
            raster::thumbnail(requiredPath(request.path, "path"), out, params);
            std::error_code ec;
            const auto bytes = fs::file_size(out, ec);
            return {{"bytes", ec ? 0 : static_cast<uint64_t>(bytes)}};
        }

        // Each of the operations below writes the file (or folder) `out` names; the host checks
        // it and moves it where it belongs
        json cog(const Request &request, const raster::ProgressCallback &progress)
        {
            const auto params = raster::cogParamsFromJson(request.params);
            raster::buildCogFile(requiredPath(request.path, "path"), requiredPath(request.out, "out"), params, progress);
            return json::object();
        }

        json buildVector(const Request &request)
        {
            const auto params = raster::vectorParamsFromJson(request.params);
            raster::buildVectorFiles(requiredPath(request.path, "path"), requiredPath(request.out, "out"), params);
            return json::object();
        }

        json normalize(const Request &request)
        {
            raster::normalizeRaster(requiredPath(request.path, "path"), requiredPath(request.out, "out"));
            return json::object();
        }

        json geoproject(const Request &request)
        {
            const auto params = raster::geoprojectParamsFromJson(request.params);
            raster::geoprojectImage(requiredPath(request.path, "path"), requiredPath(request.out, "out"), params);
            return json::object();
        }

        using Operation = std::function<json()>;

        // The operation the request names, ready to run. It refuses at once what cannot run in
        // this mode whatever the files are, so that a request that does nothing is answered
        // before anything is done for it (confining the process included).
        Operation operationOf(const Request &request, const raster::ProgressCallback *progress)
        {
            if (request.op == raster::op::kProbe)
                return [&request] { return probe(request); };
            if (request.op == raster::op::kThumbnail)
                return [&request] { return thumbnail(request); };
            if (request.op == raster::op::kNormalize)
                return [&request] { return normalize(request); };
            if (request.op == raster::op::kGeoproject)
                return [&request] { return geoproject(request); };
            if (request.op == raster::op::kCog || request.op == raster::op::kVector)
            {
                // The builds take as long as they take: a process each, which the client waits for
                // without a deadline of its own (PROTOCOL.md)
                if (progress == nullptr)
                    throw raster::ProtocolError("the operation '" + request.op + "' runs only with --request");
                if (request.op == raster::op::kCog)
                    return [&request, progress] { return cog(request, *progress); };
                return [&request] { return buildVector(request); };
            }
            throw raster::ProtocolError("unknown operation '" + request.op + "'");
        }

        // The response of the operation of the request, run after `beforeRun` (when there is one)
        // has prepared the process. Never throws.
        Response respond(const Request &request, const raster::ProgressCallback *progress,
                         const std::function<void()> &beforeRun)
        {
            try
            {
                const Operation operation = operationOf(request, progress);
                if (beforeRun)
                    beforeRun();
                return Response::success(request.id, operation());
            }
            catch (const std::bad_alloc &)
            {
                return Response::failure(request.id, ErrorCode::Limit, "out of memory");
            }
            catch (const std::exception &e)
            {
                ErrorCode code = raster::classify(e);
                // GDAL does not throw when the memory limit of the sandbox refuses an allocation:
                // it reports it and the operation fails for want of a buffer. That is the limit.
                if ((code == ErrorCode::Unreadable || code == ErrorCode::Internal) && memory_watch::seen())
                    return Response::failure(request.id, ErrorCode::Limit,
                                             std::string("the worker ran out of memory: ") + e.what());
                return Response::failure(request.id, code, e.what());
            }
            catch (...)
            {
                return Response::failure(request.id, ErrorCode::Internal, "unknown failure");
            }
        }
    } // namespace

    Response handleConfined(const Confinement &confinement, const Request &request,
                            const raster::ProgressCallback *progress, bool logWarning)
    {
        return respond(request, progress, [&] {
            const Confinement::Outcome outcome = confinement.apply(request);
            if (!outcome.refusal.empty())
            {
                LOGE << "Cannot run the request: " << outcome.refusal;
                throw std::runtime_error("cannot confine the process: " + outcome.refusal);
            }
            if (logWarning && !outcome.warning.empty())
            {
                LOGW << outcome.warning;
            }
        });
    }

    Response handleRequest(const Request &request, const raster::ProgressCallback *progress)
    {
        return respond(request, progress, nullptr);
    }
} // namespace gdalutil

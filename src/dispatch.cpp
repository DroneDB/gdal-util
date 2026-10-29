/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "dispatch.h"

#include <exception>
#include <new>
#include <string>
#include <system_error>

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

        json geoproject(const Request &request)
        {
            const auto params = raster::geoprojectParamsFromJson(request.params);
            raster::geoprojectImage(requiredPath(request.path, "path"), requiredPath(request.out, "out"), params);
            return json::object();
        }

        json run(const Request &request, const raster::ProgressCallback *progress)
        {
            if (request.op == raster::op::kProbe)
                return probe(request);
            if (request.op == raster::op::kThumbnail)
                return thumbnail(request);
            if (request.op == raster::op::kGeoproject)
                return geoproject(request);
            if (request.op == raster::op::kCog || request.op == raster::op::kVector)
            {
                // The builds take as long as they take: a process each, which the client waits for
                // without a deadline of its own (PROTOCOL.md)
                if (progress == nullptr)
                    throw raster::ProtocolError("the operation '" + request.op + "' runs only with --request");
                return request.op == raster::op::kCog ? cog(request, *progress) : buildVector(request);
            }
            if (request.op == raster::op::kNormalize)
                throw raster::ProtocolError("the operation '" + request.op + "' is not available in this version");
            throw raster::ProtocolError("unknown operation '" + request.op + "'");
        }
    } // namespace

    Response handleRequest(const Request &request, const raster::ProgressCallback *progress)
    {
        try
        {
            return Response::success(request.id, run(request, progress));
        }
        catch (const std::bad_alloc &)
        {
            return Response::failure(request.id, ErrorCode::Limit, "out of memory");
        }
        catch (const std::exception &e)
        {
            return Response::failure(request.id, raster::classify(e), e.what());
        }
        catch (...)
        {
            return Response::failure(request.id, ErrorCode::Internal, "unknown failure");
        }
    }
} // namespace gdalutil

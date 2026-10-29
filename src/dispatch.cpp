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

        json run(const Request &request)
        {
            if (request.op == raster::op::kProbe)
                return probe(request);
            if (request.op == raster::op::kThumbnail)
                return thumbnail(request);
            if (request.op == raster::op::kNormalize || request.op == raster::op::kGeoproject ||
                request.op == raster::op::kCog || request.op == raster::op::kVector)
                throw raster::ProtocolError("the operation '" + request.op + "' is not available in this version");
            throw raster::ProtocolError("unknown operation '" + request.op + "'");
        }
    } // namespace

    Response handleRequest(const Request &request)
    {
        try
        {
            return Response::success(request.id, run(request));
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

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "server.h"

#include <optional>
#include <string>

#include "dispatch.h"
#include "fd_io.h"
#include "logger.h"
#include "raster_protocol.h"

namespace gdalutil
{
    namespace
    {
        using namespace ddb;
        using raster::ErrorCode;
        using raster::Response;

        // The response to a line: the request's, or the reason it is not one
        template <typename Run>
        Response respondTo(const std::string &line, Run &&run)
        {
            try
            {
                return run(raster::decodeRequest(line));
            }
            catch (const raster::ProtocolError &e)
            {
                LOGW << "Request refused: " << e.what();
                return Response::failure(raster::requestIdOf(line), ErrorCode::Internal,
                                         std::string("not a valid request: ") + e.what());
            }
        }
    } // namespace

    int serve(Executor &executor, int requestFd, int responseFd)
    {
        LineReader reader(requestFd, raster::kMaxLineBytes);
        std::string line;
        for (;;)
        {
            Response response;
            switch (reader.next(line))
            {
            case LineReader::Status::Eof:
                return 0;
            case LineReader::Status::TooLong:
                response = Response::failure(std::nullopt, ErrorCode::Internal, "the request line is longer than 16 MiB");
                break;
            case LineReader::Status::Line:
                if (line.empty())
                    continue;
                response = respondTo(line, [&](const raster::Request &request) { return executor.execute(request); });
                break;
            }
            if (!writeLine(responseFd, raster::encodeResponse(response)))
            {
                LOGW << "The host stopped reading the responses";
                return 1;
            }
        }
    }

    int serveOne(const char *requestLine, int responseFd, const Confinement &confinement)
    {
        // The progress of a long operation goes to the host as a line of its own, before the response
        const raster::ProgressCallback progress = [responseFd](double fraction) {
            writeLine(responseFd, raster::encodeProgress(fraction));
        };
        const Response response = respondTo(requestLine, [&](const raster::Request &request) {
            return handleConfined(confinement, request, &progress, true);
        });
        if (!writeLine(responseFd, raster::encodeResponse(response)))
            return 1;
        return response.ok ? 0 : 1;
    }
} // namespace gdalutil

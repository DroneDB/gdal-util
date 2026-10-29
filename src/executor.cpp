/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "executor.h"

#include <cstring>
#include <optional>
#include <string>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

#include "dispatch.h"
#include "fd_io.h"
#include "logger.h"
#include "thread_count.h"

namespace gdalutil
{
    namespace
    {
        using namespace ddb;
        using raster::ErrorCode;
        using raster::Request;
        using raster::Response;

#ifdef _WIN32
        // Windows has no fork: the server runs the request itself
        class InProcessExecutor : public Executor
        {
          public:
            Response execute(const Request &request) override { return handleRequest(request); }
        };
#else
        // The exit code of a worker that could not write its response
        constexpr int kWorkerFailed = 70;

        // The response line of a worker, never longer than the protocol allows: a result that
        // does not fit is a failure of the limit kind, so the host's line limit is never hit
        std::string responseLine(const Response &response)
        {
            std::string line = raster::encodeResponse(response);
            if (line.size() > raster::kMaxLineBytes)
                line = raster::encodeResponse(
                    Response::failure(response.id, ErrorCode::Limit, "the response is longer than the protocol allows"));
            return line;
        }

        // What happened to a worker that left no response
        Response failureOf(const Request &request, int status)
        {
            if (WIFSIGNALED(status))
            {
                const int signal = WTERMSIG(status);
                const char *described = strsignal(signal);
                const std::string name = described != nullptr ? described : "unknown signal";
                // The cpu time limit of a worker ends it with SIGXCPU
                if (signal == SIGXCPU)
                    return Response::failure(request.id, ErrorCode::Limit, "the worker used more cpu time than allowed");
                return Response::failure(request.id, ErrorCode::Crashed,
                                         "the worker was killed by signal " + std::to_string(signal) + " (" + name + ")");
            }
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                return Response::failure(request.id, ErrorCode::Crashed,
                                         "the worker ended with exit code " + std::to_string(WEXITSTATUS(status)));
            return Response::failure(request.id, ErrorCode::Crashed, "the worker ended without a response");
        }

        // The worker: handles the one request, writes the response to the pipe and ends without
        // running any exit handler (the state it inherited is not its own to clean up)
        [[noreturn]] void runWorker(const Request &request, int pipeFd, pid_t serverPid)
        {
#ifdef __linux__
            // A server that dies takes its worker along; the check closes the window between
            // the fork and this call
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != serverPid)
                _exit(kWorkerFailed);
#else
            (void)serverPid;
#endif
            // The worker serves no further request: standard input is not its to read
            const int devNull = open("/dev/null", O_RDONLY);
            if (devNull < 0 || dup2(devNull, STDIN_FILENO) < 0)
                _exit(kWorkerFailed);
            close(devNull);

            try
            {
                const bool written = writeAll(pipeFd, responseLine(handleRequest(request)) + "\n");
                _exit(written ? 0 : kWorkerFailed);
            }
            catch (...)
            {
                _exit(kWorkerFailed);
            }
        }

        // One worker per request, forked from a server that has no thread: a fork in a process
        // with several threads copies only the one that forks, and a lock another thread held
        // stays locked forever in the child
        class ForkExecutor : public Executor
        {
          public:
            Response execute(const Request &request) override
            {
                if (const auto threads = threadCount(); threads && *threads != 1)
                {
                    LOGE << "The server has " << *threads << " threads; it forks only from a single thread";
                    return Response::failure(request.id, ErrorCode::Internal,
                                             "the helper has " + std::to_string(*threads) +
                                                 " threads and does not fork");
                }

                int fds[2];
                if (pipe(fds) != 0)
                    return Response::failure(request.id, ErrorCode::Internal,
                                             std::string("cannot create a pipe: ") + std::strerror(errno));

                const pid_t serverPid = getpid();
                const pid_t pid = fork();
                if (pid < 0)
                {
                    const std::string reason = std::strerror(errno);
                    close(fds[0]);
                    close(fds[1]);
                    return Response::failure(request.id, ErrorCode::Internal, "cannot fork a worker: " + reason);
                }
                if (pid == 0)
                {
                    close(fds[0]);
                    runWorker(request, fds[1], serverPid);
                }

                close(fds[1]);
                bool tooLong = false;
                // One byte over the limit tells a line that is too long from one that fits
                const std::string output = readAll(fds[0], raster::kMaxLineBytes + 2, tooLong);
                close(fds[0]);
                if (tooLong)
                    kill(pid, SIGKILL);

                int status = 0;
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                {
                }

                if (tooLong || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                    return failureOf(request, status);
                return verified(request, output);
            }

          private:
            // A worker that ended well wrote one complete response line for this request
            static Response verified(const Request &request, const std::string &output)
            {
                if (output.empty() || output.back() != '\n')
                    return Response::failure(request.id, ErrorCode::Crashed, "the worker ended without a response");
                try
                {
                    Response response = raster::decodeResponse(output);
                    if (response.id != std::optional<int64_t>(request.id))
                        return Response::failure(request.id, ErrorCode::Crashed, "the worker answered another request");
                    return response;
                }
                catch (const raster::ProtocolError &e)
                {
                    return Response::failure(request.id, ErrorCode::Crashed,
                                             std::string("the worker wrote a response that is not valid: ") + e.what());
                }
            }
        };
#endif
    } // namespace

    std::unique_ptr<Executor> makePlatformExecutor()
    {
#ifdef _WIN32
        return std::make_unique<InProcessExecutor>();
#else
        return std::make_unique<ForkExecutor>();
#endif
    }
} // namespace gdalutil

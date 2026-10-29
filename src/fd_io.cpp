/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "fd_io.h"

#include <algorithm>
#include <cerrno>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace gdalutil
{
    namespace
    {
        constexpr size_t kChunkBytes = 64 * 1024;

#ifdef _WIN32
        long long readSome(int fd, void *buffer, size_t count)
        {
            return _read(fd, buffer, static_cast<unsigned int>(std::min<size_t>(count, 1u << 30)));
        }
        long long writeSome(int fd, const void *buffer, size_t count)
        {
            return _write(fd, buffer, static_cast<unsigned int>(std::min<size_t>(count, 1u << 30)));
        }
        int duplicate(int fd) { return _dup(fd); }
        int duplicateOnto(int from, int to) { return _dup2(from, to); }
        void makeBinary(int fd) { _setmode(fd, _O_BINARY); }
#else
        long long readSome(int fd, void *buffer, size_t count) { return ::read(fd, buffer, count); }
        long long writeSome(int fd, const void *buffer, size_t count) { return ::write(fd, buffer, count); }
        int duplicate(int fd) { return ::dup(fd); }
        int duplicateOnto(int from, int to) { return ::dup2(from, to); }
        void makeBinary(int) {}
#endif
    } // namespace

    LineReader::LineReader(int fd, size_t maxLineBytes) : fd_(fd), maxLineBytes_(maxLineBytes)
    {
        makeBinary(fd);
    }

    bool LineReader::fill()
    {
        if (eof_)
            return false;
        char chunk[kChunkBytes];
        for (;;)
        {
            const long long n = readSome(fd_, chunk, sizeof(chunk));
            if (n > 0)
            {
                buffer_.append(chunk, static_cast<size_t>(n));
                return true;
            }
            if (n < 0 && errno == EINTR)
                continue;
            eof_ = true;
            return false;
        }
    }

    LineReader::Status LineReader::next(std::string &line)
    {
        bool dropping = false;
        size_t scanned = 0;
        for (;;)
        {
            const size_t lf = buffer_.find('\n', scanned);
            if (lf != std::string::npos)
            {
                std::string found = buffer_.substr(0, lf);
                buffer_.erase(0, lf + 1);
                if (dropping || found.size() > maxLineBytes_)
                    return Status::TooLong;
                if (!found.empty() && found.back() == '\r')
                    found.pop_back();
                line = std::move(found);
                return Status::Line;
            }
            scanned = buffer_.size();

            // No line feed yet: past the limit the line is dropped as it comes, never kept
            if (buffer_.size() > maxLineBytes_)
            {
                dropping = true;
                buffer_.clear();
                scanned = 0;
            }
            if (!fill())
            {
                if (dropping)
                {
                    buffer_.clear();
                    return Status::TooLong;
                }
                if (buffer_.empty())
                    return Status::Eof;
                line = std::move(buffer_);
                buffer_.clear();
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return Status::Line;
            }
        }
    }

    bool writeAll(int fd, std::string_view text)
    {
        while (!text.empty())
        {
            const long long n = writeSome(fd, text.data(), text.size());
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            text.remove_prefix(static_cast<size_t>(n));
        }
        return true;
    }

    bool writeLine(int fd, std::string_view line)
    {
        std::string text;
        text.reserve(line.size() + 1);
        text.append(line);
        text.push_back('\n');
        return writeAll(fd, text);
    }

    std::string readAll(int fd, size_t limit, bool &truncated)
    {
        truncated = false;
        std::string all;
        std::vector<char> chunk(kChunkBytes);
        for (;;)
        {
            const long long n = readSome(fd, chunk.data(), chunk.size());
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return all;
            if (all.size() + static_cast<size_t>(n) > limit)
            {
                all.append(chunk.data(), limit - all.size());
                truncated = true;
                return all;
            }
            all.append(chunk.data(), static_cast<size_t>(n));
        }
    }

    int claimStandardOutput()
    {
        const int copy = duplicate(1);
        if (copy < 0)
            return -1;
        makeBinary(copy);
        if (duplicateOnto(2, 1) < 0)
            return -1;
        return copy;
    }
} // namespace gdalutil

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_FD_IO_H
#define GDAL_UTIL_FD_IO_H

#include <cstddef>
#include <string>
#include <string_view>

// Lines on file descriptors (standard input, the pipes between the server and its workers). POSIX
// and Windows file descriptors, binary: a line ends with a line feed and nothing is translated.
namespace gdalutil
{
    // Reads the lines of a descriptor. A line longer than the limit is discarded up to its line
    // feed and reported as TooLong, so a request that is too long costs one answer, not the
    // connection.
    class LineReader
    {
      public:
        enum class Status
        {
            Line,    // a line, without its line feed (and the carriage return before it)
            TooLong, // a line longer than the limit, dropped
            Eof      // no more input; a last line without a line feed comes as a Line first
        };

        LineReader(int fd, size_t maxLineBytes);

        Status next(std::string &line);

      private:
        // Reads more into buffer_; false at end of input (or a read that fails)
        bool fill();

        int fd_;
        size_t maxLineBytes_;
        std::string buffer_;
        bool eof_ = false;
    };

    // Writes all of the text; false when the other end is gone
    bool writeAll(int fd, std::string_view text);

    // Writes the text and a line feed with one call to writeAll
    bool writeLine(int fd, std::string_view line);

    // Reads a descriptor to its end, at most limit bytes; truncated is set when there was more
    std::string readAll(int fd, size_t limit, bool &truncated);

    // Gives the program a private copy of its standard output for its responses, and points
    // standard output at standard error: whatever else writes to standard output (a library's
    // printf) cannot get into a response line. Returns the copy, -1 when it cannot be made.
    int claimStandardOutput();
} // namespace gdalutil

#endif

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef GDAL_UTIL_RUNTIME_H
#define GDAL_UTIL_RUNTIME_H

namespace gdalutil
{
    // The process of the helper while it handles requests: libddb initialised (GDAL, PROJ, the
    // data files) once, user files allowed to open, and standard output kept for the responses.
    // The destructor finalises GDAL, so create one in main and keep it to the end.
    class Runtime
    {
      public:
        Runtime();
        ~Runtime();
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        // The descriptor the response lines go to. Standard output itself is pointed at standard
        // error, so nothing but a response reaches the host.
        int responseFd() const { return responseFd_; }

      private:
        int responseFd_;
    };
} // namespace gdalutil

#endif

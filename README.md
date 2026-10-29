# gdal-util

`gdal-util` reads the files that users supply (rasters, vectors, photos) with GDAL, in its own
process, for [DroneDB](https://github.com/DroneDB/DroneDB). A file can be damaged or hostile: a
crash, a hang or a memory bomb in a parser ends a worker of this program, not the application that
loads libddb. DroneDB runs it as a child process and talks to it over a line-based JSON protocol.

```
gdal-util --version              # gdal-util <version> protocol <major>.<minor>
gdal-util --serve                # answer the requests of PROTOCOL.md on stdin/stdout
gdal-util --request '<json>'     # one request, one response line on stdout
gdal-util --sandbox-report       # what confines the workers
```

The protocol is described in [PROTOCOL.md](PROTOCOL.md).

## How it works

* `--serve` reads one request per line. On Linux and macOS it forks a worker for each request; the
  worker handles that one request, writes one response and ends, so nothing a file does to the
  memory of a parser lives on to the next file. The server itself keeps no thread when it forks. On
  Windows the server handles the requests itself.
* The operations (`probe`, `thumbnail`, `normalize`, `geoproject`, `cog`, `vector`) are part of libddb (`src/library/raster_ops*.cpp` in the
  DroneDB repository); this program holds the modes, the server loop, the worker and the dispatch of
  a request to its operation. The program contains the library (it does not load `libddb`), and the
  only place that allows the opening of user files is this program's `main`.
* Standard output carries the responses and nothing else: the program keeps a private copy of it for
  the responses and points standard output at standard error, so a library that prints cannot
  corrupt a line.

## Build

The program builds only inside the DroneDB tree: it contains libddb's objects and libraries, its
version is the version of the package, and it is placed next to the other programs of the build.
`apps/CMakeLists.txt` of DroneDB adds this folder (a git submodule); configuring this folder on its
own stops with a message that says so.

```
cmake -S <DroneDB> -B build
cmake --build build --target gdal-util
```

`DDB_GDAL_UTIL_PATH` tells libddb where to find the program when it is not next to the library.

## Licence

* The source code is licensed under the Mozilla Public License, version 2.0 (MPL-2.0), like libddb.
  See [LICENSE](LICENSE).
* [PROTOCOL.md](PROTOCOL.md) is released under CC0-1.0 (public domain): anyone can implement a
  client or a server of the protocol under any licence.

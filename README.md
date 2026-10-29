# gdal-util

`gdal-util` reads the files that users supply (rasters, vectors, photos) with GDAL, in its own
process, for [DroneDB](https://github.com/DroneDB/DroneDB). A file can be damaged or hostile: a
crash, a hang or a memory bomb in a parser ends a worker of this program, not the application that
loads libddb. DroneDB runs it as a child process and talks to it over a line-based JSON protocol.

```
gdal-util --version              # gdal-util <version> protocol <major>.<minor>
gdal-util --serve                # answer the requests of PROTOCOL.md on stdin/stdout
gdal-util --request '<json>'     # one request, one response line on stdout
gdal-util --sandbox-report       # the sandbox mode and what this host offers (release check)
```

The protocol is described in [PROTOCOL.md](PROTOCOL.md).

## How it works

* `--serve` reads one request per line. On Linux and macOS it forks a worker for each request; the
  worker handles that one request, writes one response and ends, so nothing a file does to the
  memory of a parser lives on to the next file. The server itself keeps no thread when it forks. A
  worker closes the server's copy of standard output first: what it keeps that leads to the host is
  standard error, so a worker a file took over cannot write a line the host reads as a response. On
  Windows the server handles the requests itself.
* The operations (`probe`, `thumbnail`, `normalize`, `geoproject`, `cog`, `vector`) are part of libddb (`src/library/raster_ops*.cpp` in the
  DroneDB repository); this program holds the modes, the server loop, the worker and the dispatch of
  a request to its operation. The program contains the library (it does not load `libddb`), and the
  only place that allows the opening of user files is this program's `main`.
* The host bounds the heap of glibc's allocator by the environment it starts the program with:
  `MALLOC_ARENA_MAX=2` for `--serve` (one thread a worker, many workers), and one arena per thread
  of the build plus one for the `--request` of `cog` and `vector`, whose threads would otherwise
  wait on the locks of two arenas (PROTOCOL.md section 9).
* Standard output carries the responses and nothing else: the program keeps a private copy of it for
  the responses and points standard output at standard error, so a library that prints cannot
  corrupt a line.

## Sandbox

A worker is a process that opens a file nobody trusts, so it restricts itself first, before it
opens anything (`src/confinement.cpp` decides what, the library in `vendor/sandbox` of DroneDB
does it). `DDB_GDAL_SANDBOX` chooses what happens on a host that lacks a layer:

| `DDB_GDAL_SANDBOX` | |
| --- | --- |
| `best-effort` (default) | applies the layers the host has; the server writes one warning at start that names the missing ones |
| `strict` | refuses to start (`--serve`, exit status 3) or to run (`--request`, an `internal` response) and names the missing layer |
| `off` | applies no layer; the process boundary stays. For diagnosis, never a production setting |

The layers on Linux, in the order they are applied: resource limits (`rlimits`), `oom_score_adj`
1000 so the host is not the process the kernel kills first (`oom-score`), `no_new_privs`, Landlock
(`landlock`) and a seccomp filter (`seccomp`) that refuses sockets, `execve`, a new process (a
thread is allowed), leaving the process group, namespaces, `ptrace` and the signals to other
processes. macOS has the resource limits. Windows has a Job Object and a
low integrity level. `gdal-util --sandbox-report` prints the mode and each layer as `<name>:
active|missing (<why>)` and exits 0. A build with a sanitizer, which is for tests and never
packaged, leaves out what the runtime cannot live with and says so in the detail of the layer:
no address space limit (AddressSanitizer, ThreadSanitizer and MemorySanitizer take terabytes of
it for their shadow memory), and LeakSanitizer's leak check at exit may make its one clone, use
`ptrace` and read `/proc/self/task` ("Sanitizer builds" in `vendor/sandbox/README.md` of DroneDB).

**Release check.** After installing a package, on the machine or in the container that will run
it, `gdal-util --sandbox-report` must show every layer `active`. A line that says `missing` is a
host that confines less (an old kernel, a container profile that hides Landlock or seccomp):
production then runs with the other layers and one warning in the log, or, with
`DDB_GDAL_SANDBOX=strict`, does not start the helper.

What each request is allowed:

| | |
| --- | --- |
| Read | the folder of `path`; the folders of the `source` and `sidecar` params; the folder of the program, the libraries it loaded and the data files libddb ships (`proj.db`, `sensor_data.sqlite`, `timezone21.bin`, GDAL data); `GDAL_DATA`, `PROJ_DATA` and `PROJ_LIB`; the user directory of PROJ; `/proc/self`, `/dev/null`, `/dev/urandom`, `/etc/localtime`, `/usr/share/zoneinfo` |
| Write | the folder `out` lies in (for `vector`, `out` itself), which the client made |
| Never | network sockets, `execve`, a new process (`fork`, `vfork`, a `clone` that is not a thread), `setsid` and `setpgid`, namespaces, signals to other processes |

| Operation | cpu time | address space | file written | open files |
| --- | --- | --- | --- | --- |
| `probe` | 25 s | 1/4 of the memory, at least 2 GiB | 16 MiB | 256 |
| `thumbnail` | 25 s | 1/4, at least 2 GiB | 64 MiB | 256 |
| `normalize` | 110 s | 1/2, at least 4 GiB | 128 GiB | 1024 |
| `geoproject` | 110 s | 1/2, at least 4 GiB | 16 GiB | 1024 |
| `cog`, `vector` | `DDB_GDAL_UTIL_MAX_SECONDS` (12 h) for each thread the machine has | 3/4, at least 4 GiB | 1 TiB (a runaway file) | 4096 |

The cpu time of the first four ends a little before the client's 30 s and 120 s, so the answer is
`limit` and not a worker the client killed. The memory is that of the machine, or of its control
group in a container, whichever is less; the limit counts what is mapped (libraries and stacks
too), not only what is used. None of these can be raised past what the process already has: an
operator who wants less starts the program under `ulimit` or the limits of the container. A
worker that goes past a limit is answered with `limit`: the system ends it for its cpu time
(`SIGXCPU`) or the size of a file (`SIGXFSZ`), and an allocation that fails in GDAL fails the
request with the same code. `PROJ_NETWORK` is off in the program, whatever the host says.

On Windows the server runs the requests itself, so the Job Object limits the whole life of the
process: memory, one active process, no access to the desktop. The cpu time of one request is
bounded by a thread of the server (`src/cpu_watchdog.cpp`) with the limits of the table: past
them it answers the request with `limit` and ends the server with status 4, since a request that
ran away may have left its state in any shape; the client starts another server. Its low
integrity level lets it write only in folders with a low integrity label, which the client sets on
the folder of `out`.

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

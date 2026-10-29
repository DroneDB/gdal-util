# gdal-util protocol

**Protocol version 1.0**

SPDX-License-Identifier: CC0-1.0

This document is released under CC0 1.0 Universal (public domain dedication,
<https://creativecommons.org/publicdomain/zero/1.0/>). Anyone may implement either side of the
protocol, under any licence, without asking.

The protocol is the whole interface between `gdal-util` and the programs that use it. It carries
plain data only: file paths, numbers, names, and WKT text. It carries no structures, pointers,
handles, file descriptors or shared memory names, and no request depends on an earlier one.

`gdal-util` reads files that users supplied. Treat everything it returns as data that came from
such a file: read it, never open a path or a URL in it.

## 1. Transport

`gdal-util --serve` reads requests from standard input and writes responses to standard output.

* A request or a response is one JSON object (RFC 8259), encoded as UTF-8, on one line that ends
  with a line feed (`\n`). A carriage return before the line feed is allowed in a request and
  ignored.
* The server answers every request line with exactly one response line, in the order of the
  requests, and flushes it. It handles one request at a time. A client may send several requests
  before it reads, as long as it keeps reading. An empty line is not a request and gets no answer.
* Nothing but response lines is written to standard output. Diagnostics, and whatever the
  libraries the program uses print, go to standard error, which clients should read and discard or
  log.
* The server exits with status 0 when standard input reaches end of file.
* A request line longer than 16 MiB (16 777 216 bytes) is discarded up to its line feed and
  answered with `error: "internal"` and an `id` of `null`.
* On POSIX systems the server handles each request in a worker, a child process that ends after the
  request. A worker that ends without a response (a signal, an exit code, running out of memory or
  cpu time) is answered with `error: "crashed"`, or `"limit"` when it was the limit that ended it;
  the server keeps serving. On Windows the server handles the requests itself.

## 2. Command line

| Command | Behaviour |
| --- | --- |
| `gdal-util --version` | Prints one line, `gdal-util <version> protocol <major>.<minor>`, and exits 0. `<version>` is the version of the DroneDB build that contains the program. Nothing else is initialised. |
| `gdal-util --serve` | Serves the requests of this document on standard input and output. File paths are never taken from the command line in this mode. |
| `gdal-util --request '<json>'` | Handles the one request given as the argument, in this process, writes its response line to standard output and exits 0 when the response is a success and 1 when it is a failure. Later versions write progress lines (`{"progress":<number>}`) before the response of an operation that runs long; no operation of this version does. |
| `gdal-util --sandbox-report` | Prints what confines the workers of the program. |
| anything else | Prints a usage line to standard error and exits 2. |

The version line is matched by the regular expression `protocol ([0-9]+)\.([0-9]+)$`. Clients
should match it that way and ignore the rest of the line.

## 3. Requests

Every request is an object:

| Member | Type | Meaning |
| --- | --- | --- |
| `id` | integer | A signed 64-bit integer, echoed in the response. |
| `op` | string | The operation: `probe` or `thumbnail`. The operations `normalize`, `geoproject`, `cog` and `vector` are reserved: this version answers them with `error: "internal"`. |
| `path` | string | The file to read: an absolute path. See section 6 for `path64`. |
| `out` | string | The file to write, an absolute path, for the operations that write (`thumbnail`). The folder it lies in exists and is the only place the operation writes to. See section 6 for `out64`. |
| `params` | object | The members the operation defines. A missing `params` is an empty object. |

Members a request does not define are ignored, so that later minor versions can add optional
members. A request that is not an object, has no integer `id`, no string `op`, a `params` that is
not an object, or both forms of a path, is answered with `error: "internal"`, and with the `id`
when the line has a readable one.

A relative `path` or `out` is refused with `error: "internal"`: the working directory of the
server is whatever it was started with.

### 3.1 `probe`

```json
{"id":7,"op":"probe","path":"/data/ds/ortho.tif","params":{"kind":"raster"}}
```

Reads what GDAL finds in a raster or a vector file, as far as the entry of a dataset needs it.

| `params` member | Type | Meaning |
| --- | --- | --- |
| `kind` | string | `raster` or `vector`. Required. |
| `source` | string | `vector` only: the file the user named when `path` is a conversion of it (a DWG converted to a DXF); the drawing's sidecars give its coordinate system. See section 6 for `source64`. |
| `converted` | boolean | `vector` only: `path` is a conversion of `source`. Default false. |

The result of a probe:

| Member | Type | Meaning |
| --- | --- | --- |
| `type` | string | `GeoRaster` (a raster with a projection), `Raster` (a raster without) or `Vector`. |
| `isCog` | boolean | A `GeoRaster` that is already an optimised Cloud Optimized GeoTIFF (no conversion needed); false otherwise. |
| `properties` | object | The members the entry of the file adds to its properties (see below). |
| `wkt` | string | A `GeoRaster` with a geotransform: its projection as WKT text. The entry keeps it as its `projection` property. Absent otherwise. |
| `pointGeom` | object | A GeoJSON `Point` (`coordinates`: longitude, latitude, 0), WGS84: the centre of the footprint. Absent when there is none. |
| `polygonGeom` | object | A GeoJSON `Polygon` of one closed ring, WGS84: the footprint (a raster: its corners; a vector: the envelope of the layers). Absent when there is none. |

`properties` of a `GeoRaster`: `width` and `height` (pixels), `geotransform` (six numbers, when
the raster has one) and `bands` (an array of objects with `type`, the GDAL name of the data type,
and `colorInterp`, the GDAL name of the colour interpretation). Of a `Raster`: empty. Of a
`Vector`: `vector`, an object with the `driver` and the `layers` (`name`, `geometryType`,
`featureCount`, and `extent` in WGS84 as `[minX, minY, maxX, maxY]` when the layer has one); for a
CAD drawing also `georeferenced`, and in `vector` the `crs` (`source`, `label`), `units`,
`converter` (when `converted`) and, for a drawing without a coordinate system, `localExtent`.

```json
{"id":7,"ok":true,"result":{"type":"GeoRaster","isCog":false,"properties":{"width":4096,"height":4096,"geotransform":[500000.0,0.05,0.0,5000000.0,0.0,-0.05],"bands":[{"type":"Byte","colorInterp":"Red"}]},"wkt":"PROJCS[...]","polygonGeom":{"type":"Polygon","coordinates":[[[9.1,45.2,0.0],[9.2,45.2,0.0],[9.2,45.3,0.0],[9.1,45.3,0.0],[9.1,45.2,0.0]]]},"pointGeom":{"type":"Point","coordinates":[9.15,45.25,0.0]}}}
```

A file that GDAL cannot open as what `kind` says is `error: "unreadable"`. The probe reads no EXIF
or other tags of the file.

### 3.2 `thumbnail`

```json
{"id":8,"op":"thumbnail","path":"/data/ds/photo.jpg","out":"/tmp/ddb/req-1/photo.webp","params":{"size":256}}
```

Writes the thumbnail of a photo or a raster to `out`, as a WebP file.

| `params` member | Type | Meaning |
| --- | --- | --- |
| `size` | integer | The length of the longest side in pixels. Required, greater than 0. |
| `vis` | object | The visualisation of a raster: the rendering the members choose. Without `vis` the thumbnail is the plain one of the file (the colours of an 8-bit raster; other data stretched to 0-255). With it, even empty, the bands, formula and colormap the members name are rendered. |

`vis` members, all strings and all optional: `preset`, `bands` (comma-separated band numbers),
`formula`, `bandFilter`, `colormap`, `rescale` (`min,max`).

The result is `{"bytes": <size of the file written>}`. For a JPEG that carries a preview of the
same framing at least `size` pixels long (a Multi-Picture Format preview), the thumbnail is
decoded from the preview.

## 4. Responses

A success: `{"id": <id>, "ok": true, "result": {...}}`. A failure:
`{"id": <id or null>, "ok": false, "error": "<code>", "message": "<text>"}`. The `message` is for
people (at most 2000 bytes; bytes that are not valid UTF-8 are replaced). An `id` of `null` means the
request had none that could be read.

| `error` | Meaning |
| --- | --- |
| `unreadable` | The file cannot be opened or read as what the request needs. |
| `policy` | The rules for opening user files refuse the file. |
| `limit` | A limit of the worker was reached: memory, cpu time, file size, or the 16 MiB of a response line. |
| `crashed` | The worker ended without a response. |
| `timeout` | The client's deadline passed. Never written by the program; clients use it for their own timeouts. |
| `nospace` | The device `out` lies on is full. |
| `internal` | Anything else: a request that is not valid, an operation that does not exist or is not available, a defect. |

A client treats a response line longer than 16 MiB, a line that is not valid JSON, or a response
with an `id` it did not ask for as a broken program.

## 5. Limits

| Item | Limit |
| --- | --- |
| Request line, response line | 16 MiB |
| `message` of a failure | 2000 bytes |
| Wall time of `probe` and `thumbnail` | 30 s (the client's timeout) |

## 6. Text that is not UTF-8

File names are bytes, not always text. JSON strings must be valid UTF-8, so a path that is not
stays intact like this: the member `<name>` is replaced by `<name>64`, holding the standard base64
(RFC 4648 section 4, with padding) of the raw bytes. This applies to `path`, `out` and the `source`
param, and works in both directions: a receiver accepts both forms (exactly one of the two); a
sender uses the plain form when the bytes are valid UTF-8 and the base64 form otherwise.

## 7. Versioning

The protocol version is `<major>.<minor>`, reported by `--version` and at the head of this
document.

* A change that existing clients cannot read raises the **major** number. That includes removing
  or renaming a member, changing its type or meaning, and changing which requests are accepted.
* A change that existing clients can read raises the **minor** number: new optional request
  members, new response members, new operations. Clients ignore response members they do not know.
  A new value of `error` is not such a change, since a client has to treat each value in its own
  way.
* A client implementing version `C.m` accepts a server of version `S.n` only when `S` = `C` and
  `n` >= `m`. A client checks the version once, when it starts a server, with `--version`.

## 8. Example

```
{"id":7,"op":"probe","path":"/data/ds/ortho.tif","params":{"kind":"raster"}}
{"id":7,"ok":true,"result":{"type":"GeoRaster","isCog":false,"properties":{"width":4096,"height":4096,"geotransform":[500000.0,0.05,0.0,5000000.0,0.0,-0.05],"bands":[{"type":"Byte","colorInterp":"Red"}]},"wkt":"PROJCS[...]","pointGeom":{"type":"Point","coordinates":[9.15,45.25,0.0]},"polygonGeom":{"type":"Polygon","coordinates":[[[9.1,45.2,0.0],[9.2,45.2,0.0],[9.2,45.3,0.0],[9.1,45.3,0.0],[9.1,45.2,0.0]]]}}}
{"id":8,"op":"thumbnail","path":"/data/ds/photo.jpg","out":"/tmp/ddb/req-1/photo.webp","params":{"size":256}}
{"id":8,"ok":true,"result":{"bytes":9120}}
{"id":9,"op":"probe","path":"/data/ds/notes.tif","params":{"kind":"raster"}}
{"id":9,"ok":false,"error":"unreadable","message":"Cannot open /data/ds/notes.tif for reading"}
not json
{"id":null,"ok":false,"error":"internal","message":"not a valid request: not a valid JSON line: the request"}
```

(The first response is shortened.)

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
  ignored. Arrays and objects nest at most 64 deep (the outer object counts): a deeper line is not
  valid, whichever side sends it.
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
  cpu time) is answered with `error: "crashed"`, or `"limit"` when it was a limit that ended it
  (cpu time, the size of a file, memory); the server keeps serving. A worker keeps nothing that
  leads to the client but standard error: only the server writes to standard output. On Windows
  the server handles the requests itself; a request that uses more cpu time than its operation
  allows is answered with `error: "limit"` and the server then exits with status 4. Every worker confines itself before it opens the file of its request
  (section 9).

## 2. Command line

| Command | Behaviour |
| --- | --- |
| `gdal-util --version` | Prints one line, `gdal-util <version> protocol <major>.<minor>`, and exits 0. `<version>` is the version of the DroneDB build that contains the program. Nothing else is initialised. |
| `gdal-util --serve` | Serves the requests of this document on standard input and output. File paths are never taken from the command line in this mode. In the sandbox mode `strict` it exits with status 3 and a message that names the missing layer, without serving, when this host lacks a layer (section 9). |
| `gdal-util --request '<json>'` | Handles the one request given as the argument, in this process, writes its response line to standard output and exits 0 when the response is a success and 1 when it is a failure. An operation that runs long (`cog`) writes progress lines before the response, each a line of its own: `{"progress":<number>}`, the fraction done from 0 to 1, rising. The operations `cog` and `vector` run only in this mode. |
| `gdal-util --sandbox-report` | Prints the sandbox mode and, one line per layer, whether this host offers it (section 9), and exits 0 whatever is missing. |
| anything else | Prints a usage line to standard error and exits 2. |

The version line is matched by the regular expression `protocol ([0-9]+)\.([0-9]+)$`. Clients
should match it that way and ignore the rest of the line.

## 3. Requests

Every request is an object:

| Member | Type | Meaning |
| --- | --- | --- |
| `id` | integer | A signed 64-bit integer, echoed in the response. |
| `op` | string | The operation: `probe`, `thumbnail`, `normalize`, `geoproject`, `cog` or `vector`. |
| `path` | string | The file to read: an absolute path. See section 6 for `path64`. |
| `out` | string | The file to write (a folder, for `vector`), an absolute path, for the operations that write (`thumbnail`, `normalize`, `geoproject`, `cog`, `vector`). The folder it lies in exists and is the only place the operation writes to (for `cog`, a scratch file or two of GDAL and the statistics beside the COG). See section 6 for `out64`. |
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
| `sidecar` | string | `vector` only: the `.prj` next to `source` that gives a drawing its coordinate system, decided by the client (a client may ignore a `.prj` that is still on disk, for instance one it just removed from its index). An empty string says that none counts. Without the member the server reads the `.prj` it finds. See section 6 for `sidecar64`. |

The result of a probe:

| Member | Type | Meaning |
| --- | --- | --- |
| `type` | string | `GeoRaster` (a raster with a projection), `Raster` (a raster without) or `Vector`. |
| `isCog` | boolean | A `GeoRaster` that is already an optimised Cloud Optimized GeoTIFF (no conversion needed); false otherwise. |
| `properties` | object | The members the entry of the file adds to its properties (see below). |
| `wkt` | string | A `GeoRaster` with a geotransform: its projection as WKT text. The entry keeps it as its `projection` property. Absent otherwise. |
| `pointGeom` | object | A GeoJSON `Point` (`coordinates`: longitude, latitude, 0), WGS84: the centre of the footprint. Absent when there is none. |
| `polygonGeom` | object | A GeoJSON `Polygon` of one closed ring, WGS84: the footprint (a raster: its corners; a vector: the envelope of the layers). Absent when there is none. |
| `warning` | string | Present when the probe could only describe the file in part, with what to tell the user (at most 2000 bytes): a CAD drawing whose coordinate system GDAL cannot use keeps `georeferenced` but has no footprint. Absent when the probe is whole. |

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
| `thermalTags` | boolean | Whether the EXIF and XMP tags of the file say it is a thermal image (the make and model of a thermal camera, the FLIR or DJI thermal tags). Optional, `false` when absent. |

`vis` members, all strings and all optional: `preset`, `bands` (comma-separated band numbers),
`formula`, `bandFilter`, `colormap`, `rescale` (`min,max`).

The helper reads no EXIF or other tags of the file. A `formula` without a `bandFilter` takes its
bands from the sensor the helper detects in the raster; a single-band raster of no known sensor
gives its band to the thermal formulas when it is thermal: when `thermalTags` is `true`, when its
JPEG holds a FLIR segment, or when its GDAL metadata says so (a thermal keyword, a temperature
unit, the scale and offset of a Landsat thermal band). The client reads the tags and sets
`thermalTags` for such a request.

The result is `{"bytes": <size of the file written>}`. For a JPEG that carries a preview of the
same framing at least `size` pixels long (a Multi-Picture Format preview), the thumbnail is
decoded from the preview.

### 3.3 `geoproject`

```json
{"id":9,"op":"geoproject","path":"/data/ds/photo.jpg","out":"/tmp/ddb/req-2/geoprojected.tif","params":{"width":4000,"height":3000,"footprint":[[9.1,45.3],[9.1,45.2],[9.2,45.2],[9.2,45.3]],"outsize":"100%"}}
```

Places a photo on the ground and writes it to `out` as a GeoTIFF (JPEG compressed, with an alpha
band). The helper reads no EXIF: the client says where the photo is.

| `params` member | Type | Meaning |
| --- | --- | --- |
| `width`, `height` | integer | The size of the photo in pixels, 1 to 1 000 000. Required. |
| `footprint` | array | The ground position of the four corners of the photo, each `[longitude, latitude]` in EPSG:4326, in the order upper left, lower left, lower right, upper right. Required. |
| `outsize` | string | The size of the result as `gdal_translate -outsize` takes it: `"100%"`, or a number of pixels of width. Default: as the photo. |

The result is `{}`. A file that GDAL cannot open as a raster is `error: "unreadable"`. An `outsize`
that is not a positive number is `error: "internal"`.

### 3.4 `cog`

```json
{"id":10,"op":"cog","path":"/data/ds/ortho.tif","out":"/data/ds/.ddb/build/ab12/cog-temp-x/cog.tif","params":{"threads":4}}
```

Builds a Cloud Optimized GeoTIFF from a raster with a projection and writes it to `out`, with its
statistics in `<out>.stats.json`: Web Mercator, tiled in the GoogleMapsCompatible scheme, with
overviews, compressed with JPEG (8-bit RGB or RGBA) or LZW. The raster is always decoded and
encoded again, also when it is a COG already: the output is what GDAL wrote, never a copy of the
bytes of the input.

| `params` member | Type | Meaning |
| --- | --- | --- |
| `threads` | integer | The most threads the warp and the compression may use, 1 to 4096; the client holds that many tokens of its own CPU budget. Default: what the environment of the process allows (`DDB_BUILD_THREADS`). |

The result is `{}`. A raster without a projection is `error: "unreadable"`. A full device is
`error: "nospace"`.

### 3.5 `vector`

```json
{"id":11,"op":"vector","path":"/data/ds/parcels.geojson","out":"/data/ds/.ddb/build/.vec-stage-x","params":{}}
```

Builds the outputs of a vector file in the folder `out`, which exists and is empty:
`vec/source.gpkg` (a GeoPackage of the layers that hold a feature, in EPSG:4326, with a spatial
index) and `mvt/` (the MVT tiles in EPSG:3857, with `metadata.json`). A CAD drawing without a
coordinate system has no tiles: it gets `vec/local.json` (units and extent) instead of `mvt/`.

| `params` member | Type | Meaning |
| --- | --- | --- |
| `source` | string | The file the user named when `path` is a conversion of it (a DWG converted to a DXF by the client): its sidecars (the `.prj`) give the drawing its coordinate system. See section 6 for `source64`. |

The result is `{}`. A file that GDAL cannot open as a vector is `error: "unreadable"`.

### 3.6 `normalize`

```json
{"id":12,"op":"normalize","path":"/data/ds/DJI_0001_R.JPG","out":"/cache/artefacts/req-normalize-x/normalized.tif","params":{}}
```

Writes a raster as a GeoTIFF with a lossless compression (DEFLATE), so a client reads the copy
and never the file of the user: this is how the analyses of a raster that is not a build output of
a dataset (a photo, a GeoTIFF of an upload) get their pixels and their metadata. The copy has the
blocks of the raster (stripes of the same height, or tiles of the same size; 256 when TIFF cannot
write that tile size) and its overviews, the data of those the raster offers (a JPEG decoded at a
smaller scale, the overviews of a TIFF), because the approximate statistics of GDAL read an
overview or sample whole blocks: other ones would give other statistics for the same raster. The
copy keeps all bands with their data types, no-data values, colour interpretation, descriptions,
units, offsets and scales; the georeferencing (geotransform and coordinate system, or the ground
control points; the RPC); and the metadata of the dataset and of each band in every domain, except
`IMAGE_STRUCTURE` (it describes how the source is stored) and the domains that name other
datasets: `SUBDATASETS`, `DERIVED_SUBDATASETS`, `OVERVIEWS` and `GEOLOCATION`. Every value of the
copy is the value of the raster. The sidecar files of the raster are not copied. The
operation has no params. Only rasters are normalized: the vector queries of a client read the
GeoPackage that the `vector` operation wrote.

The result is `{}`. A file that GDAL cannot open as a raster is `error: "unreadable"`. A raster of
more than 1 000 000 000 pixels (width times height) is `error: "limit"`, with a message that says
to build the dataset first; the request writes nothing then. A full device is `error: "nospace"`.

## 4. Responses

A success: `{"id": <id>, "ok": true, "result": {...}}`. A failure:
`{"id": <id or null>, "ok": false, "error": "<code>", "message": "<text>"}`. The `message` is for
people (at most 2000 bytes; bytes that are not valid UTF-8 are replaced). An `id` of `null` means the
request had none that could be read.

| `error` | Meaning |
| --- | --- |
| `unreadable` | The file cannot be opened or read as what the request needs. |
| `policy` | The rules for opening user files refuse the file. |
| `limit` | A limit of the worker was reached: memory (an allocation failed), cpu time, file size, or the 16 MiB of a response line. |
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
| Wall time of `normalize` and `geoproject` | 120 s (the client's timeout) |
| Pixels of a raster to `normalize` | 1 000 000 000 (width times height) |
| Time of `cog` and `vector` | no limit of the protocol: the client ends the process after an idle time without a line, or at its maximum run time |
| Cpu time, memory, file size and open files of a worker | by operation, section 9 |

## 6. Text that is not UTF-8

File names are bytes, not always text. JSON strings must be valid UTF-8, so a path that is not
stays intact like this: the member `<name>` is replaced by `<name>64`, holding the standard base64
(RFC 4648 section 4, with padding) of the raw bytes. This applies to `path`, `out` and the `source`
and `sidecar` params, and works in both directions: a receiver accepts both forms (exactly one of the two); a
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

## 9. Confinement of the workers

A worker, or the process of `--request`, restricts itself before it opens the file of its request.
How depends on the host, and the environment variable `DDB_GDAL_SANDBOX` says what to do about a
layer the host lacks:

| Value | Behaviour |
| --- | --- |
| `best-effort` (default, also for an empty or unknown value, which is named once on standard error) | Applies the layers the host has. The server writes one warning at start that names the others. |
| `strict` | Every layer must be there. `--serve` exits with status 3 at start and names the missing layer; `--request` answers with `error: "internal"`, naming it, and runs nothing. |
| `off` | Applies no layer: only the limits of the host (and its `ulimit`) hold. The process boundary stays. The server says once that it is not confined. |

What a worker may do once confined (Linux; README.md has the details and the other systems):

* **Read** the folder of `path`; the folders of the `source` and `sidecar` params; the folders of
  the data of GDAL and PROJ, of the files libddb ships and of the program, and of the libraries it
  loaded; the user directory of PROJ; the system files it needs (`/proc/self`, `/dev/null`,
  `/dev/urandom`, `/etc/localtime`, the time zone data). Nothing else.
* **Write** in the folder `out` lies in (the folder `out` is, for `vector`), which the client made
  for the request and which must exist. Nothing else.
* **Not** open a network socket, start a program or another process (a thread is allowed), leave
  its process group, trace another process or signal another process.
* **Limits**, set for each request from its operation: cpu time below the client's wait (25 s for
  `probe` and `thumbnail`, 110 s for `normalize` and `geoproject`; `cog` and `vector` the run time
  of the client for each thread), address space (a share of the memory of the machine or its
  container), the size of a file written, open files, no core files. A limit the process already
  has from its starter (`ulimit`, a container) is never raised.

A worker that goes past a limit ends with `error: "limit"`: an allocation that fails in GDAL, a
worker the system ends for its cpu time or the size of a file. A one-shot process ended that way
writes no response; the client reads the signal (`SIGXCPU`, `SIGXFSZ`).

On Windows the server runs the requests itself, in a Job Object (memory, one process, no access to
the desktop) and at low integrity, and a thread of the server watches the cpu time of each request:
past the limit of its operation the request is answered with `error: "limit"` and the server exits
with status 4 (a client starts another for the next request); it can write only in folders that carry a low integrity label,
which the client puts on the folder of `out` when it makes it.

`gdal-util --sandbox-report` is the check to run on a production host: every line must say `active`.

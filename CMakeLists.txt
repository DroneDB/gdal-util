# gdal-util: the program that reads the files of users with GDAL, in its own process, for DroneDB.
#
# It builds only inside the DroneDB tree (add_subdirectory from apps/): the operations it runs live
# in libddb (src/library/raster_ops*.cpp), which the program contains (ddb_embed_library), so it
# takes the version of the package and the settings of the build from there. The two programs talk
# by the text protocol of PROTOCOL.md.
cmake_minimum_required(VERSION 3.21)

if(NOT COMMAND ddb_embed_library OR NOT DEFINED APP_VERSION OR NOT DEFINED DDB_RUNTIME_DIR)
    message(FATAL_ERROR "gdal-util is built as part of DroneDB (add_subdirectory(apps/gdal-util) after "
                        "src/): it contains libddb, which only that build provides. Configure the "
                        "DroneDB repository instead of this folder.")
endif()

add_executable(gdal-util
    src/main.cpp
    src/confinement.cpp
    src/cpu_watchdog.cpp
    src/dispatch.cpp
    src/executor.cpp
    src/fd_io.cpp
    src/memory_watch.cpp
    src/runtime.cpp
    src/server.cpp
    src/thread_count.cpp
)
target_include_directories(gdal-util PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(gdal-util PRIVATE GDAL_UTIL_VERSION="${APP_VERSION}")

# libddb's objects and libraries, without loading libddb
ddb_embed_library(gdal-util)
# It opens the files of datasets that can lie deep (cmake/LongPathAware.cmake)
ddb_long_path_aware(gdal-util)
# The sandbox the workers confine themselves with (vendor/sandbox); it depends on nothing of libddb
target_link_libraries(gdal-util PRIVATE ddb_warnings ddb_sandbox)
# The data files libddb looks up next to the executable (PROJ database, GDAL data)
ddb_stage_runtime_files(gdal-util)

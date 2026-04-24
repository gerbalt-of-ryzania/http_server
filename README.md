This setup was tested with Conan installed via `pip install conan==1.52.0`. Version `1.51` did not work correctly.

For more details, see the lesson: [Conan setup notes](https://www.notion.so/praktikum/bc565fa7a70040c48dc10850049b0a62?v=24f7e4d44c034398bc7a2c0899dbfd07&p=13c770a14d9246f58379cc4228d1a1ce&pm=s).

## Build on Linux

When building on Linux, make sure to pass these flags:

* `-s compiler.libcxx=libstdc++11`
* `-s build_type=???`

Example configuration for `Release` and `Debug`:

```sh
mkdir -p build-release
cd build-release
conan install .. --build=missing -s build_type=Release -s compiler.libcxx=libstdc++11
cmake .. -DCMAKE_BUILD_TYPE=Release
cd ..

mkdir -p build-debug
cd build-debug
conan install .. --build=missing -s build_type=Debug -s compiler.libcxx=libstdc++11
cmake .. -DCMAKE_BUILD_TYPE=Debug
cd ..
```

## Build on Windows

Two changes are required:

1. In `conanfile.txt`, replace `cmake` with `cmake_multi`.
2. In `CMakeLists.txt`, replace `include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)` with `include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)`.

After that, you can configure the project like this:

```sh
mkdir build
cd build
conan install .. --build=missing -s build_type=Debug
conan install .. --build=missing -s build_type=Release
conan install .. --build=missing -s build_type=RelWithDebInfo
conan install .. --build=missing -s build_type=MinSizeRel
cmake ..
```

This will generate all build configurations, which is slower than necessary. You can save time by keeping only the configurations you actually need.

Run the build only from the native Windows `cmd`. Other terminals may cause problems in some environments.

## Run with Docker

You can build and run the server in Docker with:

```sh
docker build -t my_http_server .
docker run --rm -p 80:8080 my_http_server
```

This is the canonical launch flow expected for students. If it does not work, the project setup is likely incorrect.

The `Dockerfile` contains the full build and run sequence if you need to inspect the details.

## Run

From the `build` directory, run:

```sh
bin/game_server ../data/config.json ../static/
```

Then open these URLs in the browser:

* `http://127.0.0.1:8080/api/v1/maps` to get the list of maps
* `http://127.0.0.1:8080/api/v1/map/map1` to get detailed information about map `map1`
* `http://127.0.0.1:8080/` to serve static content from the `static` directory

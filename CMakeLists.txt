cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)
add_compile_definitions(BOOST_ALL_NO_LIB)

if(MSVC)
    add_compile_options(/bigobj)
endif()

if(EXISTS "${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake")
    include("${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake")
else()
    include("${CMAKE_BINARY_DIR}/conanbuildinfo.cmake")
endif()
conan_basic_setup(TARGETS)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(GAME_SERVER_SOURCES
    src/http_server.cpp
    src/http_server.h
    src/sdk.h
    src/model.h
    src/model.cpp
    src/geom.h
    src/loot_generator.h
    src/loot_generator.cpp
    src/collision_detector.h
    src/collision_detector.cpp
    src/tagged.h
    src/json_loader.h
    src/json_loader.cpp
    src/request_handler.cpp
    src/request_handler.h
    src/extra_data.h
    src/extra_data.cpp
)

add_executable(game_server
    src/main.cpp
    ${GAME_SERVER_SOURCES}
)
target_link_libraries(game_server PRIVATE Threads::Threads CONAN_PKG::boost)

set_target_properties(game_server PROPERTIES
    VS_GLOBAL_VcpkgEnabled false
)

add_executable(game_server_tests
    ${GAME_SERVER_SOURCES}
    tests/loot_generator_tests.cpp
    tests/model-tests.cpp
)
target_link_libraries(game_server_tests PRIVATE Threads::Threads CONAN_PKG::catch2 CONAN_PKG::boost)

set_target_properties(game_server_tests PROPERTIES
    VS_GLOBAL_VcpkgEnabled false
)

enable_testing()
add_test(NAME game_server_tests COMMAND game_server_tests)

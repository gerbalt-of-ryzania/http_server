FROM gcc:11.3 AS build

RUN apt update && \
    apt install -y \
      python3-pip \
      cmake \
    && \
    pip3 install conan==1.*

WORKDIR /app

COPY conanfile.txt .
COPY CMakeLists.txt .
COPY src ./src
COPY tests ./tests

RUN echo "===== PWD =====" && pwd
RUN echo "===== FILES IN /app =====" && ls -la /app
RUN echo "===== FILES IN /app/src =====" && ls -la /app/src
RUN echo "===== CMakeLists.txt =====" && cat /app/CMakeLists.txt
RUN echo "===== conanfile.txt =====" && cat /app/conanfile.txt

RUN conan profile new default --detect --force && \
    conan profile update settings.compiler.libcxx=libstdc++11 default

RUN mkdir build && cd build && \
    conan install .. -s build_type=Release -s compiler.libcxx=libstdc++11 --build=missing && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build . && \
    if [ -f /app/build/game_server ]; then cp /app/build/game_server /app/game_server; \
    elif [ -f /app/build/bin/game_server ]; then cp /app/build/bin/game_server /app/game_server; \
    else echo "game_server binary not found" && exit 1; \
    fi

FROM ubuntu:22.04 AS run

RUN groupadd -r www && useradd -r -g www www
USER www

COPY --from=build /app/game_server /app/game_server
COPY ./data /app/data
COPY ./static /app/static

RUN echo "===== BUILD COMMANDS START ====="

ENTRYPOINT ["/app/game_server", "--config-file", "/app/data/config.json", "--www-root", "/app/static"]

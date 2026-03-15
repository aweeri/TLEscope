FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# build tools + x86_64 linux deps + cross-compilers
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git ca-certificates curl pkg-config \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    gcc-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*

# windows arm64 cross-compiler (llvm-mingw)
ARG LLVM_MINGW_VERSION=20260311
RUN curl -fSL -o /tmp/llvm-mingw.tar.xz \
    "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz" \
    && mkdir /opt/llvm-mingw \
    && tar xf /tmp/llvm-mingw.tar.xz -C /opt/llvm-mingw --strip-components=1 \
    && rm /tmp/llvm-mingw.tar.xz

WORKDIR /build
COPY . .

CMD ["./scripts/raylib-crossbuild.sh"]

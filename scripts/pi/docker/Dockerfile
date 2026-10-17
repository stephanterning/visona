# Bookworm keeps the binary compatible with Raspberry Pi OS Bookworm and later.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config ccache ca-certificates curl \
    libasound2-dev libfreetype6-dev libfontconfig1-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxcomposite-dev libxi-dev libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*
ENV CCACHE_DIR=/cache/ccache CCACHE_MAXSIZE=2G
WORKDIR /src

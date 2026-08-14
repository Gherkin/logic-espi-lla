# Build environment for the eSPI analyzer.
#
# The Saleae Analyzer SDK ships prebuilt shared libraries for linux-x86_64,
# and the offline test harness links against stubs rather than the real
# library, so the whole test suite builds and runs in this container.
#
#   docker build -t espi-build .
#   docker run --rm -v "$PWD":/work espi-build ./scripts/build.sh

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["./scripts/build.sh"]

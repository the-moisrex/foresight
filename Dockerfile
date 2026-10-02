FROM archlinux:base-devel

# Toolchain + system dependencies for building and testing foresight.
# The source is bind-mounted at runtime (see tools/container-test.sh),
# so this image never needs rebuilding on code changes.
# Retried a few times because Arch mirrors occasionally stall.
RUN ok=0; \
    for i in 1 2 3; do \
        if pacman -Syu --noconfirm --needed --disable-download-timeout \
            cmake \
            git \
            gtest \
            libevdev \
            libx11 \
            libxkbcommon \
            ninja \
            pkgconf \
            systemd \
            systemd-libs \
            xkeyboard-config; then \
            ok=1; break; \
        fi; \
        sleep 15; \
    done; \
    [ "$ok" = 1 ] || exit 1; \
    pacman -Scc --noconfirm

# googletest is found on the system; google-benchmark and liburing-hdr-only
# are fetched by CPM at configure time -- keep those downloads across runs.
ENV CPM_SOURCE_CACHE=/opt/cpm-cache
RUN mkdir -p /opt/cpm-cache

WORKDIR /src

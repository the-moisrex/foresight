#!/usr/bin/env bash
#
# Build and run the foresight test suite inside a container (podman or docker).
#
#   tools/container-test.sh                 # configure + build + ctest (debug-gcc)
#   tools/container-test.sh --device        # also run the uinput/udev device tests
#   tools/container-test.sh -- --output-on-failure -R test-bash
#
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)

image="foresight-dev:latest"
preset="debug-gcc"
engine="${CONTAINER_ENGINE:-}"
cpm_cache=""       # empty => named volume
rebuild=0
device=0
ctest_args=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] [-- <extra ctest args>]

Builds the dev image (if needed), then configures, builds and tests inside it.

Options:
  --device            Run the device-level tests (uinput/udev/hotplug) too.
                      These need real kernel uevents and device nodes, which
                      rootless containers cannot see, so this mode runs the
                      container rootful (sudo podman / docker), shares the host
                      network namespace and bind-mounts the host's live
                      /run/udev + /dev/input. Without it those tests skip.
  --preset <preset>   CMake preset to use (default: $preset).
  --image <ref>       Image reference (default: $image).
  --engine <e>        Container engine: podman or docker (default: auto-detect,
                      \$CONTAINER_ENGINE overrides).
  --cpm-cache <path>  Bind-mount this host directory as the CPM download cache
                      instead of the named volume "$image"-cpm (used by CI).
  --rebuild           Rebuild the image even if it already exists.
  -h, --help          Show this help.

Anything after "--" is passed to ctest, e.g.:
  $(basename "$0") -- -R test-bash -j 4
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --device)     device=1 ;;
        --preset)     preset="${2:?--preset needs a value}"; shift ;;
        --image)      image="${2:?--image needs a value}"; shift ;;
        --engine)     engine="${2:?--engine needs a value}"; shift ;;
        --cpm-cache)  cpm_cache="${2:?--cpm-cache needs a value}"; shift ;;
        --rebuild)    rebuild=1 ;;
        -h|--help)    usage; exit 0 ;;
        --)           shift; ctest_args=("$@"); break ;;
        *)            echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

if [[ -z "$engine" ]]; then
    if command -v podman >/dev/null 2>&1; then
        engine=podman
    elif command -v docker >/dev/null 2>&1; then
        engine=docker
    else
        echo "error: neither podman nor docker found" >&2
        exit 1
    fi
fi

# Device tests need kernel uevents and device nodes: only a rootful container
# gets those (rootless ones can neither hear uevents nor open /dev/input), so
# --device reruns the engine through sudo unless we are already root.
run_cmd=("$engine")
chown_after=""
if [[ $device -eq 1 && $engine == podman && $EUID -ne 0 ]]; then
    run_cmd=(sudo podman)
    chown_after=1
fi

if [[ $rebuild -eq 1 ]]; then
    echo "==> building image $image with $engine"
    "$engine" build -t "$image" "$root"
fi

if [[ ${run_cmd[*]} == "$engine" ]]; then
    # `image inspect` rather than podman-only `image exists` (docker lacks it).
    if ! "$engine" image inspect "$image" >/dev/null 2>&1; then
        echo "==> building image $image with $engine"
        "$engine" build -t "$image" "$root"
    fi
else
    # The rootful store is separate: keep it in sync with the primary one.
    user_id=$("$engine" image inspect --format '{{.Id}}' "$image" 2>/dev/null || true)
    root_id=$("${run_cmd[@]}" image inspect --format '{{.Id}}' "$image" 2>/dev/null || true)
    if [[ -z "$root_id" || ( -n "$user_id" && "$root_id" != "$user_id" ) ]]; then
        if [[ -n "$user_id" ]]; then
            echo "==> copying image $image into the root podman store"
            "$engine" save "$image" | "${run_cmd[@]}" load
        else
            echo "==> building image $image with ${run_cmd[*]}"
            "${run_cmd[@]}" build -t "$image" "$root"
        fi
    fi
fi

# Mount the repo at the very same path it has on the host: CMake bakes absolute
# paths into the cache, so this lets host and container share one build tree.
mounts=(-v "$root:$root:Z")
if [[ $device -eq 1 ]]; then
    # Live view of the host's udev (daemon, database, control socket) and input
    # nodes; the host udevd keeps them up to date while the tests run.
    mounts+=(-v /run/udev:/run/udev:ro -v /dev/input:/dev/input)
else
    # Rootless containers only get a static copy of the host udev database,
    # which is enough for the tests that enumerate devices by their properties.
    mounts+=(-v /run/udev:/host-udev:ro)
fi
if [[ -n "$cpm_cache" ]]; then
    mkdir -p "$cpm_cache"
    mounts+=(-v "$cpm_cache:/opt/cpm-cache")
else
    mounts+=(-v "${image//[:\/]/-}-cpm:/opt/cpm-cache")
fi

run_opts=()
prelude="set -o pipefail"
if [[ $device -eq 1 ]]; then
    # --net=host: libudev listens for the host udevd's broadcasts. The cgroup
    # rule allows the /dev/input/event* nodes (major 13) besides /dev/uinput.
    run_opts+=(--net=host --device /dev/uinput "--device-cgroup-rule=c 13:* rwm")
else
    prelude+=$'\n'"mkdir -p /run/udev"
    prelude+=$'\n'"[ ! -d /host-udev/data ] || cp -a /host-udev/data /run/udev/"
fi

# Shell-quote the passthrough ctest args so they survive the bash -c below.
printf -v quoted_ctest_args ' %q' "${ctest_args[@]+"${ctest_args[@]}"}"

cmd="$prelude
cmake --preset $preset &&
cmake --build --preset $preset &&
ctest --preset $preset --output-on-failure$quoted_ctest_args"

mode_desc=$preset
if [[ $device -eq 1 ]]; then
    mode_desc+=", device tests"
fi
echo "==> ${run_cmd[*]} run $image ($mode_desc)"
set +e
"${run_cmd[@]}" run --rm \
    "${mounts[@]}" \
    "${run_opts[@]+"${run_opts[@]}"}" \
    -w "$root" \
    "$image" \
    bash -c "$cmd"
rc=$?
set -e

# A rootful run leaves root-owned files in the shared build tree.
if [[ -n "$chown_after" && -d "$root/build-$preset" ]]; then
    sudo chown -R "$(id -u):$(id -g)" "$root/build-$preset" || true
fi
exit $rc

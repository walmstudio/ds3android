#!/usr/bin/env bash
set -euo pipefail
umask 077
root=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=${1:-"$root/../build-output/linux"}
[[ $# -le 1 && ! -e $out && ! -L $out ]] || { echo 'Pass a new output directory' >&2; exit 2; }
inputs=$(realpath -e -- "${DS3_INPUTS:-$root/../build-inputs}")
for f in image.bin libme1spv.so; do
    [[ -s $inputs/linux-x86_64/$f ]] || { echo "Missing Linux runtime input: $f" >&2; exit 2; }
done
[[ $(uname -m) == x86_64 ]] && pkg-config --exists 'sdl2 >= 2.26' || { echo 'Linux x86-64 and SDL2 development files >= 2.26 required' >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/ds3-linux.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
make -C "$root" INPUTS="$inputs" BUILD="$work" check-linux-inputs
make -C "$root" -j"${JOBS:-$(nproc)}" INPUTS="$inputs" BUILD="$work" linux
python3 -B "$root/linux/test-desktop-route.py" "$work/linux/ds3-wayland"
python3 -B "$root/linux/test-render-settings.py" "$work/linux/ds3-render-settings"
mkdir -p -- "$(dirname -- "$out")"
mkdir -- "$out"
install -m 700 -- "$work/linux/"{ds3-wayland,ds3-launcher,ds3-render-settings} "$root/linux/play-ds3.sh" "$out/"
install -m 600 -- "$inputs/linux-x86_64/"{image.bin,libme1spv.so} "$root/"{icon.ico,loading.png} "$out/"
(cd -- "$out" && sha256sum -- ds3-wayland ds3-launcher ds3-render-settings play-ds3.sh image.bin libme1spv.so icon.ico loading.png > SHA256SUMS)
printf 'Linux build: %s\n' "$out"

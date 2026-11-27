#!/usr/bin/env bash
set -euo pipefail
umask 077
root=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=${1:-"$root/../build-output/linux"}
[[ $# -le 1 ]] || { echo "usage: $0 [OUTPUT_DIR]" >&2; exit 2; }
out=$(realpath -m -- "$out")
[[ $out != / && $out != "$HOME" && $out != "$root" && $out != "$root"/* && $out != "$(dirname -- "$root")" ]] \
    || { echo "Refusing to use $out as the output directory" >&2; exit 2; }
if [[ -e $out || -L $out ]]; then
    [[ -d $out && ! -L $out ]] || { echo "Output path exists and is not a directory: $out" >&2; exit 2; }
    if [[ -n $(ls -A -- "$out") && ! ( -f $out/SHA256SUMS && ( -f $out/ds3-wayland || -f $out/ds3-launcher ) ) ]]; then
        echo "Output directory is not empty and is not a previous Linux build: $out" >&2
        echo 'Pass an empty or non-existent directory, or remove the old contents.' >&2
        exit 2
    fi
fi
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
mkdir -p -- "$out"
if [[ -f $out/SHA256SUMS ]]; then
    while read -r _ name; do
        case $name in
            ds3-wayland|ds3-launcher|ds3-render-settings|play-ds3.sh|image.bin|libme1spv.so|icon.ico|loading.png) ;;
            *) rm -f -- "$out/$name" ;;
        esac
    done < "$out/SHA256SUMS"
fi
install -m 700 -- "$work/linux/"{ds3-wayland,ds3-launcher,ds3-render-settings} "$root/linux/play-ds3.sh" "$out/"
install -m 600 -- "$inputs/linux-x86_64/"{image.bin,libme1spv.so} "$root/"{icon.ico,loading.png} "$out/"
(cd -- "$out" && sha256sum -- ds3-wayland ds3-launcher ds3-render-settings play-ds3.sh image.bin libme1spv.so icon.ico loading.png > SHA256SUMS)
printf 'Linux build: %s\n' "$out"

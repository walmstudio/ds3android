#!/usr/bin/env bash
set -euo pipefail
umask 077
root=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=${1:-"$root/../build-output/android"}
[[ $# -le 1 ]] || { echo "usage: $0 [OUTPUT_DIR]" >&2; exit 2; }
out=$(realpath -m -- "$out")
[[ $out != / && $out != "$HOME" && $out != "$root" && $out != "$root"/* && $out != "$(dirname -- "$root")" ]] \
    || { echo "Refusing to use $out as the output directory" >&2; exit 2; }
if [[ -e $out || -L $out ]]; then
    [[ -d $out && ! -L $out ]] || { echo "Output path exists and is not a directory: $out" >&2; exit 2; }
    if [[ -n $(ls -A -- "$out") && ! ( -f $out/SHA256SUMS && -f $out/ds3port.apk ) ]]; then
        echo "Output directory is not empty and is not a previous Android build: $out" >&2
        echo 'Pass an empty or non-existent directory, or remove the old contents.' >&2
        exit 2
    fi
fi
inputs=$(realpath -e -- "${DS3_INPUTS:-$root/../build-inputs}")
[[ -s $inputs/android-arm64/libme1spv.so ]] || { echo 'Missing ARM64 libme1spv.so in private inputs' >&2; exit 2; }
#[[ -n ${KEYSTORE:-} && -s $KEYSTORE ]] || { echo 'Set KEYSTORE to the existing local signing key' >&2; exit 2; }
#KEYSTORE=$(realpath -e -- "$KEYSTORE"); export KEYSTORE
export ANDROID_SDK=${ANDROID_SDK:-${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}}
export ANDROID_BT=${ANDROID_BT:-$ANDROID_SDK/build-tools/34.0.0}
export ANDROID_JAR=${ANDROID_JAR:-$ANDROID_SDK/platforms/android-34/android.jar}
export NDK=${NDK:-$HOME/android-ndk-r27c}
export ANDROID_API=29
jdk17() {
    local version
    [[ -x $1/bin/java && -x $1/bin/javac ]] || return 1
    version=$("$1/bin/javac" -version 2>&1) || return 1
    [[ $version =~ ^javac\ 17([.+-]|$) ]]
}
if [[ -z ${JAVA_HOME:-} ]]; then
    javac_path=$(type -P javac || true)
    if [[ -n $javac_path ]]; then
        candidate=$(dirname -- "$(dirname -- "$(readlink -f -- "$javac_path")")")
        if jdk17 "$candidate"; then JAVA_HOME=$candidate; fi
    fi
    if [[ -z ${JAVA_HOME:-} ]]; then
        for candidate in /usr/lib/jvm/*; do
            if jdk17 "$candidate"; then JAVA_HOME=$candidate; break; fi
        done
    fi
fi
jdk17 "${JAVA_HOME:-}" || { echo 'JDK 17 required: set JAVA_HOME' >&2; exit 2; }
JAVA_HOME=$(realpath -e -- "$JAVA_HOME"); export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"
for tool in "$ANDROID_BT/"{aapt2,d8,zipalign,apksigner} "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang"; do
    [[ -x $tool ]] || { echo "Missing tool: $tool" >&2; exit 2; }
done
[[ -s $ANDROID_JAR ]] || { echo "Missing SDK platform: $ANDROID_JAR" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/ds3-android.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
make -C "$root" INPUTS="$inputs" BUILD="$work" check-android-inputs
make -C "$root" -j"${JOBS:-$(nproc)}" INPUTS="$inputs" BUILD="$work" game-lib
install -m 600 -- "$inputs/android-arm64/libme1spv.so" "$work/android/"
apk="$work/ds3port.apk"
PRIVATE_DEV=1 NO_EXE= NO_D3DX=1 NO_GPU=1 REQUIRE_DS3_VULKAN=1 \
    DS3_NATIVE_DIR="$work/android" DS3_IMAGE_INPUT="$inputs/linux-x86_64/image.bin" \
    DS3_APK_WORK="$work/apk-work" DS3_APK_OUTPUT="$apk" bash "$root/android/build_apk.sh"
"$ANDROID_BT/apksigner" verify "$apk"
"$ANDROID_BT/zipalign" -c -p 4 "$apk"
mkdir -p -- "$(dirname -- "$out")"
mkdir -p -- "$out"
install -m 600 -- "$apk" "$out/ds3port.apk"
(cd -- "$out" && sha256sum -- ds3port.apk > SHA256SUMS)
printf 'Android build: %s/ds3port.apk\n' "$out"

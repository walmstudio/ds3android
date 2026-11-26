#!/usr/bin/env bash
set -euo pipefail
umask 077
lang=${1:-}
case $lang in
    ru|en) ;;
    *) printf 'Usage: %s ru|en [PROFILE_DIR]\n' "$0" >&2; exit 2;;
esac
if (( $# > 2 )); then exit 2; fi
cursors=${DS3_DSOUND_CURSORS:-donor30}
case $cursors in
    donor30|wine9|wine) ;;
    *) printf 'DS3_DSOUND_CURSORS: expected donor30, wine9 or wine\n' >&2; exit 2;;
esac
pcm=${DS3_DSOUND_PCM:-retain}
queue=${DS3_PIPEWIRE_QUEUE:-target}
case $pcm in retain|fresh) ;; *) printf 'DS3_DSOUND_PCM: expected retain or fresh\n' >&2; exit 2;; esac
case $queue in target|request) ;; *) printf 'DS3_PIPEWIRE_QUEUE: expected target or request\n' >&2; exit 2;; esac
profile=${2:-"$HOME/.local/share/ds3port-test73"}
game=$(realpath -e -- "${DS3_GAME_DIR:-$HOME/ds3an/game}")
test -f "$game/deadspace3.exe"
audio=${DS3_AUDIO_BACKEND:-pipewire}
case $audio in alsa|pipewire|pulse) ;; *) printf 'Invalid DS3_AUDIO_BACKEND\n' >&2; exit 2;; esac
mkdir -p -- "$profile"
profile=$(realpath -- "$profile")
cd -- "$(dirname -- "$0")"
render_size=${DS3_RENDER_SIZE:-game}
if [[ $render_size != game ]]; then ./ds3-render-settings "$profile" "$render_size"; fi
DS3_IMAGE="$PWD/image.bin" ME1_SPV_LIB="$PWD/libme1spv.so" \
    ME1_D3D_CMD=2 ME1_GEARMASS="${ME1_GEARMASS:-custom}" ME1_GM_AHEAD=0 ME1_GM_WORKERS=0 \
    ME1_VK_SECONDARY=0 ME1_VK_STREAM_COPY=0 ME1_VK_SHADOW_COPY=0 ME1_VK_RANGE_REUSE=on \
    DS3_AUDIO_BACKEND="$audio" DS3_DSOUND_CURSORS="$cursors" DS3_DSOUND_PCM="$pcm" DS3_PIPEWIRE_QUEUE="$queue" \
    ./ds3-wayland --language "$lang" "$game" "$profile" \
    2>&1 | tee "$profile/ds3-last.log"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p tmp
out=$(mktemp -d "$PWD/tmp/input-test.XXXXXX")
trap 'rm -rf "$out"' EXIT
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}
jar=${ANDROID_JAR:-$sdk/platforms/android-34/android.jar}
src=android/src/com/ds3port/launcher
javac -cp "$jar" -d "$out" android/test/input/{Handler,Looper,GameActivity,PadTest,TouchMouseTest,RelativeTouchpadTest}.java \
    "$src/Pad.java" "$src/PadMap.java" "$src/PadAxes.java" "$src/TouchMouse.java" "$src/RelativeTouchpad.java" "$src/L.java"
for t in triggers hat touch reset axes sourceOwnership quantizedOwnership stickOwnership sinks remap rearm; do
  java -cp "$out:$jar" com.ds3port.launcher.PadTest "$t" || failed=1
done
java -cp "$out:$jar" com.ds3port.launcher.TouchMouseTest || failed=1
java -cp "$out:$jar" com.ds3port.launcher.RelativeTouchpadTest || failed=1
python3 android/test/input_cancel_test.py || failed=1
python3 android/test/input_profile_test.py || failed=1
if [[ -n ${INPUT_JSON_JAR:-} ]]; then
  javac -cp "$jar" -d "$out/profile" android/test/input/{ControlsProfileTest,Context,AssetManager}.java \
      "$src/"{Pad,PadMap,Bind,CtlDialogs,CtlProfile,ControlsSettings,L}.java \
      android/test/input/{Handler,Looper,GameActivity}.java
  java -cp "$out/profile:$INPUT_JSON_JAR:$jar" com.ds3port.launcher.ControlsProfileTest "$out/saved" || failed=1
fi
${CC:-cc} -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -pthread \
    android/test/input/xinput_state_test.c -o "$out/xinput_state_test"
"$out/xinput_state_test" || failed=1
jdk=$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")
${CC:-cc} -D_GNU_SOURCE -w -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$jdk/include" -I"$jdk/include/linux" -Iandroid/test/input/include -Iruntime \
    android/test/input/kbm_ownership_test.c -o "$out/kbm_ownership_test"
ulimit -c 0
for t in pointer bindings keys hardware_cleanup layout1; do
  "$out/kbm_ownership_test" "$t" || failed=1
done
${CC:-cc} -O2 -D__ANDROID__ -ffunction-sections -fdata-sections -Wl,--gc-sections -pthread \
    -Iruntime android/test/input/touch_cursor_test.c -o "$out/touch_cursor_test"
"$out/touch_cursor_test" || failed=1
exit "${failed:-0}"

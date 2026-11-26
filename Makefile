# One DS3 runtime, two platform builds. Generated guest C is a private input.
.DEFAULT_GOAL := help
CC ?= gcc
BUILD ?= build
INPUTS ?=
NDK ?= $(HOME)/android-ndk-r27c
ACC := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang

RT_NAMES := rt rt_fp cpu_profile hle_win32 hle_sys hle_fs hle_mod hle_thread hle_seh hle_exe hle_vmem hle_d3d9 hle_d3d9_cmd gearmass sm3_glsl hle_hb hle_input hle_al ds3_audio_trace main_ds3
COMMON := $(addprefix runtime/,$(addsuffix .c,$(RT_NAMES))) settings/ini.c settings/me1settings.c settings/ds3gearmass.c
HEADERS := $(wildcard runtime/*.h runtime/*.inc settings/*.h)
RT_FLAGS := -w -fwrapv -fno-strict-aliasing -Iruntime -Iruntime/khr -Isettings
GUEST_FLAGS := -O1 -w -fwrapv -fno-strict-aliasing -Iruntime
SDL_CFLAGS = $(shell pkg-config --cflags sdl2)
SDL_LIBS = $(shell pkg-config --libs sdl2)
PW_CFLAGS ?= -I$(INPUTS)/pipewire-headers/pipewire-0.3 -I$(INPUTS)/pipewire-headers/spa-0.2

L := $(BUILD)/linux
A := $(BUILD)/android
# The private linux/ input directory contains the canonical №75 guest code.
GUEST_ROOT := $(INPUTS)/linux
BASE_C := $(sort $(wildcard $(GUEST_ROOT)/base/code_*.c))
WIDE_C := $(sort $(wildcard $(GUEST_ROOT)/wide/add_*.c))
GUEST_C := $(WIDE_C) $(addprefix $(GUEST_ROOT)/dispatch/,module.c vtable.c) $(BASE_C)
GUEST_HEADERS := $(wildcard $(GUEST_ROOT)/*/funcs.h)
L_GUEST := $(patsubst $(GUEST_ROOT)/%.c,$(L)/guest/%.o,$(GUEST_C))
A_GUEST := $(patsubst $(GUEST_ROOT)/%.c,$(A)/guest/%.o,$(GUEST_C))
L_RT := $(patsubst %.c,$(L)/%.o,$(COMMON))
A_RT := $(patsubst %.c,$(A)/%.o,$(COMMON) android/jni/game_jni.c)
LINUX_HOST := $(addprefix $(L)/linux/,$(addsuffix .o,ds3_wayland audio-out audio-pump audio-alsa audio-pipewire audio-pulse))
A_FLAGS := -march=armv8.2-a+rcpc -fsigned-char -fPIC

.PHONY: help linux game-lib check-guest-inputs check-linux-inputs check-android-inputs
help:
	@printf '%s\n' 'Use ./build-linux.sh or ./build-android.sh' 'Direct targets: linux, game-lib (INPUTS=/absolute/private-input-dir)'

check-guest-inputs:
	@test -n '$(INPUTS)' && test $(words $(BASE_C)) = 130 && test $(words $(WIDE_C)) = 53 && test $(words $(GUEST_HEADERS)) = 3 && test -s '$(GUEST_ROOT)/dispatch/module.c' && test -s '$(GUEST_ROOT)/dispatch/vtable.c' && test -s '$(INPUTS)/linux-x86_64/image.bin' || { echo 'Missing shared №75 guest C/image: set INPUTS to the private input directory' >&2; exit 2; }

check-linux-inputs check-android-inputs: check-guest-inputs

linux: check-linux-inputs $(L)/ds3-wayland $(L)/ds3-launcher $(L)/ds3-render-settings
game-lib: check-android-inputs $(A)/libds3game.so $(A)/libds3run.so

$(L)/runtime/%.o: runtime/%.c $(HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) -O1 -g $(RT_FLAGS) -DDS3_LIB -c $< -o $@
$(L)/settings/%.o: settings/%.c $(HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) -O1 -g $(RT_FLAGS) -c $< -o $@
$(L)/linux/ds3_wayland.o: linux/ds3_wayland.c $(wildcard linux/*.h linux/*.inc) $(HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) -O1 -g -Wall -Wextra -Iruntime $(SDL_CFLAGS) -c $< -o $@
$(L)/linux/audio-%.o: linux/audio-%.c linux/audio-out.h Makefile
	@mkdir -p $(@D)
	$(CC) -std=gnu11 -O2 -g -Wall -Wextra -Werror $(PW_CFLAGS) -c $< -o $@
$(L)/guest/%.o: $(GUEST_ROOT)/%.c runtime/rt.h $(GUEST_HEADERS) Makefile | check-guest-inputs
	@mkdir -p $(@D)
	$(CC) $(GUEST_FLAGS) -I$(dir $<) -c $< -o $@
$(L)/ds3-wayland: $(L_GUEST) $(L_RT) $(LINUX_HOST)
	$(CC) -o $@ $^ $(SDL_LIBS) -ldl -lm -pthread
$(L)/ds3-launcher: linux/launcher.c linux/ui.c linux/font-assets.S linux/ui.h $(wildcard linux/assets/*.ttf linux/third_party/*.h)
	@mkdir -p $(@D)
	$(CC) -O2 -g $(SDL_CFLAGS) $(filter %.c %.S,$^) $(SDL_LIBS) -lm -o $@
$(L)/ds3-render-settings: linux/render-settings.c
	@mkdir -p $(@D)
	$(CC) -O2 -g -Wall -Wextra $< -o $@

$(A)/%.o: %.c $(HEADERS) Makefile
	@mkdir -p $(@D)
	$(ACC) -O2 $(A_FLAGS) $(RT_FLAGS) -DDS3_LIB -c $< -o $@
$(A)/guest/%.o: $(GUEST_ROOT)/%.c runtime/rt.h $(GUEST_HEADERS) Makefile | check-guest-inputs
	@mkdir -p $(@D)
	$(ACC) $(GUEST_FLAGS) $(A_FLAGS) -I$(dir $<) -c $< -o $@
$(A)/libds3game.so: $(A_RT) $(A_GUEST) android/jni/game.map | check-android-inputs
	$(ACC) -shared -o $@ -Wl,-soname,libds3game.so -Wl,-Bsymbolic -Wl,--version-script=android/jni/game.map -Wl,--pack-dyn-relocs=android -Wl,-z,max-page-size=16384 $(A_RT) $(A_GUEST) -landroid -llog -lm -ldl -Wl,--no-undefined
$(A)/libds3run.so: android/jni/run_main.c
	@mkdir -p $(@D)
	$(ACC) -O2 -Iruntime -fPIE -pie -Wl,-z,max-page-size=16384 -o $@ $< -ldl

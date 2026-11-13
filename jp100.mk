NDK ?= /data/tools/android-ndk-r27c
ACC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang
GOPT = -march=armv8.2-a+rcpc -fPIC -fsigned-char -fwrapv -fno-strict-aliasing -w -Iruntime
JPOPT ?= -O1
P = build/android/jp
JP_C = $(wildcard out/jp100/*.c)
FM_C = $(wildcard out/fmodex/*.c)
BK_C = $(wildcard out/bink/*.c)
JP_O = $(patsubst out/jp100/%.c,$(P)/jp/%.o,$(JP_C))
FM_O = $(patsubst out/fmodex/%.c,$(P)/fm/%.o,$(FM_C))
BK_O = $(patsubst out/bink/%.c,$(P)/bk/%.o,$(BK_C))
RT = rt rt_fp cpu_profile hle_win32 hle_sys hle_fs hle_mod hle_thread hle_seh hle_exe hle_vmem hle_d3d9 hle_d3d9_cmd gearmass sm3_glsl hle_hb hle_input hle_al jp100_cursor hle_jp ini me1settings main_jp game_jni
RT_O = $(patsubst %,$(P)/rt/%.o,$(RT))
RTOPT = -O2 -march=armv8.2-a+rcpc -fsigned-char -fwrapv -fno-strict-aliasing -w -Iruntime -Iruntime/khr -Isettings -fPIC -DME1_LIB -DJP100
vpath %.c runtime settings android/jni

$(P)/jp/%.o: out/jp100/%.c
	@mkdir -p $(@D); echo "  CC $<"; $(ACC) $(JPOPT) $(GOPT) -Iout/jp100 -c -o $@.tmp $< && mv $@.tmp $@
$(P)/fm/%.o: out/fmodex/%.c
	@mkdir -p $(@D); echo "  CC $<"; $(ACC) -O2 $(GOPT) -Iout/fmodex -c -o $@ $<
$(P)/bk/%.o: out/bink/%.c
	@mkdir -p $(@D); echo "  CC $<"; $(ACC) -O2 $(GOPT) -Iout/bink -c -o $@ $<
# headers are dependencies too: hle_d3d9.o includes hle_d3d9_vk.h, a header-only change must rebuild it
$(P)/rt/%.o: %.c runtime/*.h settings/*.h
	@mkdir -p $(@D); echo "  CC $<"; $(ACC) $(RTOPT) -c -o $@ $<

jp-objs: $(JP_O)
mod-objs: $(FM_O) $(BK_O)
rt-objs: $(RT_O)
build/android/libme1game.so: $(RT_O) $(FM_O) $(BK_O) $(JP_O)
	$(ACC) -shared -o $@ -Wl,-soname,libme1game.so -Wl,-Bsymbolic -Wl,--version-script=android/jni/game.map -Wl,--pack-dyn-relocs=android -Wl,-z,max-page-size=16384 -Wl,--no-undefined $(RT_O) $(FM_O) $(BK_O) $(JP_O) -landroid -llog -lm -ldl
build/android/libme1run.so: android/jni/run_main.c
	$(ACC) -O2 -fPIE -pie -Wl,-z,max-page-size=16384 -o $@ android/jni/run_main.c -ldl
jp-lib: build/android/libme1game.so build/android/libme1run.so
.PHONY: jp-objs mod-objs rt-objs jp-lib

# v0.10: libme1spv.so - GLSL -> SPIR-V (glslang 15, static) for the Vulkan renderer (hle_d3d9_vk.h loads it with dlopen)
GLSLANG_SRC ?= /data/build3p/glslang-15.0.0
GA = $(GLSLANG_SRC)/build-android/glslang
build/android/libme1spv.so: android/jni/me1spv.c android/jni/me1spv.map
	mkdir -p build/android && $(ACC) -O2 -fPIC -fvisibility=hidden -I$(GLSLANG_SRC) -I$(GLSLANG_SRC)/build-android/include -c -o build/android/me1spv.o $<
	$(ACC:clang=clang++) -shared -fPIC -Wl,-soname,libme1spv.so -Wl,-z,max-page-size=16384 -Wl,--gc-sections -Wl,--version-script=android/jni/me1spv.map -s \
	    -o $@ build/android/me1spv.o -Wl,--whole-archive $(GA)/libglslang-default-resource-limits.a -Wl,--no-whole-archive $(GA)/libglslang.a -static-libstdc++ -lm -Wl,--no-undefined

# ---- v4: desktop Linux (SDL2): make -f jp100.mk jp-linux (build-linux-jp100.sh) ----
# The same recompiled code as jp-lib, built for the host (x86-64 or aarch64); linux/me1_linux.c is the front end
# (window, input, audio, loading screen) instead of android/jni/game_jni.c. Flags follow the me1port Makefile (linux).
HCC ?= cc
L = build/linux-jp
HARM := $(if $(filter aarch64 arm64,$(shell uname -m)),-march=armv8.2-a+rcpc,)
SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS ?= $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)
HGOPT = $(HARM) -fsigned-char -fwrapv -fno-strict-aliasing -w -Iruntime
LJOPT ?= -O1
LCF = -O2 -g $(HARM) -fsigned-char -fwrapv -fno-strict-aliasing -w -Iruntime -Iruntime/khr -Isettings -DJP100
LRT_O = $(patsubst %,$(L)/rt/%.o,$(filter-out game_jni,$(RT)))
LJP_O = $(patsubst out/jp100/%.c,$(L)/jp/%.o,$(JP_C))
LFM_O = $(patsubst out/fmodex/%.c,$(L)/fm/%.o,$(FM_C))
LBK_O = $(patsubst out/bink/%.c,$(L)/bk/%.o,$(BK_C))
LUI_O = $(L)/rt/ui.o $(L)/rt/i18n.o $(L)/rt/gpuprobe.o $(L)/rt/assets.o
LUI_FLAGS = -O2 -g $(HARM) -Wall -Wno-format-truncation -Wno-misleading-indentation -Wno-unused-function -Ilinux -Isettings -Iruntime/khr $(SDL_CFLAGS)

$(L)/jp/%.o: out/jp100/%.c
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) $(LJOPT) $(HGOPT) -Iout/jp100 -c $< -o $@
$(L)/fm/%.o: out/fmodex/%.c
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) -O2 $(HGOPT) -Iout/fmodex -c $< -o $@
$(L)/bk/%.o: out/bink/%.c
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) -O2 $(HGOPT) -Iout/bink -c $< -o $@
$(L)/rt/%.o: %.c runtime/*.h settings/*.h
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) $(LCF) -c $< -o $@
$(L)/rt/main_jp.o: runtime/main_jp.c runtime/*.h
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) $(LCF) -DME1_LIB -c $< -o $@
$(L)/rt/me1_linux.o: linux/me1_linux.c linux/*.h runtime/*.h settings/*.h
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) $(LCF) -Ilinux -Wno-unused-parameter $(SDL_CFLAGS) -c $< -o $@
$(L)/rt/ui.o: linux/ui.c linux/ui.h
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) $(LUI_FLAGS) -c $< -o $@
$(L)/rt/i18n.o: linux/i18n.c linux/i18n.h
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) $(LUI_FLAGS) -c $< -o $@
$(L)/rt/gpuprobe.o: linux/gpuprobe.c linux/gpuprobe.h
	@mkdir -p $(@D); echo "  CC $<"; $(HCC) $(LUI_FLAGS) -c $< -o $@
$(L)/rt/assets.o: linux/assets.S
	@mkdir -p $(@D); echo "  AS $<"; $(HCC) -c $< -o $@
$(L)/jp100-linux: $(LRT_O) $(L)/rt/me1_linux.o $(LUI_O) $(LFM_O) $(LBK_O) $(LJP_O)
	@echo "  LD $@"; $(HCC) -o $@ $^ $(SDL_LIBS) -lm -ldl -pthread -rdynamic
jp-linux: $(L)/jp100-linux
.PHONY: jp-linux

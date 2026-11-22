# Streets of Rage X360: build no WSL com o clang Xenon do OXDK.
#   bash port/tools/build.sh   (from a path without spaces: make cannot take them)
OXDK_DIR    := $(HOME)/OXDK
OXDK_TARGET := xbox360
OXDK_LLVM   ?= $(HOME)/oxdk-llvm/build
XDK_DIR     ?= $(HOME)/xdk360/XDK
OXDK_LIBCXX_DIR ?= $(HOME)/oxdk-llvm/libcxx/include
XENON_ABI   := 1
OXDK360_LIBCXX := 1
XEX_TITLE   := StreetsOfRage
XDK_LIBS    := xapilib,d3d9,d3dx9,xgraphics,xaudio2,x3daudio,xinput2,xmcore,libcMT
XEX_STACK   := 0x400000
ROOT ?= $(abspath ..)
REF  := $(ROOT)/referencias/Streets-Of-Rage-Recomp
PORT := $(ROOT)/port
GEN  := $(REF)/generated/sor1
OUTPUT_DIR := $(PORT)/build
RT := main console machine widescreen state rewind png text launcher launcher_scene foil ui menu savestate \
      cheats hd motion ost filter input pads combat automap controls outfit preview fx texpack bus vdp io \
      settings z80bus ai fighters music decoders third
SRCS := $(patsubst %,$(PORT)/runtime/src/%.c,$(RT)) $(PORT)/runtime/src/sound.cpp \
        $(wildcard $(GEN)/main/recomp_*.c) $(GEN)/main/edition.c $(GEN)/editions.c \
        $(REF)/build/sor1/fonts_data.c $(REF)/build/sor1/musashi_gen/m68kops.c \
        $(addprefix $(REF)/third_party/musashi/,m68kcpu.c m68kdasm.c softfloat/softfloat.c) \
        $(REF)/third_party/z80/z80.c \
        $(addprefix $(REF)/third_party/ymfm/src/,ymfm_opn.cpp ymfm_ssg.cpp ymfm_adpcm.cpp ymfm_misc.cpp) \
        $(wildcard $(PORT)/x360/platform/*.c) $(wildcard $(PORT)/x360/platform/*.cpp)
include $(OXDK_DIR)/oxdk.mk
OXDK360_CXXFLAGS += -nostdlibinc
OBJCOPY := $(OXDK_LLVM)/bin/llvm-objcopy
export OXDK360_LLVM := $(OXDK_LLVM)
INC := -I$(PORT)/runtime/src -I$(REF)/build/sor1/config -I$(REF)/third_party/z80 -I$(REF)/third_party/musashi \
       -I$(REF)/build/sor1/musashi_gen -I$(REF)/third_party/ymfm/src -I$(REF)/third_party/single \
       -I$(PORT)/x360/platform -I$(PORT)/x360/posix -I$(REF)/third_party/sdl2/include \
       "-DMUSASHI_CNF=\"musashi_conf.h\"" -DSDL_MAIN_HANDLED -DX360 \
       -include $(PORT)/x360/compat/x360_compat.h
OPT      := -O2 -fbuiltin -mtune=a2
CFLAGS   := -std=gnu11 $(OPT) -w $(INC)
CXXFLAGS := -std=c++17 $(OPT) -w $(INC)

# Objetos em build/obj (a regra padrão do OXDK põe o .o ao lado do fonte, dentro da referência).
OBJDIR := $(OUTPUT_DIR)/obj
MYOBJS := $(patsubst $(ROOT)/%,$(OBJDIR)/%.o,$(SRCS))
MYXEX  := $(OUTPUT_DIR)/xex/default.xex
.DEFAULT_GOAL := xex
xex: $(MYXEX)
objs: $(MYOBJS)
$(OBJDIR)/%.c.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	@$(CLANG) $(OXDK360_CFLAGS) $(CFLAGS) $< -o $@
	@$(OBJCOPY) --remove-section=.deplibs $@ 2>/dev/null || true
$(OBJDIR)/%.cpp.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	@$(CLANGXX) $(OXDK360_CXXFLAGS) $(CXXFLAGS) $< -o $@
	@$(OBJCOPY) --remove-section=.deplibs $@ 2>/dev/null || true
$(MYXEX): $(MYOBJS) $(OXDK360_BUILTINS_O) $(OXDK360_LIBCXX_O)
	@mkdir -p $(dir $@)
	PATH="$(OXDK_LLVM)/bin:$$PATH" python3 $(OXDK360_DIR)/tools/oxdklink/oxdklink.py $^ \
	    -T $(LINKER_SCRIPT) -o $@ \
	    --xdk "$(XDK_DIR)" --libs $(XDK_LIBS) \
	    --base $(XEX_BASE) --stack $(XEX_STACK) --name $(XEX_TITLE).exe \
	    --title-id $(XEX_TITLE_ID)

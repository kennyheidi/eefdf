#---------------------------------------------------------------------------------
# 3DS Audio Player — Makefile
# Compatible with Old 3DS (O3DS) and New 3DS
#---------------------------------------------------------------------------------

APP_TITLE    := 3DS Audio Player
APP_AUTHOR   := You
APP_DESC     := MP3/OGG/FLAC/WAV player with pitch and speed control
APP_VERSION  := 1.0.0

TARGET       := audioplayer
BUILD        := build
SOURCES      := source
ROMFS        := romfs

ifeq ($(strip $(DEVKITPRO)),)
  $(error "Please set DEVKITPRO in your environment.")
endif
ifeq ($(strip $(DEVKITARM)),)
  $(error "Please set DEVKITARM in your environment.")
endif

CTRULIB      := $(DEVKITPRO)/libctru

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Memory layout — SYSTEM mode gives O3DS apps the full 64MB
# Without this, the default heap is tiny and C3D/C2D blow the stack
#---------------------------------------------------------------------------------
APP_MEMTYPE  := 0   # 0 = APPLICATION (standard, ~48MB usable on O3DS)

#---------------------------------------------------------------------------------
# Linker flags — explicit stack size to avoid overflow on O3DS
# Default stack is 32KB; we set 64KB to be safe
#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

INCLUDE  := -I$(SOURCES) \
            -I$(CTRULIB)/include \
            -I$(DEVKITPRO)/portlibs/3ds/include

LIBDIRS  := $(CTRULIB) $(DEVKITPRO)/portlibs/3ds
LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections \
            $(ARCH) $(INCLUDE) -DARM11 -D_3DS
CXXFLAGS := $(CFLAGS) -std=c++17
ASFLAGS  := -g $(ARCH)

# -Wl,--stack sets the stack size. 0x10000 = 64KB, safe for O3DS.
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) \
            -Wl,-Map,$(notdir $*.map) \
            -Wl,--stack,0x10000

LIBS     := -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# Source files
#---------------------------------------------------------------------------------
CFILES   := $(wildcard $(SOURCES)/*.c)
OFILES   := $(patsubst $(SOURCES)/%.c, $(BUILD)/%.o, $(CFILES))
OUTPUT   := $(CURDIR)/$(TARGET)

.PHONY: all clean

all: $(BUILD) $(OUTPUT).3dsx

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUTPUT).smdh:
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" \
	    $(CTRULIB)/default_icon.png $@

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
	3dsxtool $< $@ --smdh=$(OUTPUT).smdh --romfs=$(ROMFS)

clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).3dsx $(OUTPUT).smdh

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

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

INCLUDE  := -I$(SOURCES) \
            -I$(CTRULIB)/include \
            -I$(DEVKITPRO)/portlibs/3ds/include

LIBDIRS  := $(CTRULIB) $(DEVKITPRO)/portlibs/3ds
LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Use -D__3DS__ (modern libctru requirement)
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections \
            $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -std=c++17
ASFLAGS  := -g $(ARCH)

# 64KB stack prevents overflow on Old 3DS
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) \
            -Wl,-Map,$(notdir $*.map) \
            -Wl,--stack,0x10000

LIBS     := -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# Source files
# stb_vorbis.c lives in vendor/ but is #included by audio.c directly,
# so it must NOT appear as a standalone compilation unit here.
#---------------------------------------------------------------------------------
CFILES   := $(wildcard $(SOURCES)/*.c)
OFILES   := $(patsubst $(SOURCES)/%.c, $(BUILD)/%.o, $(CFILES))
OUTPUT   := $(CURDIR)/$(TARGET)

.PHONY: all clean

all: $(BUILD) $(OUTPUT).3dsx

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c
	$(CC) $(CFLAGS) -Ivendor -c $< -o $@

$(OUTPUT).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUTPUT).smdh:
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" \
	    $(CTRULIB)/default_icon.png $@

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
	3dsxtool $< $@ --smdh=$(OUTPUT).smdh --romfs=$(ROMFS)

clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).3dsx $(OUTPUT).smdh

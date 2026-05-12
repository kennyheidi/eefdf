#---------------------------------------------------------------------------------
# 3DS Audio Player — Makefile
# Requires devkitARM + libctru + citro2d
#---------------------------------------------------------------------------------

APP_TITLE    := 3DS Audio Player
APP_AUTHOR   := You
APP_DESC     := MP3/OGG/FLAC/WAV player with pitch and speed control
APP_VERSION  := 1.0.0

TARGET       := audioplayer
BUILD        := build
SOURCES      := source
INCLUDES     := source
ROMFS        := romfs

# devkitPro toolchain
ifeq ($(strip $(DEVKITPRO)),)
  $(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path>")
endif
ifeq ($(strip $(DEVKITARM)),)
  $(error "Please set DEVKITARM in your environment. export DEVKITARM=<path>")
endif

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections \
            $(ARCH) $(INCLUDE) -DARM11 -D_3DS
CXXFLAGS := $(CFLAGS) -std=c++17
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# Source files
#---------------------------------------------------------------------------------
CFILES   := $(wildcard $(SOURCES)/*.c)
OFILES   := $(patsubst $(SOURCES)/%.c, $(BUILD)/%.o, $(CFILES))

INCLUDE  := $(foreach dir,$(INCLUDES),-I$(dir)) \
            -I$(CTRULIB)/include \
            -I$(DEVKITPRO)/portlibs/3ds/include

LIBDIRS  := $(CTRULIB) $(DEVKITPRO)/portlibs/3ds

export VPATH    := $(CURDIR)/$(SOURCES)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

OUTPUT   := $(CURDIR)/$(TARGET)

#---------------------------------------------------------------------------------
# Targets
#---------------------------------------------------------------------------------
.PHONY: all clean

all: $(BUILD) $(OUTPUT).3dsx

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUTPUT).3dsx: $(OUTPUT).elf
	3dsxtool $< $@ --smdh=$(OUTPUT).smdh --romfs=$(ROMFS)

$(OUTPUT).smdh: $(APP_TITLE) $(APP_AUTHOR) $(APP_DESC) $(APP_VERSION)
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" icon.png $@

clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).3dsx $(OUTPUT).smdh

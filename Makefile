ifeq ($(strip $(PSL1GHT)),)
$(error "Defina a variavel PSL1GHT")
endif
include $(PSL1GHT)/ppu_rules

.DEFAULT_GOAL := all

TARGET    := slither
TITLE     := Slither.io PS3
APPID     := SLIT00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
SFOXML    := sfo.xml
PORTLIBS  := $(PS3DEV)/portlibs/ppu

MACHDEP := -mhard-float -fmodulo-sched -ffunction-sections -fdata-sections
CFLAGS  := -O2 -Wall -mcpu=cell $(MACHDEP) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include
LDFLAGS := $(MACHDEP) -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib
LIBS    := -ltiny3d -lpad -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm

all: $(TARGET).pkg

# ACHA O MAIN ONDE ELE ESTIVER!
SRC := $(firstword $(wildcard main.cpp main.c source/main.cpp source/main.c))
OBJ := main.o

$(OBJ): $(SRC)
	$(CXX) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $< $(LIBS)

$(TARGET).pkg: $(TARGET).elf ICON0.PNG $(SFOXML)
	rm -rf pkg
	mkdir -p pkg/USRDIR
	cp ICON0.PNG pkg/ICON0.PNG
	$(PS3DEV)/bin/sprxlinker $(TARGET).elf
	$(PS3DEV)/bin/make_self_npdrm $(TARGET).elf pkg/USRDIR/EBOOT.BIN $(CONTENTID)
	$(PS3DEV)/bin/sfo.py --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) pkg/PARAM.SFO
	$(PS3DEV)/bin/pkg.py --contentid $(CONTENTID) pkg/ $@

clean:
	rm -rf pkg *.o *.elf *.self *.pkg
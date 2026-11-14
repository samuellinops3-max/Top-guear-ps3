ifeq ($(strip $(PSL1GHT)),)
$(error "PSL1GHT is not set. Export it: export PSL1GHT=$$PS3DEV/psl1ght")
endif

include $(PSL1GHT)/ppu_rules

TARGET		:= PouPS3
TITLE		:= Pou PS3
APPID		:= POUPS3001
CONTENTID	:= UP0001-$(APPID)_00-0000000000000000

BUILD		:= build
SOURCES		:= .
DATA		:= data
INCLUDES	:= -I$(PSL1GHT)/ppu/include/SDL -I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL

CFLAGS		:= -O2 -Wall -mcpu=cell -mhard-float -I$(PSL1GHT)/ppu/include $(INCLUDES)
CXXFLAGS	:= $(CFLAGS) -fno-exceptions -fno-rtti -std=c++11
LDFLAGS		:= -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib

LIBS		:= -lSDL_image -lSDL -lpng -ljpeg -lz -laudio -lio -lrsx -lgcm_sys -lsysutil -lsysmodule -lm

CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

OFILES		:= $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(SFILES:.S=.o)
OFILES		:= $(OFILES:%=$(BUILD)/%)

PKGDIR		:= pkg

all: $(TARGET).pkg

pkg: $(TARGET).pkg

$(TARGET).elf: $(OFILES)
	@echo "Linking $@..."
	@$(CXX) $(OFILES) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).self: $(TARGET).elf
	@echo "Making SELF..."
	@$(SELF) $< $@

EBOOT.BIN: $(TARGET).self
	@echo "Making EBOOT.BIN..."
	@$(MAKE_SELF_NPDRM) $< $@ $(CONTENTID)

PARAM.SFO: sfo.xml
	@echo "Making PARAM.SFO..."
	@$(SFO) --from-xml $< $@

$(TARGET).pkg: EBOOT.BIN PARAM.SFO
	@echo "Making PKG..."
	@rm -rf $(PKGDIR)
	@mkdir -p $(PKGDIR)/USRDIR
	@cp EBOOT.BIN $(PKGDIR)/USRDIR/EBOOT.BIN
	@cp PARAM.SFO $(PKGDIR)/PARAM.SFO
	@if [ -d $(DATA) ]; then cp -r $(DATA) $(PKGDIR)/USRDIR/; fi
	@if [ -f ICON0.PNG ]; then cp ICON0.PNG $(PKGDIR)/ICON0.PNG; else cp $(PSL1GHT)/ppu/data/ICON0.PNG $(PKGDIR)/ICON0.PNG 2>/dev/null || true; fi
	@$(PKG) --contentid $(CONTENTID) $(PKGDIR)/ $(TARGET).pkg
	@echo "Done: $(TARGET).pkg"

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(BUILD)
	@echo "Assembling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).self EBOOT.BIN PARAM.SFO $(TARGET).pkg $(PKGDIR)

.PHONY: all pkg clean

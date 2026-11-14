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
SFO_XML		:= sfo.xml
SFO_FILE	:= PARAM.SFO

INCLUDES	:= -I$(PSL1GHT)/ppu/include/SDL

CFLAGS		:= -O2 -Wall -mcpu=cell -mhard-float -I$(PSL1GHT)/ppu/include $(INCLUDES)
LDFLAGS		:= -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib

LIBS		:= -lSDL -lSDL_ttf -lSDL_image -lm -lfreetype -lpng -ljpeg -lz -laudio -lio -lrsx -lgcm_sys -lsysutil -lsysmodule -lnet -lnetctl

CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

OFILES		:= $(CFILES:.c=.o) $(SFILES:.S=.o)
OFILES		:= $(OFILES:%=$(BUILD)/%)

PKGDIR		:= pkg
PKGF		:= $(TARGET).pkg

all: $(TARGET).elf $(TARGET).self EBOOT.BIN $(SFO_FILE)

$(TARGET).elf: $(OFILES)
	@$(CC) $(OFILES) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).self: $(TARGET).elf
	@$(SELF) $< $@

EBOOT.BIN: $(TARGET).self
	@$(SELF) --npdrm --encrypt $< $@
	@cp $@ $(TARGET).self.npdrm

$(SFO_FILE): $(SFO_XML)
	@$(SFO) --sfo $(SFO_XML) -f $@

pkg: EBOOT.BIN $(SFO_FILE)
	@rm -rf $(PKGDIR)
	@mkdir -p $(PKGDIR)/USRDIR
	@mkdir -p $(PKGDIR)/META
	@cp EBOOT.BIN $(PKGDIR)/USRDIR/EBOOT.BIN
	@cp $(SFO_FILE) $(PKGDIR)/PARAM.SFO
	@if [ -d $(DATA) ]; then cp -r $(DATA) $(PKGDIR)/USRDIR/; fi
	@echo "$(CONTENTID)" > $(PKGDIR)/META/PACKAGE_ID
	@$(PKG) --contentid $(CONTENTID) $(PKGDIR)/ $(PKGF)

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).self $(TARGET).self.npdrm EBOOT.BIN $(SFO_FILE) $(PKGF) $(PKGDIR)

.PHONY: all pkg clean
# ASTRONOMIA QUIZ PS3 - JA PRONTO
TARGET = ASTRONOMIA_QUIZ_PS3

PSL1GHT ?= /usr/local/psl1ght
include $(PSL1GHT)/ppu_rules

INCLUDE = -I$(PSL1GHT)/ppu/include -I$(PSL1GHT)/portlibs/ppu/include
LIBDIRS = -L$(PSL1GHT)/ppu/lib -L$(PSL1GHT)/portlibs/ppu/lib
LIBS = -lrsx -lgcm_sys -lio -lsysutil -lm

CFILES = main.c
OFILES = $(CFILES:.c=.o)

all: $(TARGET).elf $(TARGET).self

$(TARGET).elf: $(OFILES)
	$(CC) $(CFLAGS) $(OFILES) -o $@ $(LIBDIRS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

%.self: %.elf
	$(SPRX) $< $@

clean:
	rm -f $(OFILES) $(TARGET).elf $(TARGET).self

.PHONY: all clean
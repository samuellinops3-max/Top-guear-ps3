TITLE		:= SLITHER.IO PS3
APPID		:= SLTH00001
CONTENTID	:= UP0001-$(APPID)_00-0000000000000000

TARGET		:= slither_ps3
SOURCES		:= .
INCLUDES	:= .

LIBS		:= -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm

include $(PSL1GHT)/ppu_rules

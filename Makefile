###############################################################################
# Makefile for the project cantest
###############################################################################

## General Flags
PROJECT = avr-hokey
MCU = atmega88
TARGET = $(PROJECT).elf
CC = avr-gcc
CXX = avr-g++
PROG=hidspx


## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
F_CPU = 8000000
CXXFLAGS = $(COMMON)
CXXFLAGS += -std=c++11 -Wall -Wextra -Wconversion -gdwarf-2 -DF_CPU=$(F_CPU)UL -Os -funsigned-char -fpack-struct -fshort-enums -fno-threadsafe-statics

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS +=  -Wl,-Map=$(PROJECT).map


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings

## Objects that must be built in order to link
SRCS = $(shell ls *.cpp)
OBJECTS = $(patsubst %.cpp,%.o,$(SRCS))
DEPENDS = $(patsubst %.cpp,%.d,$(SRCS))

## Objects explicitly added by the user
LINKONLYOBJECTS = 

## fuse
EFUSE = 0xff
HFUSE = 0xdf
LFUSE = 0xe2

## Build
all: $(TARGET) $(PROJECT).hex $(PROJECT).eep $(PROJECT).lss size

## Compile
.cpp.o:
	$(CXX) $(INCLUDES) $(CXXFLAGS) -MMD -MP -c -o $@ $<

##Link
$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LINKONLYOBJECTS) $(LIBDIRS) $(LIBS) -o $(TARGET)

%.hex: $(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS)  $< $@

%.eep: $(TARGET)
	-avr-objcopy $(HEX_EEPROM_FLAGS) -O ihex $< $@ || exit 0

%.lss: $(TARGET)
	avr-objdump -h -S $< > $@

size: ${TARGET}
	@echo
	@avr-size -C --mcu=${MCU} ${TARGET}

.PHONY: write
write:
	make
	$(PROG) $(PROJECT).hex $(PROJECT).eep

.PHONY: fuse
fuse:
	$(PROG) -fX$(EFUSE) -fH$(HFUSE) -fL$(LFUSE)

## Clean target
.PHONY: clean
clean:
	-rm -rf $(OBJECTS) $(PROJECT).elf dep/* $(PROJECT).hex $(PROJECT).eep $(PROJECT).lss $(PROJECT).map $(PROJECT) $(DEPENDS)

## Other dependencies
-include $(DEPENDS)

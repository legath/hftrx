###############################################################################
# Makefile for the project tc1
###############################################################################

## General Flags
PROJECT = tc1
MCU = atxmega128a4u
TARGET = tc1_atxmega128a4u_32.elf
CC = avr-gcc
OBJDIR = ./obj

CPP = avr-g++

## Options common to compile, link and assembly rules
COMMON = -mmcu=$(MCU)

## Compile options common for all C compilation units.
CFLAGS = $(COMMON)
CFLAGS += -Wall -gdwarf-2 -std=gnu99 -flto -DF_CPU=32000000UL -Os -fsigned-char $(MAKE_DEFINES)
CFLAGS += -MD -MP -MT ./$(@D)/$(*F).o -MF ./$(@D)/$(@F).d

## Assembly specific flags
ASMFLAGS = $(COMMON)
ASMFLAGS += $(CFLAGS)
ASMFLAGS += -x assembler-with-cpp -Wa,-gdwarf2

## Linker flags
LDFLAGS = $(COMMON)
LDFLAGS += -Os -flto


## Intel Hex file production flags
HEX_FLASH_FLAGS = -R .eeprom -R .fuse -R .lock -R .signature

HEX_EEPROM_FLAGS = -j .eeprom
HEX_EEPROM_FLAGS += --set-section-flags=.eeprom="alloc,load"
HEX_EEPROM_FLAGS += --change-section-lma .eeprom=0 --no-change-warnings



PRJROOT = ../..

## Objects that must be built in order to link
SRCDIR = $(PRJROOT)/src
CSRC +=
ASRC +=
UINCDIR += $(PRJROOT) $(PRJROOT)/inc
ULIBDIR +=
ULIBS += 

SRCDIRS += $(PRJROOT)/src

# taken from
# https://stackoverflow.com/questions/2483182/recursive-wildcards-in-gnu-make/18258352#18258352
rwildcard = $(foreach d, $(wildcard $(1:=/*)), $(call rwildcard, $(d), $(2)) $(filter $(subst *, %, $(2)), $(d)))

CSRC += $(call rwildcard, $(SRCDIRS), *.c)
CPPSRC += $(call rwildcard, $(SRCDIRS), *.cpp)

VPATH = $(SRCDIR)

OBJS 	= $(patsubst %.s,$(OBJDIR)/%.o,$(ASRC)) $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CSRC))
INCDIR  = $(patsubst %,-I%,$(DINCDIR) $(UINCDIR))
LIBDIR  = $(patsubst %,-L%,$(DLIBDIR) $(ULIBDIR))

CPFLAGS = $(CFLAGS) $(LDFLAGS)

## Objects explicitly added by the user
LINKONLYOBJECTS = 

## Build
all: $(OBJS) $(TARGET) tc1_atmega644_10.hex tc1_atmega644_10.eep size

$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $(CPFLAGS) $(INCDIR) $(addprefix $(SRCDIR)/, $(patsubst obj/%, %, $(patsubst %.o, %.c, $@))) -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(@D)
	$(AS) -c $(ASFLAGS) $(addprefix $(SRCDIR)/, $(patsubst obj/%, %, $(patsubst %.o, %.s, $@))) -o $@

$(TARGET): $(OBJS)
	 $(CC) $(LDFLAGS) $(OBJS) $(LINKONLYOBJECTS) $(LIBDIR) $(LIBS) -o $(TARGET)

%.hex: $(TARGET)
	avr-objcopy -O ihex $(HEX_FLASH_FLAGS)  $< $@

%.eep: $(TARGET)
	-avr-objcopy $(HEX_EEPROM_FLAGS) -O ihex $< $@ || exit 0

%.lss: $(TARGET)
	avr-objdump -h -S $< > $@

size: ${TARGET}
	$(SIZE) --format=sysv --radix=16 ${TARGET}
	#$(SIZE) -C --mcu=${MCU} ${TARGET}

## Clean target
.PHONY: clean
clean:
	-rm -rf $(OBJS) tc1_atmega644_10.elf ./*.d tc1_atmega644_10.hex tc1_atmega644_10.eep
	-rm -fR $(OBJDIR)
	-rm -f $(PROJECT)_rom.elf $(PROJECT)_rom.map $(PROJECT)_rom.hex $(PROJECT)_rom.bin
	-rm -f $(PROJECT)_boot.elf $(PROJECT)_boot.map $(PROJECT)_boot.hex $(PROJECT)_boot.bin
	-rm -f $(PROJECT)_app.elf $(PROJECT)_app.map $(PROJECT)_app.hex $(PROJECT)_app.bin
	-rm -f $(CSRC:.c=.c.bak)
	-rm -f $(CSRC:.c=.lst)
	-rm -f $(ASRC:.s=.s.bak)
	-rm -f $(ASRC:.s=.lst)
	
# 
# Include the dependency files, should be the last of the makefile
#
-include $(call rwildcard, $(OBJDIR), *.d)


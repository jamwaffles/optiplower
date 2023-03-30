PROJECT=ds2502-emulator
AVRDUDE_MCU?=t85

AVRDUDE_FLAGS?=-c usbasp -P /dev/ttyUSB0
AVRDUDE_FLAGS+=-p $(AVRDUDE_MCU)

FUSES?=-U lfuse:w:0xe2:m -U hfuse:w:0xd7:m -U efuse:w:0xff:m

all: $(PROJECT).ino.hex

$(PROJECT).ino.hex:
	arduino-cli compile --profile default --build-path ./build

program: $(PROJECT).ino.hex
	avrdude $(AVRDUDE_FLAGS) -U flash:w:./build/$<

load_eeprom: eeprom-data.hex
	avrdude $(AVRDUDE_FLAGS) -U eeprom:w:$<

fuses:
	avrdude $(AVRDUDE_FLAGS) $(FUSES)

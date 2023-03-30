PROJECT=ds2502-emulator
GCC_MCU?=attiny85
AVRDUDE_MCU?=t85

AVRDUDE_FLAGS?=-c usbasp -P /dev/ttyUSB0
AVRDUDE_FLAGS+=-p $(AVRDUDE_MCU)

FUSES?=-U lfuse:w:0xe2:m -U hfuse:w:0xd7:m -U efuse:w:0xff:m

all: $(PROJECT).ino.hex

$(PROJECT).ino.hex:
	arduino-cli compile --profile default --build-path ./build

program: $(PROJECT).ino.hex
	avrdude $(AVRDUDE_FLAGS) -U flash:w:./build/$<

fuses:
	avrdude $(AVRDUDE_FLAGS) $(FUSES)

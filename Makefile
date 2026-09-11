TARGET := cube_manipulator
BUILD := build

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfpv3-d16

CFLAGS := -Wall -O2 -g $(ARCH) -MMD -MP -D__3DS__
CXXFLAGS := -Wall -O2 -g $(ARCH) -MMD -MP -std=c++11 -fno-rtti -fno-exceptions

LDFLAGS := -specs=3dsx.specs -g $(ARCH)
LIBS := -L$(DEVKITARM)/arm-none-eabi/lib/armv6k/fpu -L$(DEVKITPRO)/libctru/lib -Wl,--start-group -lctru -lcitro3d -lcitro2d -lm -lgcc -lc -Wl,--end-group

PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(DEVKITPRO)/libctru/bin:$(PATH)

CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
CXX := $(DEVKITARM)/bin/arm-none-eabi-g++
LD := $(DEVKITARM)/bin/arm-none-eabi-g++

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).3dsx

$(BUILD)/$(TARGET).elf: $(BUILD)/$(TARGET).o $(BUILD)/shaders/cube.shbin.o
	$(LD) $(BUILD)/$(TARGET).o $(BUILD)/shaders/cube.shbin.o -o $@ $(LDFLAGS) $(LIBS)

$(BUILD)/$(TARGET).o: source/main.cpp cube_shbin.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(DEVKITPRO)/libctru/include -I$(DEVKITARM)/include -I. -c $< -o $@

cube_shbin.h $(BUILD)/shaders/cube.shbin.o &: $(BUILD)/shaders/cube.shbin
	@mkdir -p $(dir $(BUILD)/shaders/cube.shbin.o)
	bin2s -a 4 -H cube_shbin.h $< > $(BUILD)/shaders/cube.s
	$(CC) -x assembler-with-cpp $(CFLAGS) -c $(BUILD)/shaders/cube.s -o $(BUILD)/shaders/cube.shbin.o

$(BUILD)/shaders/cube.shbin: source/shaders/cube.v.pica
	@mkdir -p $(dir $@)
	picasso -o $@ $<

$(BUILD)/$(TARGET).3dsx: $(BUILD)/$(TARGET).elf
	3dsxtool $< $@

clean:
	rm -rf $(BUILD) cube_shbin.h

.PHONY: all clean
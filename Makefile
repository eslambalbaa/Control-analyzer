CXX       ?= g++
CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc
BUILD_DIR := build

ifeq ($(OS),Windows_NT)
	EXE := .exe
	MKDIR_BUILD = if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
	RM_BUILD    = if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
else
	EXE :=
	MKDIR_BUILD = mkdir -p "$(BUILD_DIR)"
	RM_BUILD    = rm -rf "$(BUILD_DIR)"
endif

TARGET := $(BUILD_DIR)/control_backend$(EXE)

.PHONY: all clean

all: $(TARGET)

$(TARGET): src/main.cpp src/signal_flow_graph.hpp src/routh_table.hpp
	$(MKDIR_BUILD)
	$(CXX) $(CXXFLAGS) src/main.cpp -o $(TARGET)

clean:
	$(RM_BUILD)

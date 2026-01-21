# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../Rack-SDK

# Source files
SOURCES += src/plugin.cpp
SOURCES += src/Sequencer.cpp

# Include distributables
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# Include the VCV Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

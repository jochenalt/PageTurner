# -------------------------------------------------------------------
# Makefile to build libei_classifier.so (Edge Impulse + inference.cpp)
# -------------------------------------------------------------------

# — Tools
CXX       := g++
CC        := gcc
FIND      := find

# — Paths
SDK_DIR        := ../pageturner/edge-impulse-sdk
MODEL_CPP_DIR  := ../pageturner/tflite-model
BUILD_DIR      := build
TARGET_SO      := $(BUILD_DIR)/libPageTurnerInference.so

# — Flags
INCLUDES   := -I../pageturner -I$(SDK_DIR)
CFLAGS     := -fPIC $(INCLUDES)
CXXFLAGS   := -std=c++11 -fPIC $(INCLUDES)
LDFLAGS    := -shared -lm -lstdc++

# — Source file lists
SRCS_C      := $(shell $(FIND) $(SDK_DIR) -name '*.c'   -print)
SRCS_CC     := $(shell $(FIND) $(SDK_DIR) -name '*.cc'  -print)
SRCS_CPP_S  := $(shell $(FIND) $(SDK_DIR) -name '*.cpp' -print)
SRCS_CPP_M  := $(shell $(FIND) $(MODEL_CPP_DIR) -name '*.cpp' -print)
SRCS_CPP    := src/inference.cpp $(SRCS_CPP_S) $(SRCS_CPP_M)

SRCS        := $(SRCS_C) $(SRCS_CC) $(SRCS_CPP)

# — Object files
OBJS  := $(SRCS:.c=.o)
OBJS  := $(OBJS:.cc=.o)
OBJS  := $(OBJS:.cpp=.o)

# — Rules
.PHONY: all clean

all: $(TARGET_SO)

$(TARGET_SO): $(OBJS) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# compile C sources
%.o: %.c
	$(CC)   $(CFLAGS)  -c $< -o $@

# compile C++ sources
%.o: %.cc
	$(CXX)  $(CXXFLAGS) -c $< -o $@
%.o: %.cpp
	$(CXX)  $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)/*.so $(OBJS)

# MoshBrosh CLI Makefile

CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall

# FFmpeg paths (Homebrew on Apple Silicon)
FFMPEG_PATH = /opt/homebrew/Cellar/ffmpeg/8.0.1
INCLUDES = -I$(FFMPEG_PATH)/include
LIBS = -L$(FFMPEG_PATH)/lib -lavformat -lavcodec -lavutil -lswscale

TARGET = moshbrosh
SRCS = moshbrosh_cli.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean

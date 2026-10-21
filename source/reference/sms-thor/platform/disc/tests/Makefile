# Host test for platform/disc (reads the user's disc image read-only).
#   make run ISO=/path/game.iso FILES=/path/extracted/files [FULL=1]
ARCH ?= -m32
CXX  ?= g++
CXXFLAGS ?= -O2 -g -std=gnu++11 -Wall -D_FILE_OFFSET_BITS=64
WORK ?= $(abspath ../../..)/build-disc-test
$(WORK)/disc_test: disc_test.cc ../gcdisc.cpp ../gcdisc.h
	mkdir -p $(WORK)
	$(CXX) $(ARCH) $(CXXFLAGS) -I../../../src -o $@ disc_test.cc ../gcdisc.cpp
run: $(WORK)/disc_test
	TMPDIR=$(WORK) $(WORK)/disc_test
clean:
	rm -rf $(WORK)
.PHONY: run clean

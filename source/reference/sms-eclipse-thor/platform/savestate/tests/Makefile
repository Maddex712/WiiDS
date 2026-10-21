# Host test for platform/savestate's file format and compression.
#   make run
CXX  ?= g++
CXXFLAGS ?= -O2 -g -std=gnu++11 -Wall -Wextra
WORK ?= $(abspath ../../..)/build-savestate-test
$(WORK)/state_file_test: state_file_test.cc ../state_file.cpp ../state_file.h
	mkdir -p $(WORK)
	$(CXX) $(CXXFLAGS) -I.. -o $@ state_file_test.cc ../state_file.cpp
run: $(WORK)/state_file_test
	TMPDIR=$(WORK) $(WORK)/state_file_test
clean:
	rm -rf $(WORK)
.PHONY: run clean

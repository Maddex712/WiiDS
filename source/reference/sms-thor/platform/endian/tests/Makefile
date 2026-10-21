# Host tests for platform/endian. Reads the extracted disc (read-only).
#   make run DISC=/path/to/GMSE01/files [ARCH=-m32]
ARCH ?= -m32
CXX ?= g++
CXXFLAGS ?= -O1 -g -std=gnu++11 -Wall -Wno-multichar -Wno-unused-function
SRCS = ../endian_j3d.cpp ../endian_jpa.cpp ../endian_jaudio.cpp ../endian_dispatch.cpp ../endian_game.cpp ../endian_seq.cpp
endian_test: endian_test.cc $(SRCS) ../endian_util.h ../port_endian.h
	$(CXX) $(ARCH) $(CXXFLAGS) -DPORT_ENDIAN_STANDALONE -o $@ endian_test.cc $(SRCS)
run: endian_test
	./endian_test
clean:
	rm -f endian_test
.PHONY: run clean

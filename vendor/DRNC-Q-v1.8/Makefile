CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
ROOT_OUT := ../../bin/drnq
SRC := src/drnq.cpp src/qpc_backend.inc
.PHONY: all clean check
all: $(ROOT_OUT)
$(ROOT_OUT): $(SRC)
	$(CXX) $(CXXFLAGS) src/drnq.cpp -o $(ROOT_OUT)
	chmod 755 $(ROOT_OUT)
check: all
	$(ROOT_OUT) --version
clean:
	rm -f $(ROOT_OUT)

# Host tests (no Arduino needed). Usage: make -C test
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -I../src

all: test_all
	./test_all

test_all: test_all.cpp $(wildcard ../src/*.h)
	$(CXX) $(CXXFLAGS) test_all.cpp -o test_all

examples:
	@for e in WriterBasic ReaderBasic PrettyPrintAndErrors JsonSpeedTest; do \
	  printf '#include "Arduino.h"\n#include "../examples/%s/%s.ino"\nint main(){ setup(); loop(); }\n' $$e $$e > example_$$e.cpp; \
	  $(CXX) $(CXXFLAGS) -DARDUINO -Imock example_$$e.cpp -o example_$$e && ./example_$$e > /dev/null || exit 1; \
	  rm -f example_$$e.cpp example_$$e; done

clean:
	rm -f test_all example_*

.PHONY: all examples clean

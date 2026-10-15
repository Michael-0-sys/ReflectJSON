# Host tests (no Arduino needed). Usage:
#   make -C test
#   make -C test examples
#   make -C test ini REFLECTINI=../../ReflectINI/src
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -I../src
REFLECTINI ?=

EXAMPLES = $(filter-out 15_INI_JSON_SameStruct,$(notdir $(wildcard ../examples/*)))

all: test_all
	./test_all

test_all: test_all.cpp $(wildcard ../src/*.h)
	$(CXX) $(CXXFLAGS) test_all.cpp -o test_all

examples:
	@for e in $(EXAMPLES); do \
	  printf '#include "Arduino.h"\n#include "../examples/%s/%s.ino"\nint main(){ setup(); loop(); }\n' $$e $$e > example_$$e.cpp; \
	  $(CXX) $(CXXFLAGS) -DARDUINO -Imock example_$$e.cpp -o example_$$e && ./example_$$e > /dev/null || exit 1; \
	  echo "ok  $$e"; rm -f example_$$e.cpp example_$$e; done

ini:
	@test -n "$(REFLECTINI)" || { echo "set REFLECTINI=<path to ReflectINI/src>"; exit 1; }
	@e=15_JSON_INI_SameStruct; \
	printf '#include "Arduino.h"\n#include "../examples/%s/%s.ino"\nint main(){ setup(); loop(); }\n' $$e $$e > example_$$e.cpp; \
	$(CXX) $(CXXFLAGS) -DARDUINO -Imock -I$(REFLECTINI) example_$$e.cpp -o example_$$e && ./example_$$e > /dev/null && echo "ok  $$e"; \
	rm -f example_$$e.cpp example_$$e

clean:
	rm -f test_all example_*

.PHONY: all examples ini clean

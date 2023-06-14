CC := g++
CFLAGS := -std=c++20 -Wall -g -DDEBUG
LIBS := -lfmt -lSDL2

SRC_FILES := main.cpp networking.cpp math.cpp Player.cpp Map.cpp

DEBUG: adhoctopia

.PHONY: all clean

all: adhoctopia

adhoctopia: $(SRC_FILES)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm adhoctopia 

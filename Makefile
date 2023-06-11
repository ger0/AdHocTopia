CC := g++
CFLAGS := -std=c++20 -Wall -Wextra
DEBUG_FLAG := -DDEBUG -g
LIBS := -lfmt -lSDL2

SRC_FILES := main.cpp

ifdef DEBUG
CFLAGS += $(DEBUG_FLAG)
endif

.PHONY: all clean

all: adhoctopia

adhoctopia: $(SRC_FILES)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm adhoctopia 

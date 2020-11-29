CUR_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

CC = gcc
#-Werror -Wall
CFLAGS += -g -O2  -lpthread
LDFLAGS +=

DEPS_DIR  := $(CUR_DIR)/.deps$(LIB_SUFFIX)
DEPCFLAGS = -MD -MF $(DEPS_DIR)/$*.d -MP

SRC_FILES = $(wildcard *.c)

EXE_FILES = $(SRC_FILES:.c=)

all: $(EXE_FILES)
	echo $(EXE_FILES)

%/%.c:%.c $(DEPS_DIR)
	$(CC) $(CFLAGS) $(DEPCFLAGS) -c $@ $<

clean:
	rm -f $(EXE_FILES)

.PHONY: all clean

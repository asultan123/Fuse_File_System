#
# file:        Makefile - programming assignment 3
#

CFLAGS = -ggdb3 -Wall -O0
LDLIBS = -lcheck -lz -lm -lsubunit -lrt -lpthread -lfuse

unittest-1: unittest-1.o homework.o misc.o

unittest-2: unittest-2.o homework.o misc.o

hwfuse: misc.o homework.o hwfuse.o

all: unittest-1 unittest-2 hwfuse test.img

# force test.img, test2.img to be rebuilt each time
.PHONY: test.img test2.img

test.img: 
	python gen-disk.py -q disk1.in test.img

test2.img: 
	python gen-disk.py -q disk2.in test2.img

clean: 
	rm -f *.o unittest-1 unittest-2 hwfuse test.img test2.img

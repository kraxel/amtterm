
CC	?= gcc
CFLAGS	?= -Os -g
CFLAGS	+= -Wall -Wno-pointer-sign

TARGETS	:= amtterm

all: $(TARGETS)

clean:
	rm -f *.o *~
	rm -f $(TARGETS)

amtterm: amtterm.o tcp.o
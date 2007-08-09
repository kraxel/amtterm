
DESTDIR	?=
prefix	?= /usr
bindir	?= $(prefix)/bin

CC	?= gcc
CFLAGS	?= -Os -g
CFLAGS	+= -Wall -Wno-pointer-sign
CFLAGS	+= -DVERSION='"$(shell cat VERSION)"'

TARGETS	:= amtterm

all: $(TARGETS)

install: $(TARGETS)
	mkdir -p $(DESTDIR)$(bindir)
	install -s $(TARGETS) $(DESTDIR)$(bindir)

clean:
	rm -f *.o *~
	rm -f $(TARGETS)

amtterm: amtterm.o tcp.o


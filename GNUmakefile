# config
srcdir	= .
VPATH	= $(srcdir)
-include Make.config
include $(srcdir)/mk/Variables.mk

CFLAGS	+= -Wall -Wno-pointer-sign
CFLAGS	+= -DVERSION='"$(VERSION)"'

TARGETS	:= amtterm

all: build

#################################################################
# poor man's autoconf ;-)

include mk/Autoconf.mk

define make-config
LIB		:= $(LIB)
HAVE_GTK	:= $(call ac_pkg_config,gtk+-x11-2.0)
HAVE_VTE	:= $(call ac_pkg_config,vte)
endef

#################################################################

# build gamt?
ifeq ($(HAVE_GTK)$(HAVE_VTE),yesyes)
  TARGETS += gamt
  gamt : CFLAGS += -Wno-strict-prototypes
  gamt : pkglst += gtk+-x11-2.0 vte
endif

CFLAGS += $(shell test "$(pkglst)" != "" && pkg-config --cflags $(pkglst))
LDLIBS += $(shell test "$(pkglst)" != "" && pkg-config --libs   $(pkglst))

#################################################################

build: $(TARGETS)

install: build
	mkdir -p $(bindir)
	install -s $(TARGETS) $(bindir)

clean:
	rm -f *.o *~
	rm -f $(TARGETS)

distclean: clean
	rm -f Make.config

#################################################################

amtterm: amtterm.o redir.o tcp.o
gamt: gamt.o redir.o tcp.o parseconfig.o

#################################################################

include mk/Compile.mk
include mk/Maintainer.mk
-include $(depfiles)

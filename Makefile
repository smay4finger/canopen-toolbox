EXECUTABLE=canopentool
OBJECTS=canopentool.o socketcan.o heartbeat.o nmt.o sdo.o
SYMLINKS=nmt sdo-upload sdo-download sdo-read sdo-write heartbeat

CFLAGS=-O2 -w -Wall -Wextra -g

LDFLAGS=
LDLIBS=-lncurses


all: $(EXECUTABLE)
$(EXECUTABLE): $(OBJECTS)

clean:
	-$(RM) $(EXECUTABLE) $(OBJECTS)

install: all
	/usr/bin/install --mode=755 canopentool $(DESTDIR)/usr/bin/canopentool
	ln -s canopentool $(DESTDIR)/usr/bin/nmt
	ln -s canopentool $(DESTDIR)/usr/bin/sdo-upload
	ln -s canopentool $(DESTDIR)/usr/bin/sdo-download
	ln -s canopentool $(DESTDIR)/usr/bin/sdo-read
	ln -s canopentool $(DESTDIR)/usr/bin/sdo-write
	ln -s canopentool $(DESTDIR)/usr/bin/heartbeat

.PHONY: all clean install

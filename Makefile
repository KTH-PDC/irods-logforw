# File: Makefile.

# Make file for the log forward utility.

# Destination binaries directory.
BINDIR=/usr/bin

# Manual pages.
MANDIR=/usr/man/man1

# C compiler.
CC=gcc

# C compiler switches.
CSWITCH=

# Distribution file.
DIST=$(HOME)/tar/logforw-0.1-`uname -s`-`uname -p`.tar

# First pseudo target.
all: logforw

# Compile.
logforw: logforw.c
	$(CC) $(CSWITCH) -o logforw logforw.c

# Test.
test: logforw
	./logforw -v .

# Print daemon status.
status:
	./logforw-status

# Stop daemon.
stop:
	./logforw-stop

# Start daemon.
start:
	./logforw-start

# Cleanup.
clean:
	rm -f logforw

# Full cleanup.
distclean: clean

# Create distribution.
dist: logforw
	(cd ..; tar -cf $(DIST) ./logforw)
	gzip -f $(DIST)

install: logforw
	cp logforw $(BINDIR)
	cp logforw.1 $(MANDIR)
	cp logforw-errpt $(BINDIR)
	cp logforw-errpt.1 $(MANDIR)
	cp logforw-stop $(BINDIR)
	cp logforw-stop.1 $(MANDIR)
	cp logforw-start $(BINDIR)
	cp logforw-start.1 $(MANDIR)
	cp logforw-status $(BINDIR)
	cp logforw-status.1 $(MANDIR)

uninstall:
	rm $(BINDIR)/logforw
	rm $(MANDIR)/logforw.1
	rm $(BINDIR)/logforw-errpt
	rm $(MANDIR)/logforw-errpt.1
	rm $(BINDIR)/logforw-stop
	rm $(MANDIR)/logforw-stop.1
	rm $(BINDIR)/logforw-start
	rm $(MANDIR)/logforw-start.1
	rm $(BINDIR)/logforw-status
	rm $(MANDIR)/logforw-status.1

# End of file Makefile.


BINDIR = /usr/bin
SCRIPTDIR = /usr/share/pmdebug-tools
DESTDIR =

install:
	install -m 755 -d $(DESTDIR)$(BINDIR)
	install -m 0755 s3test $(DESTDIR)$(BINDIR)
	install -m 0755 s4test $(DESTDIR)$(BINDIR)
	install -m 755 -d $(DESTDIR)$(SCRIPTDIR)
	install -m 0644 s3.stp $(DESTDIR)$(SCRIPTDIR)/s3.stp
	install -m 0644 s4.stp $(DESTDIR)$(SCRIPTDIR)/s4.stp

.PHONY: install

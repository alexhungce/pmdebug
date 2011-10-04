locatehang: locatehang.o

BINDIR = /usr/bin
DESTDIR =

CFLAGS += -O2 -Wall -Werror

clean:
	rm -f locatehang locatehang.o

install: locatehang
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 locatehang $(DESTDIR)$(BINDIR)

.PHONY: install

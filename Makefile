all: locatehang systemtap

locatehang: force
	cd  locatehang && $(MAKE) $(MFLAGS)

clean:
	cd  locatehang && $(MAKE) $(MFLAGS) clean

force:
	true

install: locatehang
	cd locatehang && $(MAKE) $(MFLAGS) install
	cd systemtap && $(MAKE) $(MFLAGS) install
	
.PHONY: install

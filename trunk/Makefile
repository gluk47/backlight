SHELL   = /bin/bash
BIN     ?= backlight
INSTDIR ?= /usr/local/bin
INSTALL = install -o root -m 4755 "$(BIN)" "$(INSTDIR)/"

all: $(BIN)

%: %.cc
	F=$<; g++ -std=c++0x -o $${F%.cc} $<
	
install: all
	$(INSTALL)
	
uninstall:
	rm -v $(INSTDIR)/$(BIN)

# copy the binary to /tmp/, then `sudo make install`. My svn co is within encfs which is inaccessible for root, hence for sudo
indirect-install: all
	cp "$(BIN)" /tmp/ && cd /tmp && sudo $(INSTALL) && rm "$(BIN)"
	
# ci:
# 	revision= \
# 	sed -re 's/(version = "[0-9]+\.[0-9]+\.)[0-9]*";/\1' -i backlight
# 	svn ci

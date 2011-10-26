CFLAGS=-Wall
CPPFLAGS=-Wall
LDFLAGS=-lm  -ldie -lquickfix  -L . -lsettings

all: libsettings.so libdie.so liquidity

lib%.so: %.c
	cc -fpic -shared -c $(CFLAGS) -o $@ $<

liquidity: liquidity.cpp
	c++ -Wall -ldie -lquickfix -lncurses -L. -lsettings -o $@ $<

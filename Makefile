LIBDIR=/usr/local/lib/fcitx
SHAREDIR=/usr/local/share/fcitx

all: fcitx-canna.so

canna.o: canna.c canna.h
	cc -c -o canna.o -fPIC -I/usr/local/include canna.c

fcitx-canna.so:	canna.o
	cc -fPIC -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter \
		-fvisibility=hidden -Wl,--as-needed -shared \
		-o fcitx-canna.so canna.o -L/usr/local/lib \
		-Wl,-rpath,/usr/local/lib: -lcanna

install: fcitx-canna.so fcitx-canna.conf canna.conf
	install -c -m 444 -o root fcitx-canna.conf $(SHAREDIR)/addon/
	install -c -m 444 -o root canna.conf $(SHAREDIR)/inputmethod/
	install -c -m 555 -o root fcitx-canna.so $(LIBDIR)/

clean:
	rm -f canna.o fcitx-canna.so *~

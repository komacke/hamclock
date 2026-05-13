# Makefile for HamClock on linux or macos
# type "make help" for possible targets

# HamClock can be built for 16 or 32 bit frame buffers. The default is 32 but either size may be
# specified explicitly by setting FB_DEPTH either here or on the command line to 16 or 32.
# FB_DEPTH=16

# HamClock setup can ask for WiFi creds but by default only does so for fb0 systems. Even this may be
# disabled by setting WIFI_NEVER either here or on the command line to 1.
# WIFI_NEVER=1

# always runs these non-file targets
.PHONY: clean clobber help hclibs

# build flags common to all options and architectures
CXXFLAGS = -IArduinoLib -IwsServer/include -Izlib-hc -I. -g -O2 -Wall -pthread -std=c++17
# CXXFLAGS += -Wextra -pedantic -Werror -Wno-attributes -Wno-unknown-pragmas

# add explicit framebuffer depth as _FB_DEPTH if defined
ifdef FB_DEPTH
    CXXFLAGS += -D_FB_DEPTH=$(FB_DEPTH)
endif

# add server alias as _T if defined
ifdef T
    CXXFLAGS += -D_T=$(T)
endif
# add backend server as _B if defined
ifdef B
    CXXFLAGS += -D_B=$(B)
endif

# add software download server as _S if defined
ifdef S
    CXXFLAGS += -D_S=$(S)
endif
# handle WiFi configuration
ifeq ($(WIFI_NEVER),1)
    CXXFLAGS += -D_WIFI_NEVER
endif

LDXXFLAGS = -LArduinoLib -LwsServer -Lzlib-hc -g -pthread
LIBS = -lpthread -larduino -lzlib-hc -lws
CXX = g++
PYTHON = python3

# macOS does not have X11 by default; this assumes XQuartz or macports xorg has been installed
ifeq ($(shell uname -s), Darwin)
    CXXFLAGS += -I/opt/X11/include -I/opt/local/include
    LDXXFLAGS += -L/opt/X11/lib -L/opt/local/lib
endif

# check for proper version of gpiod on linux
ifeq ($(shell GF=/usr/include/gpiod.h; if [ -r $$GF ] && grep -q "gpiod_line " $$GF; then echo yes; fi), yes)
    CXXFLAGS += -D_USE_GPIOD
endif

# FreeBSD needs libgpio libexecinfo and local xorg
ifeq ($(shell uname -s), FreeBSD)
    CXXFLAGS += -I/usr/local/include
    LDXXFLAGS += -L/usr/local/lib
    LIBS += -lgpio -lexecinfo -lm
endif

# NetBSD
ifeq ($(shell uname -s), NetBSD)
    CXXFLAGS += -I/usr/pkg/include -I/usr/X11R7/include
    LDXXFLAGS += -L/usr/pkg/lib -R/usr/pkg/lib -L/usr/X11R7/lib -R/usr/X11R7/lib
    LIBS += -lexecinfo -lm
endif

# Linux needs libgpiod
ifeq ($(shell find /usr/lib -name libgpiod.a | wc -l), 1)
    LIBS += -lgpiod
endif


# make CXXFLAGS available to sub makes
export CXXFLAGS


OBJS = \
	BME280.o \
	ESPHamClock.o \
	Germano-Bold-16.o \
	Germano-Bold-30.o \
	Germano-Regular-16.o \
	OTAupdate.o \
	P13.o \
	adif.o \
	adif_parser.o \
    antenna_lookup.o \
	antennas.o \
	antennas-html.o \
	asknewpos.o \
	astro.o \
	bands.o \
	blinker.o \
	bmp.o \
	brightness.o \
	cachefile.o \
	callsign.o \
	clocks.o \
	cities.o \
	color.o \
	configs.o \
	contests.o \
	cputemp.o \
	debug.o \
	drawextra.o \
	dxcluster.o \
	dxpeds.o \
	dxpeds_hide.o \
	earthmap.o \
	earthsat.o \
	emetool.o \
	favicon.o \
	fsfree.o \
	gimbal.o \
	gpsd.o \
	grayline.o \
	kd3tree.o \
	lightning.o \
	infobox.o \
	liveweb.o \
	liveweb-html.o \
	magdecl.o \
	maidenhead.o \
	mapmanage.o \
	menu.o \
	moon_imgs.o \
	moonpane.o \
	ncdxf.o \
	nmea.o \
	nvram.o \
	ontheair.o \
	parsespot.o \
	passwd.o \
	plot.o \
	plotmap.o \
	plotmgmnt.o \
	prefixes.o \
	pskreporter.o \
	qrz.o \
	radio.o \
	robinson.o \
	rss.o \
	runner.o \
	santa.o \
	sattool.o \
	scrollbar.o \
	scrollstate.o \
	sdo.o \
	selectFont.o \
	setup.o \
	sevenseg.o \
	spacewx.o \
	sphere.o \
	spots.o \
	stopwatch.o \
	string.o \
	tooltip.o \
	touch.o \
	tz.o \
	version.o \
	webserver.o \
	watchlist.o \
	wifi.o \
	wifimeter.o \
	wx.o \
	zones.o

help:
	@printf "\nThe following targets are available (as appropriate for your system)\n\n"
	@printf "    hamclock-800x480          X11 GUI desktop version, AKA hamclock\n"
	@printf "    hamclock-1600x960         X11 GUI desktop version, larger, AKA hamclock-big\n"
	@printf "    hamclock-2400x1440        X11 GUI desktop version, larger yet\n"
	@printf "    hamclock-3200x1920        X11 GUI desktop version, huge\n"
	@printf "\n";
	@printf "    hamclock-web-800x480      web server only (no local display)\n"
	@printf "    hamclock-web-1600x960     web server only (no local display), larger\n"
	@printf "    hamclock-web-2400x1440    web server only (no local display), larger yet\n"
	@printf "    hamclock-web-3200x1920    web server only (no local display), huge\n"
	@printf "\n";
	@printf "    hamclock-fb0-800x480      RPi stand-alone /dev/fb0, AKA hamclock-fb0-small\n"
	@printf "    hamclock-fb0-1600x960     RPi stand-alone /dev/fb0, larger, AKA hamclock-fb0\n"
	@printf "    hamclock-fb0-2400x1440    RPi stand-alone /dev/fb0, larger yet\n"
	@printf "    hamclock-fb0-3200x1920    RPi stand-alone /dev/fb0, huge\n"
	@printf "\n";
	@printf "Optional command line variables which may be set before the desired target:\n"
	@printf "    FB_DEPTH=16 or 32         - Specify a given frame buffer pixel size (default is 32 on all but fb0)\n"
	@printf "    WIFI_NEVER=1              - Disable WiFi fields in setup (already the default on all but fb0)\n"
	@printf "    B=backend                 - Set backend server default so -b is not required, format host:port\n"
	@printf "    S=software                - Set software server default so -S is not required. format host.domain\n"
	@printf "    T=time                    - Set timout default (seconds) so -b is not required if timout to backend need to be increased\n"

# supporting libs
hclibs:
	$(MAKE) -C ArduinoLib libarduino.a
	$(MAKE) -C wsServer libws.a
	$(MAKE) -C zlib-hc libzlib-hc.a



# X11 versions

# N.B. do it but also remain backward compatable

hamclock-big: hamclock-1600x960
	cp $? $@

hamclock: hamclock-800x480
	cp $? $@


hamclock-800x480: CXXFLAGS+=-D_USE_X11
hamclock-800x480: LIBS+=-lX11
hamclock-800x480: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-1600x960: CXXFLAGS+=-D_USE_X11 -D_CLOCK_1600x960
hamclock-1600x960: LIBS+=-lX11
hamclock-1600x960: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-2400x1440: CXXFLAGS+=-D_USE_X11 -D_CLOCK_2400x1440
hamclock-2400x1440: LIBS+=-lX11
hamclock-2400x1440: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-3200x1920: CXXFLAGS+=-D_USE_X11 -D_CLOCK_3200x1920
hamclock-3200x1920: LIBS+=-lX11
hamclock-3200x1920: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)




# web server versions


hamclock-web-800x480: CXXFLAGS+=-D_WEB_ONLY
hamclock-web-800x480: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-web-1600x960: CXXFLAGS+=-D_WEB_ONLY -D_CLOCK_1600x960
hamclock-web-1600x960: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-web-2400x1440: CXXFLAGS+=-D_WEB_ONLY -D_CLOCK_2400x1440
hamclock-web-2400x1440: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-web-3200x1920: CXXFLAGS+=-D_WEB_ONLY -D_CLOCK_3200x1920
hamclock-web-3200x1920: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)




# RPi fb0 versions

hamclock-fb0-small: hamclock-fb0-800x480
	cp $? $@

hamclock-fb0: hamclock-fb0-1600x960
	cp $? $@


hamclock-fb0-800x480: CXXFLAGS+=-D_USE_FB0
hamclock-fb0-800x480: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-fb0-1600x960: CXXFLAGS+=-D_USE_FB0 -D_CLOCK_1600x960
hamclock-fb0-1600x960: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-fb0-2400x1440: CXXFLAGS+=-D_USE_FB0 -D_CLOCK_2400x1440
hamclock-fb0-2400x1440: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)


hamclock-fb0-3200x1920: CXXFLAGS+=-D_USE_FB0 -D_CLOCK_3200x1920
hamclock-fb0-3200x1920: $(OBJS) hclibs
	$(CXX) $(LDXXFLAGS) $(OBJS) -o $@ $(LIBS)



# /usr/local/bin seems right although Alpine linux does not have it even though it is in the default PATH
install:
	@SOURCE=hamclock-*0x*0 ; \
	DIR=/usr/local/bin ; \
	TARGET=$$DIR/hamclock ; \
	if ! [ -x $$SOURCE ] ; then \
		echo 'make something first' ; \
		make help ; \
	elif [ `id -un` != 'root' ] ; then \
		echo please run with sudo ; \
	else \
		mkdir -p $$DIR \
		&& mv -f $$SOURCE $$TARGET \
		&& chown root $$TARGET \
		&& chmod u+s $$TARGET; \
	fi

clean clobber:
	$(MAKE) -C ArduinoLib clean
	$(MAKE) -C wsServer clean
	$(MAKE) -C zlib-hc clean
	touch x.o x.dSYM hamclock hamclock-
	rm -rf *.o *.dSYM hamclock hamclock-*


.PHONY: test test-eeprom

test: test-eeprom

test-eeprom:
	$(PYTHON) ../tests/test_eeprom.py

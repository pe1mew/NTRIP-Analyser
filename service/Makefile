# Makefile for ntrip-monitord (Linux; also builds under MinGW for dev).
#
# From this directory:            make
# From the repository root:       make -C service
#
# The daemon links the same core and session sources as the CLI and GUI;
# there is no separate library step yet (that arrives with CMake,
# design/architecture.md par. 8).

CC      ?= gcc
CFLAGS  ?= -g -O2 -Wall -Wextra -std=c99 -D_USE_MATH_DEFINES
# Strict -std=c99 hides POSIX declarations on glibc; without these,
# clock_gettime, CLOCK_MONOTONIC, getaddrinfo and strncasecmp all fail
# to resolve.  MinGW does not need them but tolerates them.
CPPFLAGS = -I../src -I../lib/cJSON -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

SRC = ntrip-monitord.c \
      ../src/session/ntrip_session.c \
      ../src/net/ntrip_proto.c \
      ../src/core/ns_stats.c \
      ../src/core/rtcm3x_parser.c \
      ../src/core/nmea_parser.c \
      ../src/core/sv_ephemeris.c \
      ../src/core/sv_orbit.c \
      ../src/core/sv_track.c \
      ../src/core/iono.c \
      ../src/core/rinex_nav.c \
      ../lib/cJSON/cJSON.c

BIN = ntrip-monitord

# Windows needs winsock; everything else needs only libm.
ifeq ($(OS),Windows_NT)
LDLIBS = -lws2_32 -lm
BIN   := $(BIN).exe
else
LDLIBS = -lm
endif

.PHONY: all clean install

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRC) $(LDLIBS)

clean:
	rm -f $(BIN) ntrip-monitord.exe

# Installs the daemon, the systemd unit and the Munin plugin.
# Run as root; then:
#   systemctl daemon-reload && systemctl enable --now ntrip-monitord
#   ln -s /usr/local/share/munin/plugins/ntrip_monitor /etc/munin/plugins/
PREFIX ?= /usr/local
install: $(BIN)
	install -D -m 755 $(BIN) $(DESTDIR)$(PREFIX)/sbin/$(BIN)
	install -D -m 644 ntrip-monitord.service \
	    $(DESTDIR)/etc/systemd/system/ntrip-monitord.service
	install -D -m 755 munin/ntrip_monitor \
	    $(DESTDIR)$(PREFIX)/share/munin/plugins/ntrip_monitor
	install -D -m 640 monitord.example.json \
	    $(DESTDIR)/etc/ntrip-monitord/monitord.example.json
	install -D -m 644 ntrip-monitord.sysusers \
	    $(DESTDIR)/usr/lib/sysusers.d/ntrip-monitord.conf
	-systemd-sysusers
	install -d -m 755 $(DESTDIR)/var/lib/ntrip-monitor

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
CPPFLAGS = -I../src -I../lib/cJSON \
           -I../lib/mbedtls/include -I../lib/mbedtls/library \
           -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# Every shared source, by wildcard rather than by hand.
#
# This list used to be written out file by file, and it fell behind: it
# never gained src/net/ntrip_handler.c, so `make -C service` stopped
# linking (undefined reference to get_gnss_id_from_rtcm) while CMake --
# which keeps its own list -- carried on building the daemon happily.
# Nobody noticed until CI started running this path, because the people
# who use it are packagers and VPS deployments rather than developers.
#
# src/core, src/net and src/session are the shared layers by definition:
# anything platform- or frontend-specific lives in src/cli, gui/ or
# android/, none of which is picked up here.  So a wildcard cannot pull
# in something the daemon should not have, and it cannot fall behind
# again.
SRC = ntrip-monitord.c \
      $(wildcard ../src/session/*.c) \
      $(wildcard ../src/net/*.c) \
      $(wildcard ../src/core/*.c) \
      ../lib/cJSON/cJSON.c \
      $(wildcard ../lib/mbedtls/library/*.c)

BIN = ntrip-monitord

# Windows needs winsock and bcrypt (Mbed TLS's OS entropy source);
# everything else needs only libm.
ifeq ($(OS),Windows_NT)
LDLIBS = -lws2_32 -lbcrypt -lm
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

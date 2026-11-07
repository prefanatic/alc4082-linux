CC ?= cc
CFLAGS ?= -O2
CPPFLAGS += $(shell pkg-config --cflags libusb-1.0)
CFLAGS += -Wall -Wextra -Werror
LDLIBS += $(shell pkg-config --libs libusb-1.0)

.PHONY: all clean

all: alc4082-reg

alc4082-reg: tools/alc4082-reg.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	$(RM) alc4082-reg


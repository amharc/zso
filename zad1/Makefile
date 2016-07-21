CFLAGS=-Wall -Wextra -Werror -pedantic -std=gnu11 -m32 -g
ASFLAGS=--32 --gstabs
LDFLAGS=-Wl,-Ttext=0x204800 -Wl,--section-start=.plt=0x02048200 -Wl,--section-start=.init=0x020481d0 -Wl,--section-start=.rel.plt=0x2048158 --static -m32
SOURCES=$(wildcard *.c)
ASMSOURCES=$(wildcard *.S)
DEPENDS=$(patsubst %.c,.%.depends,$(SOURCES))
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))
ASMOBJECTS=$(patsubst %.S,%.o,$(ASMSOURCES))

all: raise

raise: raise.o $(OBJECTS) $(ASMOBJECTS)

.%.depends: %.c
	$(CC) $(CFLAGS) -MM $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f *.o $(EXECS) $(DEPENDS)

-include $(DEPENDS)

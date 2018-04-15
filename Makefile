TARGET:=sndsample_u
SRCS:=main.c
OBJS:=$(SRCS:.c=.o)
ZED_LIB?= /usr/share/EECE4534/lib
ZED_INCLUDE?=/usr/share/EECE4534/include
INCLUDE_DIRS:=$(ZED_INCLUDE)
CFLAGS:=$(foreach incdir, $(INCLUDE_DIRS), -I$(incdir)) -g -O0
CROSS_COMPILE?=arm-linux-gnueabihf
CROSS_LIBS?=/usr/$(CROSS_COMPILE)/lib
LZED:=-lzed -L$(ZED_LIB)
LALSA:= -lasound -lpthread -lrt -ldl -lm

include zed.mk
.PHONY: clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CROSS_COMPILE)-gcc -o $@ $^ $(LALSA) $(LZED)

%.o: %.c %.h
	$(CROSS_COMPILE)-gcc $(CFLAGS) -c $<

%.o: %.c
	$(CROSS_COMPILE)-gcc $(CFLAGS) -c $<

clean:
	rm -rf $(OBJS) $(TARGET)

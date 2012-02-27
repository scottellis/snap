# Use this makefile for a cross-platform overo build

ifeq ($(strip $(OETMP)),)
	OETMP = $(HOME)/overo-oe/tmp 
endif


TOOLDIR = $(OETMP)/sysroots/`uname -m`-linux/usr/armv7a/bin

STAGEDIR = $(OETMP)/sysroots/armv7a-angstrom-linux-gnueabi/usr

CC = $(TOOLDIR)/arm-angstrom-linux-gnueabi-gcc

CFLAGS = -Wall

LIBDIR = $(STAGEDIR)/lib

INCDIR = $(STAGEDIR)/include

TARGET = snap

$(TARGET): snap.c
	$(CC) $(CFLAGS) -I $(INCDIR) -L $(LIBDIR) snap.c -o $(TARGET)

install:
	sudo cp $(TARGET) /exports/overo/home/root

clean:
	rm -f $(TARGET)




INCLUDES = 					\
	@KDRIVE_INCS@				\
	@KDRIVE_CFLAGS@

AM_CFLAGS = -DHAVE_DIX_CONFIG_H

noinst_LIBRARIES = liblinux.a

liblinux_a_SOURCES =

if KDRIVE_HW
liblinux_a_SOURCES += linux.c klinux.h
endif

if KDRIVE_KBD
liblinux_a_SOURCES += keyboard.c
endif

if KDRIVE_EVDEV
liblinux_a_SOURCES += evdev.c
endif

if KDRIVE_MOUSE
liblinux_a_SOURCES += mouse.c bus.c ms.c ps2.c
endif

if TSLIB
liblinux_a_SOURCES += tslib.c
endif

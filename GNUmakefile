.SUFFIXES: .So
VPATH = .

LIB=	asyncproxy

PREFIX?= /usr/local
LIBDIR= ${PREFIX}/lib
INCLUDEDIR= ${PREFIX}/include

SRCS_C= asyncproxy.c
SRCS_H= asyncproxy.h

CFLAGS?= -O2 -pipe

OBJS = $(SRCS_C:.c=.o)
OBJS_PIC = $(SRCS_C:.c=.So)

all: lib${LIB}.a lib${LIB}.so.0 lib${LIB}.so

lib${LIB}.a: $(OBJS) $(SRCS_H)
	$(AR) cq $@ $(OBJS)
	ranlib $@

lib${LIB}.so.0: $(OBJS_PIC) $(SRCS_H)
	cc  -shared -o $@ -Wl,-soname,$@ $(OBJS_PIC)

lib${LIB}.so: lib${LIB}.so.0
	ln -sf lib${LIB}.so.0 $@

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

.c.So:
	$(CC) -fpic -DPIC -c $(CFLAGS) $< -o $@

clean:
	rm -f lib${LIB}.a lib${LIB}.so.0 $(OBJS) $(OBJS_PIC)

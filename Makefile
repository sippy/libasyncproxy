LIB= asyncproxy
SHLIB_MAJOR= 0
LIBTHREAD?= pthread
PREFIX?= /usr/local
LIBDIR= ${PREFIX}/lib

CFLAGS=	-g -O0

SRCS=		asyncproxy.c asyncproxy.h

LDADD=          -l${LIBTHREAD}

MK_PROFILE?=	no

WARNS?=         4

#CLEANFILES+=    test
#
#test: lib${LIB}.a test.c Makefile
#        cc -O0 -g3 -I. ${LDADD} test.c -o test libsinet.a
#
#includepolice:
#        for file in ${SRCS}; do \
#          python misc/includepolice.py $${file} || sleep 5; \
#        done

.include <bsd.lib.mk>

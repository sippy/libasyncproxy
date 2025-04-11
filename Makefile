LIB= asyncproxy
SHLIB_MAJOR= 0
LIBTHREAD?= pthread
PREFIX?= /usr/local
LIBDIR= ${PREFIX}/lib

CFLAGS=	-g3 -O0

SRCS=		src/asyncproxy.c src/asyncproxy.h src/asp_sock.c \
		src/asp_sock.h src/asp_iostats.h

LDADD=          -l${LIBTHREAD}

MK_PROFILE?=	no

WARNS?=         4

TSTAMP!=	date "+%Y%m%d%H%M%S"

PKGNAME=	lib${LIB}
PKGFILES=	${SRCS} Makefile AsyncProxy.py

distribution: clean
	tar cvfy /tmp/${PKGNAME}-sippy-${TSTAMP}.tbz2 ${PKGFILES}
	scp /tmp/${PKGNAME}-sippy-${TSTAMP}.tbz2 sobomax@download.sippysoft.com:/usr/local/www/data/${PKGNAME}/
	git tag rel.${TSTAMP}
	git push origin rel.${TSTAMP}

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

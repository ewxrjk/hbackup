srcdir=.
PLATFORM:=$(shell uname -s)
include ${srcdir}/makedefs.${PLATFORM}
SHELL=/bin/bash
VPATH=.:${srcdir}
LIBOBJECT=exceptions.${O} utils.${O} hash.${O} file.${O} \
	globals.${O} local.${O}	exclude.${O} backup.${O} restore.${O} \
	verify.${O} cleanup.${O} sftp.${O} version.${O} speedtest.${O} \
	sha1.${O} filesystem.${O} recode.${O} ${PLATOBJS}
VERSION=0.0+

hostname:=$(shell uname -n|sed 's/\..*//')

all: nhbackup${EXE} sha1test${EXE} recodetest${EXE}

# TODO: more sophisticated testing
check: all
	./sha1test
	${srcdir}/hbackup --help > /dev/null
	./nhbackup --help > /dev/null
	srcdir=${srcdir} PATH=`pwd`:`cd ${srcdir} && pwd`:$$PATH tests

nhbackup${EXE}: nhbackup.${O} ${LIBOBJECT} 
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o $@ $^ $(LIBS)

sha1test${EXE}: sha1test.${O} sha1.${O}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $^

recodetest${EXE}: recodetest.${O} ${LIBOBJECT}
	${CXX} ${CXXFLAGS} ${LDFLAGS} -o $@ $^ $(LIBS)

${OBJECT}: nhbackup.h

${srcdir}/version.cc: ${srcdir}/Makefile
	echo '#include "nhbackup.h"' > ${srcdir}/version.cc.tmp
	echo 'const char version[] = "${VERSION}";' >> ${srcdir}/version.cc.tmp
	mv ${srcdir}/version.cc.tmp ${srcdir}/version.cc

install:
	mkdir -m 755 -p ${bindir} ${man1dir}
	install -m 755 ${srcdir}/hbackup ${bindir}/hbackup
	install -m 644 ${srcdir}/hbackup.1 ${man1dir}/hbackup.1
	install -m 755 nhbackup ${bindir}/nhbackup

install-local: install
	install -m 644 ${srcdir}/hbackup.sh ${prefix}/share/hbackup.sh
	install -m 755 ${srcdir}/backup.${hostname} ${bindir}/backup
	if [ -e ${srcdir}/rbackup.${hostname} ]; then \
	  install -m 755 ${srcdir}/rbackup.${hostname} ${bindir}/rbackup;\
	fi
ifeq (${hostname},sfere)
	install -m 755 ${srcdir}/backup.sfere-music ${bindir}/backup-music
endif

clean:
	rm -f *.${O} nhbackup

dist:
	rm -rf hbackup-${VERSION}
	mkdir hbackup-${VERSION}
	mkdir hbackup-${VERSION}/ChangeLog.d
	cp ${srcdir}/Makefile hbackup-${VERSION}/.
	cp ${srcdir}/*.cc hbackup-${VERSION}/.
	cp ${srcdir}/*.h hbackup-${VERSION}/.
	cp ${srcdir}/*.c hbackup-${VERSION}/.
	cp ${srcdir}/README hbackup-${VERSION}/.
	cp ${srcdir}/makedefs.Linux hbackup-${VERSION}/.
	cp ${srcdir}/makedefs.Darwin hbackup-${VERSION}/.
	cp ${srcdir}/makedefs.mingw hbackup-${VERSION}/.
	cp ${srcdir}/hbackup ${srcdir}/hbackup.1 hbackup-${VERSION}/.
	cp ${srcdir}/tests hbackup-${VERSION}/.
	cp ${srcdir}/hbackup.sh ${srcdir}/backup.curator hbackup-${VERSION}/.
	cp ${srcdir}/backup.lyonesse ${srcdir}/backup.sfere ${srcdir}/backup.kakajou ${srcdir}/backup.chymax hbackup-${VERSION}/.
	cp ${srcdir}/rbackup.lyonesse ${srcdir}/rbackup.sfere ${srcdir}/rbackup.kakajou ${srcdir}/rbackup.chymax hbackup-${VERSION}/.
	cp ${srcdir}/ChangeLog.d/*[^~] hbackup-${VERSION}/ChangeLog.d/.
	bzr log > hbackup-${VERSION}/ChangeLog.d/hbackup-bzr
	tar cfj hbackup-${VERSION}.tar.bz2 hbackup-${VERSION}
	rm -rf hbackup-${VERSION}

distcheck: dist
	rm -rf ,distcheck
	mkdir ,distcheck
	cd ,distcheck && tar xfj ../hbackup-${VERSION}.tar.bz2
	$(MAKE) -C ,distcheck/hbackup-${VERSION} srcdir=. check


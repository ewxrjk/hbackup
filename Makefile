srcdir=.
PLATFORM:=$(shell uname -s)
include ${srcdir}/makedefs.${PLATFORM}
SHELL=/bin/bash
VPATH=.:${srcdir}
LIBOBJECT=exceptions.${O} utils.${O} hash.${O} file.${O} \
	globals.${O} local.${O}	exclude.${O} backup.${O} restore.${O} \
	verify.${O} cleanup.${O} sftp.${O} version.${O} speedtest.${O} \
	sha1.${O} filesystem.${O} recode.${O} ${PLATOBJS}
ALLOBJECT=${LIBOBJECT} nhbackup.${O} sha1test.${O}
VERSION=0.1+

hostname:=$(shell uname -n|sed 's/\..*//')
DISTNAME=hbackup-anjou
DISTDIR=${DISTNAME}-${VERSION}

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

${ALLOBJECT}: nhbackup.h sha1.h

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
	rm -rf ${DISTDIR}
	mkdir ${DISTDIR}
	mkdir ${DISTDIR}/ChangeLog.d
	cp ${srcdir}/Makefile ${DISTDIR}/.
	cp ${srcdir}/*.cc ${DISTDIR}/.
	cp ${srcdir}/*.h ${DISTDIR}/.
	cp ${srcdir}/*.c ${DISTDIR}/.
	cp ${srcdir}/README ${DISTDIR}/.
	cp ${srcdir}/makedefs.Linux ${DISTDIR}/.
	cp ${srcdir}/makedefs.Darwin ${DISTDIR}/.
	cp ${srcdir}/makedefs.mingw ${DISTDIR}/.
	cp ${srcdir}/hbackup ${srcdir}/hbackup.1 ${DISTDIR}/.
	cp ${srcdir}/tests ${DISTDIR}/.
	cp ${srcdir}/hbackup.sh ${srcdir}/backup.curator ${DISTDIR}/.
	cp ${srcdir}/backup.lyonesse ${srcdir}/backup.sfere ${srcdir}/backup.kakajou ${srcdir}/backup.chymax ${DISTDIR}/.
	cp ${srcdir}/rbackup.lyonesse ${srcdir}/rbackup.sfere ${srcdir}/rbackup.kakajou ${srcdir}/rbackup.chymax ${DISTDIR}/.
	cp ${srcdir}/rbackup.iset ${DISTDIR}/.
	cp ${srcdir}/openssh.patch ${DISTDIR}/.
	cp ${srcdir}/ChangeLog.d/*[^~] ${DISTDIR}/ChangeLog.d/.
	bzr log > ${DISTDIR}/ChangeLog.d/${DISTNAME}-bzr
	tar cfj ${DISTDIR}.tar.bz2 ${DISTDIR}
	rm -rf ${DISTDIR}

distcheck: dist
	rm -rf ,distcheck
	mkdir ,distcheck
	cd ,distcheck && tar xfj ../${DISTDIR}.tar.bz2
	$(MAKE) -C ,distcheck/${DISTDIR} srcdir=. check


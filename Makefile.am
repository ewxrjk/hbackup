bin_PROGRAMS=nhbackup
dist_bin_SCRIPTS=hbackup
dist_man_MANS=hbackup.1
EXTRA_DIST=CHANGES prepare openssh.patch changelog \
	debian/changelog debian/control debian/copyright debian/rules \
	scripts/dist scripts/setversion

noinst_PROGRAMS=sha1test recodetest
noinst_LIBRARIES=libhbackup.a
dist_noinst_SCRIPTS=tests
libhbackup_a_SOURCES=exceptions.cc utils.cc hash.cc file.cc globals.cc	\
	local.cc exclude.cc backup.cc restore.cc verify.cc cleanup.cc	\
	sftp.cc version.cc speedtest.cc sha1.c filesystem.cc		\
	recode.cc nhbackup.h sha1.h

nhbackup_SOURCES=nhbackup.cc
nhbackup_LDADD=libhbackup.a $(LIBPCRE) $(LIBICONV)

sha1test_SOURCES=sha1test.c
sha1test_LDADD=libhbackup.a

recodetest_SOURCES=recodetest.cc
recodetest_LDADD=libhbackup.a $(LIBICONV)

${srcdir}/version.cc: ${srcdir}/Makefile
	echo '#include "nhbackup.h"' > ${srcdir}/version.cc.tmp
	echo 'const char version[] = "${VERSION}";' >> ${srcdir}/version.cc.tmp
	mv ${srcdir}/version.cc.tmp ${srcdir}/version.cc

# TODO: more sophisticated testing
check: all
	./sha1test
	${srcdir}/hbackup --help > /dev/null
	./nhbackup --help > /dev/null
	srcdir=${srcdir} PATH=`pwd`:`cd ${srcdir} && pwd`:$$PATH bash tests

clean-local:
	rm -rf ,test

dist: changelog

${srcdir}/changelog:
	cd ${srcdir} && \
	if bzr log > changelog.new; then mv changelog.new changelog;\
	else rm -f changelog.new; fi

.PHONY: changelog

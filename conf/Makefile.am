dist_sysconf_DATA = ltfs.conf

PLAT_OPT =

if PLATFORM_LINUX
PLAT_OPT += plugin tape sg __LIBDIR__/ltfs/libtape-sg-ibmtape.so
endif

if PLATFORM_MAC
PLAT_OPT += plugin tape iokit __LIBDIR__/ltfs/libtape-iokit-ibmtape.so
endif

if PLATFORM_FREEBSD
PLAT_OPT += plugin tape camtape __LIBDIR__/ltfs/libtape-camtape-freebsd.so
endif

if ENABLE_LIN_TAPE
PLAT_OPT += \\nplugin tape lin_tape __LIBDIR__/ltfs/libtape-lin_tape.so
endif

ltfs.conf: ltfs.conf.in
	${SED} -e "s!__PLATFORM_DRIVERS__!${PLAT_OPT}!" ltfs.conf.in >ltfs.conf.in1
	${SED} -e "s!__LIBDIR__!@libdir@!" ltfs.conf.in1 >ltfs.conf
	rm ltfs.conf.in1
	mv ltfs.conf ltfs.conf.tmp
	${SED} -e "s!__DEFAULT_TAPE__!$(DEFAULT_TAPE)!" ltfs.conf.tmp >ltfs.conf
	mv ltfs.conf ltfs.conf.tmp
	${SED} -e "s!__DEFAULT_IOSCHED__!$(DEFAULT_IOSCHED)!" ltfs.conf.tmp >ltfs.conf
	mv ltfs.conf ltfs.conf.tmp
	${SED} -e "s!__DEFAULT_KMI__!$(DEFAULT_KMI)!" ltfs.conf.tmp >ltfs.conf
	mv ltfs.conf ltfs.conf.tmp
	${SED} -e "s!__CONFDIR__!@sysconfdir@!" ltfs.conf.tmp >ltfs.conf
	rm ltfs.conf.tmp

install-data-local:
	if [ ! -f "$(DESTDIR)$(sysconfdir)/ltfs.conf.local" ]; then \
		cp ltfs.conf.local "$(DESTDIR)$(sysconfdir)/ltfs.conf.local"; \
	fi

clean-local:
	rm -f ltfs.conf

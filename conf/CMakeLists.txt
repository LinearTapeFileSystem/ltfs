# TODO: Change how this is done

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

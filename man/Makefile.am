man_MANS = mkltfs.8 ltfsck.8 ltfs-sde.8

man-clean:
	rm $(man_MANS)

man-rebuild:
	docbook2man sgml/mkltfs.sgml
	docbook2man sgml/ltfsck.sgml
	docbook2man sgml/ltfs-sde.sgml
	rm -f manpage.*

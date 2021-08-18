man_MANS = mkltfs.8 ltfsck.8 ltfs-sde.8 ltfs_ordered_copy.1 ltfsindextool.8

man-clean:
	rm $(man_MANS)

man-rebuild:
	docbook2man sgml/mkltfs.sgml
	docbook2man sgml/ltfsck.sgml
	docbook2man sgml/ltfs-sde.sgml
	docbook2man sgml/ltfs_ordered_copy.sgml
	docbook2man sgml/ltfsindextool.sgml
	rm -f manpage.*

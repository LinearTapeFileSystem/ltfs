set(MANPAGES
	mkltfs
	ltfsck
	ltfs
	ltfs_ordered_copy
	ltfsindextool
)

add_custom_target(man)
foreach(NAME IN LISTS MANPAGES)
	file(REAL_PATH "sgml/${NAME}.sgml" SOURCE)
	add_custom_target("man_${NAME}"
		docbook2man ${SOURCE} # 2> /dev/null
		DEPENDS ${SOURCE}
		)
	add_dependencies(man "man_${NAME}")
endforeach()

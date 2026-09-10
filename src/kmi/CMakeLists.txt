ltfs_add_plugin(libkmi-simple
    SOURCES simple.c key_format_ltfs.c
    DEFINES KMI_SIMPLE
    MSG kmi_simple)

ltfs_add_plugin(libkmi-flatfile
    SOURCES flatfile.c key_format_ltfs.c
    MSG kmi_flatfile)

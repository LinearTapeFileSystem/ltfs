ltfs_add_plugin(libiosched-fcfs
    SOURCES fcfs.c
    MSG iosched_fcfs)

ltfs_add_plugin(libiosched-unified
    SOURCES unified.c cache_manager.c
    MSG iosched_unified)

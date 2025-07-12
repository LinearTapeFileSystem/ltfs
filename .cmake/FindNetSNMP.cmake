#####
# Find the Net SNMP library (net-snmp)
#
option(ENABLE_SNMP "Enable SNMP support" YES)
set(NetSNMP_FOUND OFF CACHE BOOL "net-snmp is found")

if(ENABLE_SNMP OR NetSNMP_FIND_REQUIRED)
	find_program(NetSNMP_CONFIG_BIN net-snmp-config REQUIRED)
else()
	find_program(NetSNMP_CONFIG_BIN net-snmp-config)
endif()


if(NetSNMP_CONFIG_BIN)
	execute_process(COMMAND ${NetSNMP_CONFIG_BIN} --cflags
		OUTPUT_VARIABLE NetSNMP_CFLAGS
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	execute_process(COMMAND ${NetSNMP_CONFIG_BIN} --libs
		OUTPUT_VARIABLE NetSNMP_LIBS
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	execute_process(COMMAND ${NetSNMP_CONFIG_BIN} --agent-libs
		OUTPUT_VARIABLE NetSNMP_AGENT_LIBS
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	# NetSNMP import library
	add_library(NetSNMP INTERFACE IMPORTED)
	separate_arguments(NetSNMP_CFLAGS)
	separate_arguments(NetSNMP_AGENT_LIBS)
	separate_arguments(NetSNMP_LIBS)

	set_target_properties(NetSNMP PROPERTIES
		INTERFACE_COMPILE_OPTIONS "${NetSNMP_CFLAGS};${NetSNMP_LIBS};${NetSNMP_AGENT_LIBS}"
	)
	set(NetSNMP_FOUND ON CACHE BOOL "net-snmp is found")
endif()

cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(ads7830
	VERSION 0.1
    DESCRIPTION "Server to interface system variables to ADS7830 ADC channels"
)

add_executable( ${PROJECT_NAME}
	src/ads7830.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
)

target_link_libraries( ${PROJECT_NAME}
	${CMAKE_THREAD_LIBS_INIT}
	rt
	varserver
    tjson
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

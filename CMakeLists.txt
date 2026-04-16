cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
	set(_default_idf_path "$ENV{HOME}/.espressif/v6.0/esp-idf")
	if(EXISTS "${_default_idf_path}/tools/cmake/project.cmake")
		set(ENV{IDF_PATH} "${_default_idf_path}")
	else()
		message(FATAL_ERROR "IDF_PATH is not set and default ESP-IDF path was not found at ${_default_idf_path}")
	endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32os VERSION 1.0.0)

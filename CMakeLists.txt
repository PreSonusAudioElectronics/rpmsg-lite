#
# This file added by Presonus after forking
#

cmake_minimum_required(VERSION 3.10)

project(rpmsg-lite)

set(RPMSG_LIB_ROOT ${CMAKE_CURRENT_LIST_DIR} )

include( ${CMAKE_CURRENT_LIST_DIR}/cmake/common.cmake )

if("${TARGET_ENVIRONMENT}" STREQUAL "zephyr")
	# Use case: Zephyr project 
	include(${CMAKE_CURRENT_LIST_DIR}/cmake/zephyr.cmake)
elseif("${TARGET_ENVIRONMENT}" STREQUAL "gtest")
	include(${CMAKE_CURRENT_LIST_DIR}/cmake/gtest.cmake)
endif()


set (CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

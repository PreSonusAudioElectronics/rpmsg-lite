cmake_minimum_required(VERSION 3.14)

project(rpmsg-lite-test)

set(TARGET_NAME rpmsg-lite-test)
add_executable(${TARGET_NAME})
enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG e4717df71a4f45bf9f0ac88c6cd9846a0bc248dd
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

set(RPMSG_LIB_ROOT ${CMAKE_CURRENT_LIST_DIR}/../ )

include( ${RPMSG_LIB_ROOT}/cmake/common.cmake )
include( ${RPMSG_LIB_ROOT}/cmake/gtest.cmake )

set (CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

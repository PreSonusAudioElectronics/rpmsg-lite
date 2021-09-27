
cmake_minimum_required(VERSION 3.9)
set(CMAKE_CXX_STANDARD 17)

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

include (GoogleTest)

set( RPMSG_TEST_SOURCES
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/porting/platform/simulation_linux/rpmsg_platform.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/porting/environment/linux_sim/rpmsg_env_linux_sim.cpp
)

set( RPMSG_TEST_INCLUDES
	${RPMSG_LIB_ROOT}/lib/include/platform/simulation
)

add_subdirectory( ${RPMSG_LIB_ROOT}/test )

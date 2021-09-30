cmake_minimum_required(VERSION 3.14)

set( TARGET_NAME test_rpmsg_lite )
add_executable(
	${TARGET_NAME}
	test_rpmsg_lite.cpp
	${RPMSG_COMMON_SRCS}
	${RPMSG_TEST_SOURCES}
)

target_include_directories(${TARGET_NAME} PUBLIC
	${RPMSG_COMMON_INCS}
	${RPMSG_TEST_INCLUDES}
)

target_link_libraries(${TARGET_NAME}
	gtest_main
)

include(GoogleTest)
gtest_discover_tests(${TARGET_NAME})

target_compile_definitions( ${TARGET_NAME} PUBLIC RL_USE_ENVIRONMENT_CONTEXT=1 )

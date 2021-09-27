cmake_minimum_required(VERSION 3.14)

add_executable(
	test_rpmsg_lite
	test_rpmsg_lite.cpp
	${RPMSG_COMMON_SRCS}
	${RPMSG_TEST_SOURCES}
)

target_include_directories(test_rpmsg_lite PUBLIC
	${RPMSG_COMMON_INCS}
	${RPMSG_TEST_INCLUDES}
)

target_link_libraries(test_rpmsg_lite
	gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_rpmsg_lite)

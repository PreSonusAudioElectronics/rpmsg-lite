


# rpmsg-lite target-specific sources
if("${TARGET_DEVICE}" STREQUAL "imx8mn-cm7")
	target_sources(pep PRIVATE
		${RPMSG_LIB_ROOT}/lib/rpmsg_lite/porting/platform/imx8mn_m7/rpmsg_platform.c
	)

	target_include_directories(pep PUBLIC
		${RPMSG_LIB_ROOT}/lib/include/platform/imx8mn_m7
	)
endif()

# rpmsg-lite environment-specifics
if("${TARGET_ENVIRONMENT}" STREQUAL "zephyr")
	target_sources(pep PRIVATE
		${RPMSG_LIB_ROOT}/lib/rpmsg_lite/porting/environment/zephyr/rpmsg_env_zephyr.c
	)
endif()

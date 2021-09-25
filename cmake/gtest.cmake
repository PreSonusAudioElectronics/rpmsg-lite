
target_sources(${TARGET_NAME} PRIVATE
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/porting/platform/simulation_linux/rpmsg_platform.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/porting/environment/linux_sim/rpmsg_env_linux_sim.c
)

target_include_directories( ${TARGET_NAME} PUBLIC
	${RPMSG_LIB_ROOT}/lib/include/platform/simulation
)

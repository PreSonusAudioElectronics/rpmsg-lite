#
# This file added by Presonus after forking
#

# rpmsg-lite common source files
target_sources(pep PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/lib/common/llist.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/rpmsg_lite/rpmsg_lite.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/rpmsg_lite/rpmsg_ns.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/rpmsg_lite/rpmsg_queue.c
)

# rpmsg-lite common include paths
target_include_directories(pep PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/lib/include
)

# rpmsg-lite target-specific sources
if("${TARGET_DEVICE}" STREQUAL "imx8mn-cm7")
    target_sources(pep PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lib/rpmsg_lite/porting/platform/imx8mn_m7/rpmsg_platform.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/rpmsg_lite/porting/platform/imx8mn_m7/rsc_table.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/virtio/virtqueue.c
    )

    target_include_directories(pep PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/lib/include/platform/imx8mn_m7
    )
endif()

# rpmsg-lite environment-specifics
if("${TARGET_ENVIRONMENT}" STREQUAL "zephyr")
    target_sources(pep PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lib/rpmsg_lite/porting/environment/zephyr/rpmsg_env_zephyr.c
    )
endif()



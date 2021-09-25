
# rpmsg-lite common source files
message("TARGET_NAME = ${TARGET_NAME}")
target_sources(${TARGET_NAME} PRIVATE
	${RPMSG_LIB_ROOT}/lib/common/llist.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/rpmsg_lite.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/rpmsg_ns.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/rpmsg_queue.c
    ${RPMSG_LIB_ROOT}/lib/virtio/virtqueue.c
    ${RPMSG_LIB_ROOT}/lib/virtio/rsc_table.c
)

# rpmsg-lite common include paths
target_include_directories(${TARGET_NAME} PUBLIC
	${RPMSG_LIB_ROOT}/lib/include
)
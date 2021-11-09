
# rpmsg-lite common source files
message("rpmsg-lite TARGET_NAME = ${TARGET_NAME}")
set( RPMSG_COMMON_SRCS
	${RPMSG_LIB_ROOT}/lib/common/llist.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/rpmsg_lite.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/rpmsg_ns.c
	${RPMSG_LIB_ROOT}/lib/rpmsg_lite/rpmsg_queue.c
    ${RPMSG_LIB_ROOT}/lib/virtio/virtqueue.c
    ${RPMSG_LIB_ROOT}/lib/virtio/rsc_table.c
)

# rpmsg-lite common include paths
set( RPMSG_COMMON_INCS
	${RPMSG_LIB_ROOT}/lib/include
)
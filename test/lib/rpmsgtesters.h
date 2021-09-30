
#ifndef _rpmsgtesters_h
#define _rpmsgtesters_h

#include <mutex>
#include <thread>

#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "rpmsg_ns.h"
#include "rsc_table.h"
#include "rpmsg_env_linux.h"


class RpmsgRemoteTester
{
public:
    static constexpr unsigned kMaxAnnounceStringLen = 256;
    static constexpr unsigned kMsgBufSize = 1024;

    RpmsgRemoteTester(void* shmemAddr, uint32_t linkId, uint32_t endpointAddress,
        const char *eptAnnounceString, const char *expectedHelloString,
        bool& remoteReady,
        std::mutex& rpmsgMutex);
    ~RpmsgRemoteTester();

    int start();
    int stop();

private:
    // managed by this class
    rpmsg_lite_instance* instance = nullptr;
    rpmsg_queue_handle rpQueue;
    rpmsg_lite_endpoint *rpEpt;

    // not managed here
    void* shmemAddr;

    // class scope
    uint32_t linkId;
    rpmsg_env_init_t remoteEnv;
    std::thread rxThread;
    std::mutex& rpmsgMutex;
    uint32_t eptAddress;
    uint32_t srcAddress;
    const char * eptAnnounceString = nullptr;
    const char * eptHelloString = nullptr;
    int eptAnnounceStringLen = 0;
    char msgBuf[kMsgBufSize];
    bool& remoteReady;

    // private methods
    void threadFunc();

};

#endif // _rpmsgtesters_h

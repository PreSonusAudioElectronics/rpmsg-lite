
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "rpmsgtesters.h"

RpmsgRemoteTester::RpmsgRemoteTester(void* shmemAddr, uint32_t linkId, 
    uint32_t endpointAddress, const char *eptAnnounceString, const char *expectedHelloString, 
    bool& remoteReady,
    std::mutex& rpmsgMutex):
    shmemAddr(shmemAddr), linkId(linkId), eptAddress(endpointAddress), 
    eptAnnounceString(eptAnnounceString), eptHelloString(expectedHelloString),
    remoteReady(remoteReady),
    rpmsgMutex(rpmsgMutex)
{
    EXPECT_NE( shmemAddr, nullptr );
    this->remoteEnv = { shmemAddr, nullptr };
    rpmsgMutex.lock();
    this->instance = rpmsg_lite_remote_init(shmemAddr, linkId, RL_NO_FLAGS, &remoteEnv);
    rpmsgMutex.unlock();
    EXPECT_NE( this->instance, nullptr );
    EXPECT_NE( this->eptAnnounceString, nullptr );
    this->eptAnnounceStringLen = std::string(this->eptAnnounceString).size();
    EXPECT_LE( this->eptAnnounceStringLen, kMaxAnnounceStringLen );
}

RpmsgRemoteTester::~RpmsgRemoteTester()
{
    rpmsgMutex.lock();
    EXPECT_EQ( rpmsg_lite_destroy_ept( this->instance, this->rpEpt ), RL_SUCCESS );
    EXPECT_EQ( rpmsg_queue_destroy(this->instance, this->rpQueue), RL_SUCCESS );
    EXPECT_EQ( rpmsg_lite_deinit(this->instance), RL_SUCCESS );
    rpmsgMutex.unlock();

}

int RpmsgRemoteTester::start()
{
    if( this->rxThread.joinable() )
    {
        // thread is already started
        return -1;
    }

    // ooh, lambda, tricky!
    rxThread = std::thread( [this] { this->threadFunc(); } );
    return 0;
}

int RpmsgRemoteTester::stop()
{
    if( !( this->rxThread.joinable() ) )
    {
        // thread is not running
        return -1;
    }
    this->rxThread.join();
    return 0;
}

void RpmsgRemoteTester::threadFunc()
{
    // Wait for link to come up (i.e. wait for other side to kick us)
    int rpStat = 0;
    while( 0 == rpStat )
    {
        rpmsgMutex.lock();
        rpStat = rpmsg_lite_is_link_up( this->instance );
        rpmsgMutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    printf("remote link with id %d is up\n", this->linkId);

    // create and bind queue and endpoint
    this->rpmsgMutex.lock();
    this->rpQueue = rpmsg_queue_create(this->instance);
    EXPECT_NE( this->rpQueue, nullptr );
    this->rpEpt = rpmsg_lite_create_ept(this->instance, this->eptAddress,
        rpmsg_queue_rx_cb, this->rpQueue );
    EXPECT_NE( this->rpEpt, nullptr );

    // announce to other side
    rpmsg_ns_announce( this->instance, this->rpEpt, this->eptAnnounceString, RL_NS_CREATE );
    this->rpmsgMutex.unlock();

    // signal to any watchers that we are ready for action
    this->remoteReady = true;

    printf("remote on link id %d announce string sent\n", this->linkId );
    printf("remote on link id %d now listening on endpoint %d\n", this->linkId, this->eptAddress);

    uint32_t len;
    while ( true )
    {
        auto retVal = rpmsg_queue_recv(this->instance, this->rpQueue, &this->srcAddress, 
            this->msgBuf, kMsgBufSize, &len, RL_DONT_BLOCK);
        if ( retVal == RL_SUCCESS )
        {
            printf("remote received a buffer from source endpoint %d\n", this->srcAddress);
            printf("remote received: %s\n", this->msgBuf);
            if( 0 == std::strncmp( this->eptHelloString, this->msgBuf, len) )
            {
                printf("remote on link id %d received correct hello from master\n", this->linkId);
                break;
            }
        }

        std::this_thread::sleep_for( std::chrono::milliseconds(25) );
    }

    __asm("nop");
}
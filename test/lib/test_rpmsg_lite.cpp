
#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "rpmsg_ns.h"
#include "rsc_table.h"
#include "rpmsg_env_linux.h"
#include "threadsafequeue.h"
#include "rpmsgtestdispatchers.h"

#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdio>
#include <chrono>
#include <cstring>


static void *memBase = nullptr;

/**
 * @brief Since we are simulating multiple different instances running on 
 * different processors by simply spawning a thread for each instance, this 
 * mutex provides a global lock so the threads don't stomp on each other
 */
std::mutex rpmsgMutex;


static constexpr unsigned kQueueMsgSize = 128;
static constexpr char const * kMsgLinkIsUp = "linkIsUp";
static constexpr unsigned kMsgSizeLinkIsUp = std::strlen(kMsgLinkIsUp);
static constexpr char const * kMasterToRemoteHello = "Hello remote!";
static constexpr unsigned kMsgSizeMasterToRemoteHello = std::strlen(kMasterToRemoteHello);

static constexpr unsigned kEpt1Address = 30;
static constexpr char const * kEpt1AnnounceString = "rpmsg-remote-channel-1";

// Queue for rpmsg-lite remote thread to signal main thread
ThreadsafeQueue remote2Main(4, kQueueMsgSize);

// Queue for rpmsg-lite master thread to signal main (test) thread
ThreadsafeQueue master2Main(4, kQueueMsgSize);

// Forward declarations
void remoteThreadFunc(void *sharedMemBase);
void masterThreadFunc(void *sharedMemBase, uint32_t shMemSize, bool* remoteReady);

TEST(TestRpmsgLite, CanInstantiateRemote)
{
	// Allocate a continuous block of memory for both vrings and the offset table
	static const uint32_t kMemToAllocate = ( RESOURCE_TABLE_OFFSET + sizeof(remote_resource_table) + 16);
	auto vringMem = new char[kMemToAllocate];

	rpmsg_env_init_t remoteEnv = { vringMem, nullptr };
	rpmsg_lite_instance * inst = rpmsg_lite_remote_init( vringMem, 0, 0, &remoteEnv);

	ASSERT_NE(inst, nullptr);

	auto status = rpmsg_lite_deinit(inst);
	ASSERT_GE( status, 0);

	delete(vringMem);
}

TEST(TestRpmsgLite, CanInstantiateMaster)
{
	static const uint32_t kMemToAllocate = ( RESOURCE_TABLE_OFFSET + sizeof(remote_resource_table) + 16);
	auto vringMem = new char[kMemToAllocate];

	memBase = (void*)vringMem;

	rpmsg_env_init_t masterEnv = { vringMem, nullptr };
	rpmsg_lite_instance *inst = rpmsg_lite_master_init(vringMem, kMemToAllocate, 0, 0, &masterEnv );

	ASSERT_NE( inst, nullptr );

	auto status = rpmsg_lite_deinit(inst);
	ASSERT_GE( status, 0);

	delete(vringMem);
}

TEST(TestRpmsgLite, CanTalkBothWays)
{
	static const uint32_t kMemToAllocate = ( RESOURCE_TABLE_OFFSET + sizeof(remote_resource_table) + 16);
	auto sharedMemBase = new char[kMemToAllocate];

	bool remote0Ready = false;

	auto remoteTester0 = std::make_unique<RpmsgRemoteTester>(
		sharedMemBase, 0, 30, kEpt1AnnounceString, kMasterToRemoteHello,
		remote0Ready, rpmsgMutex );
	
	ASSERT_EQ( remoteTester0.get()->start(), 0 );

	// give remote some time to setup
	std::this_thread::sleep_for(std::chrono::milliseconds(50));


	// once remote is up and waiting, start the master thread
	auto masterThread = std::thread( masterThreadFunc, (void*)sharedMemBase, kMemToAllocate,
		&remote0Ready );


	// Send from remote to master and verify the received message

	// Send from master to remote and verify the received message

	remoteTester0.get()->stop();
	masterThread.join();

}



void masterThreadFunc(void *sharedMemBase, uint32_t shMemSize, bool* remoteReady)
{
	printf("Starting master thread...\n");

	rpmsg_env_init_t masterEnv = { sharedMemBase, nullptr };
	rpmsgMutex.lock();
	static auto masterInstance = rpmsg_lite_master_init( sharedMemBase, shMemSize, 0, 
		RL_NO_FLAGS, &masterEnv );
	rpmsgMutex.unlock();
	ASSERT_NE( masterInstance, nullptr );

	rpmsgMutex.lock();
	auto masterQueue1 = rpmsg_queue_create(masterInstance);
	rpmsgMutex.unlock();
	ASSERT_NE( masterQueue1, nullptr );

	rpmsgMutex.lock();
	auto ept1 = rpmsg_lite_create_ept( masterInstance, kEpt1Address, rpmsg_queue_rx_cb, masterQueue1 );
	rpmsgMutex.unlock();
	ASSERT_NE( ept1, nullptr );

	// wait until remote is good to go
	while( !(*remoteReady) )
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	printf("master sending '%s' to remote...\n", kMasterToRemoteHello);

	rpmsgMutex.lock();
	int status = rpmsg_lite_send(masterInstance, ept1, kEpt1Address, (char*)kMasterToRemoteHello, kMsgSizeMasterToRemoteHello, RL_BLOCK );
	rpmsgMutex.unlock();

	printf("master thread terminating...\n");
}



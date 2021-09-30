
#include "rpmsg_lite.h"
#include "rpmsg_queue.h"
#include "rpmsg_ns.h"
#include "rsc_table.h"
#include "rpmsg_env_linux.h"
#include "threadsafequeue.h"

#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdio>
#include <chrono>
#include <cstring>


void *lastAllocatedAddress = 0;
void *memBase = nullptr;

std::mutex remoteReadyMutex;
std::mutex rpmsgMutex;
std::condition_variable remoteReadyCondition;
bool remoteReady = false;

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
void masterThreadFunc(void *sharedMemBase, uint32_t shMemSize);

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

	// start the remote instance thread
	auto remoteThread = std::thread( remoteThreadFunc, (void*)sharedMemBase );

	// wait for remote to be ready
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	std::unique_lock<std::mutex> lk(remoteReadyMutex);
	lk.unlock();

	printf("we made it this far\n");

	// once remote is up and waiting, start the master thread
	auto masterThread = std::thread( masterThreadFunc, (void*)sharedMemBase, kMemToAllocate );

	// wait for notification that the remote thread has been kicked by the master
	int status = -1;
	static char buffer [kQueueMsgSize];
	// polling, should be ASIO, but performance not critical here
	while( status < 0 )
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		status = remote2Main.get(buffer);
		if( status >= 0 )
		{
			if( std::strncmp(buffer, kMsgLinkIsUp,  kMsgSizeLinkIsUp) != 0 )
			{
				status = -1;
			}
		}
	}

	// Send from remote to master and verify the received message

	// Send from master to remote and verify the received message


	remoteThread.join();
	masterThread.join();

}


void remoteThreadFunc(void *sharedMemBase)
{
	std::unique_lock<std::mutex> lk(remoteReadyMutex);

	rpmsg_env_init_t remoteEnv = { sharedMemBase, nullptr };

	rpmsgMutex.lock();
	static auto remoteInstance = rpmsg_lite_remote_init( sharedMemBase, 0, 0, &remoteEnv);
	rpmsgMutex.unlock();

	ASSERT_NE( remoteInstance, nullptr );

	remoteReady = true;
	lk.unlock();

	int rpStat = 0;
	while( 0 == rpStat )
	{
		rpmsgMutex.lock();
		rpStat = rpmsg_lite_is_link_up( remoteInstance );
		rpmsgMutex.unlock();
		env_sleep_msec(10);
	}

	while( remote2Main.put( (void*)(kMsgLinkIsUp) ) != 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	printf("remote link is up\n");

	rpmsgMutex.lock();
	auto rpmsgQueue1 = rpmsg_queue_create(remoteInstance);
	rpmsgMutex.unlock();
	ASSERT_NE( rpmsgQueue1, nullptr );

	rpmsgMutex.lock();
	auto ept1 = rpmsg_lite_create_ept(remoteInstance, kEpt1Address, rpmsg_queue_rx_cb, rpmsgQueue1);
	rpmsgMutex.unlock();
	ASSERT_NE( ept1, nullptr );

	rpmsgMutex.lock();
	rpmsg_ns_announce(remoteInstance, ept1, kEpt1AnnounceString, RL_NS_CREATE);
	rpmsgMutex.unlock();

	printf("remote announce string sent\n");
}

void masterThreadFunc(void *sharedMemBase, uint32_t shMemSize)
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

	// give the remote a chance to set up its endpoint before sending
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	rpmsgMutex.lock();
	int status = rpmsg_lite_send(masterInstance, ept1, kEpt1Address, (char*)kMasterToRemoteHello, kMsgSizeMasterToRemoteHello, RL_BLOCK );
	rpmsgMutex.unlock();

	printf("um...\n");

}



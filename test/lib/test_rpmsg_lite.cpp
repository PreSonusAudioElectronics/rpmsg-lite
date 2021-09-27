
#include "rpmsg_lite.h"
#include "rsc_table.h"

#include <gtest/gtest.h>

#include <memory>

void *lastAllocatedAddress = 0;

TEST(TestRpmsgLite, CanInstantiateRemote)
{
    // Allocate a continuous block of memory for both vrings and the offset table
    static const uint32_t kMemToAllocate = ( RESOURCE_TABLE_OFFSET + sizeof(remote_resource_table) + 16);

    auto vringMem = new char[kMemToAllocate];

    lastAllocatedAddress = vringMem + kMemToAllocate;

    rpmsg_lite_instance * inst = rpmsg_lite_remote_init( vringMem, 0, 0);

    ASSERT_NE(inst, nullptr);

    delete(vringMem);
}

TEST(TestRpmsgLite, CanInstantiateMaster)
{
    
}

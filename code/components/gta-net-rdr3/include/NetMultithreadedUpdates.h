#pragma once

#include <cstdint>
#include <netSyncTree.h>
#include <rlNetBuffer.h>
#include <RageScheduler.h>

struct SyncWorkItem
{
	rage::netObject* object;
	uint32_t ts;
	uint32_t lastChangeTime;
	uint32_t dataLen;
	uint16_t uniqifier;
	uint8_t syncType; // Originally an int32
	bool shouldTrySend;
	uint8_t storage[2400 /*kSyncPacketMaxLength*/];
};

namespace rage
{
	constexpr int kMaxEntityUpdates = 32 /*Max CNetObjPlayers*/ 
	+ 220 /*CNetObjPedBase*/ 
	+ 40 /*CNetObjDraftVehicle*/
	+ 40 /*CNetObjVehicle*/
	+ 60 /*CNetObjProjectile*/ 
	+ 40 /*CNetObjDoor*/
	+ 160 /*CNetObjObject*/
	+ 50 /*CNetObjPropset*/;

	namespace schedulers
	{
		bool DependencyThreadUpdate(rage::sysDependencyBatch* batch);
		bool SyncWorkSerialise(rage::sysDependencyBatch* batch);
	}

	void SyncTreeBeginWrite(rage::netSyncTree::LockMutex& mutex, rage::netSyncTree* tree, void* syncData);
	void SyncTreeEndWrite(rage::netSyncTree::LockMutex& mutex, void* syncData);

	void ScheduleEntityBatch(std::vector<rage::netObject*> objects, rage::sysDependencyBatch::Callback* callback);
	void ScheduleSyncBatch(SyncWorkItem* items, int size, rage::sysDependencyBatch::Callback* callback);
}

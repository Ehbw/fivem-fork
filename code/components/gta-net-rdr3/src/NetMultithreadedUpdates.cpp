#include "StdInc.h"
#include <NetMultithreadedUpdates.h>

#include <Hooking.h>
#include <Hooking.Patterns.h>

#include <rlNetBuffer.h>
#include <netSyncTree.h>
#include <netObject.h>
#include <sysAllocator.h>

#include <thread>
#include <algorithm>

bool* g_useLocks;

static hook::cdecl_stub<void*(rage::netSyncTree::LockMutex*, rage::netSyncTree*)> _netSyncTree_LockTreeMutex([]()
{
	return hook::get_pattern("40 53 48 83 EC 20 48 89 11 48 8B D9 48 8B 0D ? ? ? ? 48 8B 01 FF 90 80 00 00 00");
});

static hook::cdecl_stub<void(rage::netSyncTree*)> _netSyncTree_EndMutexLock([]()
{
	return hook::get_pattern("B9 ? ? ? ? 48 8B 04 C2 48 8B 14 01 48 39 93", -0x18);
});

static hook::cdecl_stub<void(void*)> _netSyncDataBase_StartMutexLock([]()
{
	return hook::get_pattern("40 53 48 83 EC ? 48 8B D9 48 83 C1 ? E8 ? ? ? ? 65 48 8B 14 25");
});

static hook::cdecl_stub<void(void*)> _netSyncDataBase_EndMutexLock([]()
{
	return hook::get_pattern("48 83 EC 28 65 48 8B 14 25 58 00 00 00 8B 05 ? ? ? ? 48 8B 09");
});

void rage::ScheduleEntityBatch(std::vector<rage::netObject*> objects, rage::sysDependencyBatch::Callback* callback)
{
	static rage::sysDependencyBatch::IpcRef updateRef{};
	if (!updateRef.event)
	{
		updateRef.refCount = 0;
		updateRef.event = CreateEvent(0, false, false, 0);
	}

	size_t size = objects.size();
	if (size == 0)
	{
		return;
	}
	assert(size <= kMaxEntityUpdates);

	rage::sysDependencyBatch* batch[kMaxEntityUpdates]{};
	size_t count = 0;
	for (auto object : objects)
	{
		rage::sysDependencyBatch* instance = object->GetSchedulerUpdate();
		instance->argument = (void*)object;
		instance->ipcEventRef = &updateRef;
		instance->m_callerFunc = callback;
		instance->unk_02 = 0;
		instance->unk_03 = 0;
		instance->unk_01 &= 0x1FFFFFFFu;
		instance->unk_01 |= 0x20000000u;
		instance->m_unkFlags &= 0xCA;
		instance->m_unkFlags |= 0x0A;
		batch[count] = instance;
		count++;
	}

	*g_useLocks = true;
	_InterlockedExchangeAdd(&updateRef.refCount, count);
	rage::InsertDependencyBatch(batch, count);
	if (updateRef.event && _InterlockedExchangeAdd(&updateRef.refCount, 0x80000000))
	{
		WaitForSingleObject(updateRef.event, 0xFFFFFFFF);
	}
	updateRef.refCount = 0;
	*g_useLocks = false;
}

void rage::ScheduleSyncBatch(SyncWorkItem* items, int size, rage::sysDependencyBatch::Callback* callback)
{
	static rage::sysDependencyBatch::IpcRef updateRef{};
	if (!updateRef.event)
	{
		updateRef.refCount = 0;
		updateRef.event = CreateEvent(0, false, false, 0);
	}

	if (size == 0)
	{
		return;
	}
	// this should never be hit.
	assert(size <= kMaxEntityUpdates);

	rage::sysDependencyBatch* batch[kMaxEntityUpdates]{};
	size_t count = 0;
	for (int i = 0; i < size; i++)
	{
		auto& item = items[i];
		rage::sysDependencyBatch* instance = item.object->GetSchedulerUpdate();
		instance->argument = (void*)&item;
		instance->ipcEventRef = &updateRef;
		instance->m_callerFunc = callback;
		instance->unk_02 = 0;
		instance->unk_03 = 0;
		instance->unk_01 &= 0x1FFFFFFFu;
		instance->unk_01 |= 0x20000000u;
		instance->m_unkFlags &= 0xCA;
		instance->m_unkFlags |= 0x0A;
		batch[count] = instance;
		count++;
	}

	*g_useLocks = true;
	_InterlockedExchangeAdd(&updateRef.refCount, count);
	rage::InsertDependencyBatch(batch, count);
	if (updateRef.event && _InterlockedExchangeAdd(&updateRef.refCount, 0x80000000))
	{
		WaitForSingleObject(updateRef.event, 0xFFFFFFFF);
	}
	updateRef.refCount = 0;
	*g_useLocks = false;
}

void rage::SyncTreeBeginWrite(rage::netSyncTree::LockMutex& mutex, rage::netSyncTree* tree, void* syncData)
{
	_netSyncTree_LockTreeMutex(&mutex, tree);

	if (syncData)
	{
		_netSyncDataBase_StartMutexLock(syncData);
	}
}

void rage::SyncTreeEndWrite(rage::netSyncTree::LockMutex& mutex, void* syncData)
{
	if (syncData)
	{
		_netSyncDataBase_EndMutexLock(&syncData);
	}

	if (mutex.m_lock == rage::netSyncTree::LockMutex::TREE_THREAD_WRITER)
	{
		_netSyncTree_EndMutexLock(mutex.m_tree);
	}
}

bool rage::schedulers::DependencyThreadUpdate(rage::sysDependencyBatch* batch)
{
	rage::netObject* object = (rage::netObject*)batch->argument;
	rage::sysDependencyBatch::IpcRef* ref = batch->ipcEventRef;

	object->DependencyThreadUpdate();
	if (_InterlockedExchangeAdd(&ref->refCount, -1) == 0x80000001)
	{
		SetEvent(ref->event);
	}
	return true;
}

extern thread_local rage::netObject* g_curNetObject;

namespace rage
{
void InitTree(rage::netSyncTree* tree);
}

bool rage::schedulers::SyncWorkSerialise(rage::sysDependencyBatch* batch)
{
	SyncWorkItem* item = (SyncWorkItem*)batch->argument;
	rage::sysDependencyBatch::IpcRef* ref = batch->ipcEventRef;

	g_curNetObject = item->object;

	rage::datBitBuffer rlBuffer(item->storage, sizeof(item->storage));

	rage::netSyncTree* syncTree = item->object->GetSyncTree();
	void* syncData = item->object->GetSyncData();
	rage::netSyncTree::LockMutex mutex{};

	SyncTreeBeginWrite(mutex, syncTree, syncData);
	rage::InitTree(syncTree);
	item->shouldTrySend = syncTree->WriteTreeCfx(item->syncType, (item->syncType == 2 || item->syncType == 4) ? 1 : 0, item->object, &rlBuffer, item->ts, nullptr, 31, nullptr, &item->lastChangeTime);
	item->dataLen = rlBuffer.GetDataLength();
	SyncTreeEndWrite(mutex, syncData);

	if (_InterlockedExchangeAdd(&ref->refCount, -1) == 0x80000001)
	{
		SetEvent(ref->event);
	}
	return true;
}

static HookFunction syncMtUpdate([]()
{
	g_useLocks = hook::get_address<bool*>(hook::get_pattern("8A 1D ? ? ? ? 4C 8D 35", 2));
});

#include <StdInc.h>
#include <Hooking.h>
#include <Hooking.Stubs.h>

#include <netObjectMgr.h>
#include <ICoreGameInit.h>

static rage::netObjectMgr** g_objectMgr;

static hook::cdecl_stub<rage::netObject* (rage::netObjectMgr*, uint16_t, bool)> _getNetworkObject([]()
{
	return hook::get_pattern("66 89 54 24 ? 56", -0xA);
});

struct IpcRef
{
	uint32_t* addend;
	HANDLE* handle;
};


bool(*_updateNetObjectMultiThreadedCB)(void*);
struct sysDependencyBatch
{
	typedef bool(Callback)(void*);

	Callback* m_callerFunc; // 0x00
	char pad[8];
	uint32_t unk_01; // r12d
	uint16_t unk_02; // r12w
	uint8_t unk_03; // r12b
	uint8_t unk_04;
	char pad3[8];
	void* argument; // rdi
	uint32_t* ipcEventRef; // rax
};
static_assert(offsetof(sysDependencyBatch, m_callerFunc) == 0);
static_assert(offsetof(sysDependencyBatch, unk_01) == 16);
static_assert(offsetof(sysDependencyBatch, unk_02) == 20);
static_assert(offsetof(sysDependencyBatch, unk_03) == 22);
static_assert(offsetof(sysDependencyBatch, argument) == 32);
static_assert(offsetof(sysDependencyBatch, ipcEventRef) == 40);

static bool updateTest(void* batch)
{
	sysDependencyBatch* dependency = (sysDependencyBatch*)batch;

	rage::netObject* object = (rage::netObject*)dependency->argument;

	if (!object)
	{
		return true;
	}

	object->DependencyThreadUpdate();

	if (_InterlockedExchangeAdd(dependency->ipcEventRef, 0xFFFFFFFF) == 0x80000001)
	{
		SetEvent(reinterpret_cast<uint8_t*>(dependency->ipcEventRef) + 8);
	}

	return true;
}

static hook::cdecl_stub<int(sysDependencyBatch**, unsigned int)> rage__sysDependencyScheduler__InsertBatch([]()
{
	return hook::get_call(hook::get_pattern("E8 ? ? ? ? 41 8B 87 ? ? ? ? 85 C0"));
});

namespace rage
{
netObjectMgr* netObjectMgr::GetInstance()
{
	return *g_objectMgr;
}

netObject* netObjectMgr::GetNetworkObject(uint16_t id, bool a3)
{
	return _getNetworkObject(this, id, a3);
}

void netObjectMgr::UpdateAllNetworkObjects(std::unordered_map<uint32_t, rage::netObject*>& entities)
{
	auto objManager = rage::netObjectMgr::GetInstance();
	if (!objManager)
	{
		return;
	}

	static constexpr size_t size = offsetof(rage::netObjectMgr, m_autoLock);
	static_assert(offsetof(rage::netObjectMgr, m_autoLock) == 0xBE88);

	//Singlethreaded block start
	objManager->PreMultithreadedUpdate();

	int mtUpdateIndex = 0;
	sysDependencyBatch* mtUpdateList[392]{};

	int objectUpdateList = 0;
	rage::netObject* stUpdateList[392 * 2]{};

	_RTL_CRITICAL_SECTION* lock = &objManager->m_autoLock;

	if (lock && lock->DebugInfo)
	{
		EnterCriticalSection(lock);
	}

	uint32_t* ipcEventRef = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(objManager) + 48760);

	// Run MainThread updates here while building a list to be sent to dependency workers
	{
		for (auto& clone : entities)
		{
			rage::netObject* object = clone.second;
			if (!object)
			{
				continue;
			}

			// TODO: Wants to delete flag check.
			object->MainThreadUpdate();

			sysDependencyBatch* cloneSyncCB = (sysDependencyBatch*)(clone.second + 144);
			if (cloneSyncCB && mtUpdateIndex < 0x188)
			{
				*(uint8_t*)(object + 167) &= 0xCA;
				*(uint8_t*)(object + 167) |= 0xA;
				cloneSyncCB->m_callerFunc = _updateNetObjectMultiThreadedCB; 
				cloneSyncCB->argument = object;
				cloneSyncCB->ipcEventRef = ipcEventRef;
				cloneSyncCB->unk_01 = (1u << 29);
				cloneSyncCB->unk_02 = 0;
				cloneSyncCB->unk_03 = 0;
				cloneSyncCB->unk_04 = (cloneSyncCB->unk_04 & 0xCA) | 0x0A;

				memset(cloneSyncCB->pad3, 0, sizeof(cloneSyncCB->pad3));
				*(uint32_t*)(object + 160) = 0;
				*(WORD*)(object + 164) = 0;
				*(uint8_t*)(object + 166) = 0;
				*reinterpret_cast<uint64_t*>(object + 184) = reinterpret_cast<uint64_t>(ipcEventRef);
				*reinterpret_cast<uint64_t*>(object + 176) = reinterpret_cast<uint64_t>(object);
				*(uint32_t*)(object + 160) &= 0x1FFFFFFFu;
				*(uint32_t*)(object + 160) |= 0x20000000u;

				int mtIdx = mtUpdateIndex++;
				mtUpdateList[mtIdx] = cloneSyncCB;
			}

			// Add objects to list to be updated post multi-thread update
			stUpdateList[objectUpdateList++] = object;
		}
	}

	if (objManager->m_autoLock.DebugInfo)
	{
		LeaveCriticalSection(&objManager->m_autoLock);
	}

	objManager->PostSinglethreadedUpdate();

	if (mtUpdateIndex > 0)
	{
		objManager->PreMultithreadedUpdate();
		_InterlockedExchangeAdd(ipcEventRef, mtUpdateIndex);
		rage__sysDependencyScheduler__InsertBatch(mtUpdateList, mtUpdateIndex);

		if (ipcEventRef)
		{
			if (_InterlockedExchangeAdd(ipcEventRef, 0x80000000))
			{
				WaitForSingleObject(reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(objManager) + 48768), 0xFFFFFFFF) == 1;
			}
			*ipcEventRef = 0;
		}
		objManager->PostMultithreadedUpdate();
	}

	for (int i = 0; i < objectUpdateList; i++)
	{
		auto obj = stUpdateList[i];
		if (obj)
		{

			//TODO: P2P Does extra logic with updating local objects, should copy them overhere (onesync previously didn't do these)
			obj->PostDependencyThreadUpdate();
		}
	}

}
}

extern ICoreGameInit* icgi;

static void (*g_updateAllNetworkObjects)(void*);
static void netObjectMgr__updateAllNetworkObjects(void* objMgr)
{
	if (icgi->OneSyncEnabled)
	{
		return;
	}

	g_updateAllNetworkObjects(objMgr);
}

extern ICoreGameInit* icgi;

static void (*g_updateAllNetworkObjects)(void*);
static void netObjectMgr__updateAllNetworkObjects(void* objMgr)
{
	if (icgi->OneSyncEnabled)
	{
		return;
	}

	g_updateAllNetworkObjects(objMgr);
}

static bool (*g_updateNetObjectMultiThreaded)(sysDependencyBatch*);
static bool UpdateNetObjectMultiThreadedCB(sysDependencyBatch* batch)
{
	batch;
	return g_updateNetObjectMultiThreaded(batch);
}

static bool* g_mtSyncTree;

static HookFunction hookFunction([]()
{
	g_objectMgr = hook::get_address<rage::netObjectMgr**>(hook::get_pattern("45 0F 57 C0 48 8B 35 ? ? ? ? 0F 57 FF", 7));
	_updateNetObjectMultiThreadedCB = hook::get_pattern<bool(void*)>("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC ? 48 8B 71 ? 48 8B 69");

	// Don't run function in onesync. (playerObjects is 32-sized and isn't given information in onesync)
	g_updateAllNetworkObjects = hook::trampoline(hook::get_call(hook::get_pattern("E8 ? ? ? ? 44 8B 35 ? ? ? ? 33 C0")), netObjectMgr__updateAllNetworkObjects);
	g_mtSyncTree = hook::get_address<bool*>(hook::get_pattern("80 3D ? ? ? ? ? 0F 84 ? ? ? ? 80 7E", 3));
	//*g_mtSyncTree = false;

	//g_updateNetObjectMultiThreaded = hook::trampoline(hook::get_pattern("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC ? 48 8B 71 ? 48 8B 69"), UpdateNetObjectMultiThreadedCB);
	// Don't run function in onesync. (playerObjects is 32-sized and isn't given information in onesync)
	//g_updateAllNetworkObjects = hook::trampoline(hook::get_call(hook::get_pattern("E8 ? ? ? ? 44 8B 35 ? ? ? ? 33 C0")), netObjectMgr__updateAllNetworkObjects);
});

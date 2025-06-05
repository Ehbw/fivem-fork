#include <StdInc.h>
#include <Hooking.h>
#include <Hooking.Stubs.h>

#include <CloneManager.h>
#include <netObjectMgr.h>
#include <ICoreGameInit.h>

static rage::netObjectMgr** g_objectMgr;
static DWORD g_mainThreadId;
bool (*_updateNetObjectMultiThreadedCB)(void*);

static hook::cdecl_stub<rage::netObject* (rage::netObjectMgr*, uint16_t, bool)> _getNetworkObject([]()
{
	return hook::get_pattern("66 89 54 24 ? 56", -0xA);
});

struct sysDependencyBatch
{
	typedef bool(Callback)(void*);

	struct IpcRef
	{
		uint32_t refCount;
		HANDLE event;
	};

	Callback* m_callerFunc; // 0x00
	char pad[8];
	uint32_t unk_01; // r12d
	uint16_t unk_02; // r12w
	uint8_t unk_03; // r12b
	uint8_t unk_04;
	char pad3[8];
	void* argument; // rdi
	IpcRef* ipcEventRef; // rax
};
static_assert(offsetof(sysDependencyBatch, m_callerFunc) == 0);
static_assert(offsetof(sysDependencyBatch, unk_01) == 16);
static_assert(offsetof(sysDependencyBatch, unk_02) == 20);
static_assert(offsetof(sysDependencyBatch, unk_03) == 22);
static_assert(offsetof(sysDependencyBatch, argument) == 32);
static_assert(offsetof(sysDependencyBatch, ipcEventRef) == 40);

static bool (*g_updateNetObjectMultiThreaded)(void*);
static bool UpdateNetObjectMultiThreadedCB(void* batch)
{
	sysDependencyBatch* dependency = (sysDependencyBatch*)batch;

	if (!dependency)
	{
		trace("bad\n");
	}

	return g_updateNetObjectMultiThreaded(dependency);
}

static bool updateTest(void* batch)
{
	sysDependencyBatch* dependency = (sysDependencyBatch*)batch;

	rage::netObject* object = (rage::netObject*)dependency->argument;

	if (g_mainThreadId == GetCurrentThreadId())
	{
		//BREAKPOINT
		return false;
	}

	if (!object)
	{
		// BREAKPOINT
		return true;
	}

	if (dependency->unk_04 != 10)
	{
		// BREAKPOINT
		trace("unk-04 bad??\n");
	}
	object->DependencyThreadUpdate();

	if (_InterlockedExchangeAdd(&dependency->ipcEventRef->refCount, 0xFFFFFFFF) == 0x80000001)
	{
		SetEvent(&dependency->ipcEventRef->event);
	}

	return true;
}

static hook::cdecl_stub<int(sysDependencyBatch**, unsigned int)> rage__sysDependencyScheduler__InsertBatch([]()
{
	return hook::get_call(hook::get_pattern("E8 ? ? ? ? 41 8B 87 ? ? ? ? 85 C0"));
});

static bool* g_useLocks;

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

void netObjectMgr::UpdateAllNetworkObjects()
{
	auto objManager = rage::netObjectMgr::GetInstance();
	if (!objManager)
	{
		return;
	}

	if (GetCurrentThreadId() != g_mainThreadId)
	{
		// BREAKPOINT
		return;
	}

	static constexpr size_t size = offsetof(rage::netObjectMgr, m_autoLock);
	static_assert(offsetof(rage::netObjectMgr, m_autoLock) == 0xBE88);

	//Singlethreaded block start
	objManager->PreSinglethreadedUpdate();

	int mtUpdateIndex = 0;
	sysDependencyBatch* mtUpdateList[392]{};

	int objectUpdateList = 0;
	rage::netObject* stUpdateList[392 * 2]{};

	_RTL_CRITICAL_SECTION* lock = &objManager->m_autoLock;

	if (lock && lock->DebugInfo)
	{
		EnterCriticalSection(lock);
	}

	auto& entities = TheClones->GetObjectList();

	sysDependencyBatch::IpcRef* ipcEventRef = reinterpret_cast<sysDependencyBatch::IpcRef*>(reinterpret_cast<uint8_t*>(objManager) + 0x0BE78 /*48760*/);
	// Run MainThread updates here while building a list to be sent to dependency workers
	{
		for (auto& clone : entities)
		{
			rage::netObject* object = clone;
			if (!object)
			{
				continue;
			}

			if ((*(int32_t*)(object + 0x48) & 1) == 0)
			{
				object->MainThreadUpdate();

				sysDependencyBatch* cloneSyncCB = (sysDependencyBatch*)(object + 144);
				if (cloneSyncCB && object && mtUpdateIndex < 0x188)
				{

					cloneSyncCB->unk_04 &= 0xCA;
					cloneSyncCB->unk_04 |= 0x0A;
					cloneSyncCB->m_callerFunc = updateTest; // updateTest; //_updateNetObjectMultiThreadedCB;
					cloneSyncCB->unk_02 = 0;
					cloneSyncCB->unk_03 = 0;
					cloneSyncCB->argument = object;
					cloneSyncCB->ipcEventRef = ipcEventRef;
					cloneSyncCB->unk_01 &= 0x1FFFFFFFu;
					cloneSyncCB->unk_01 |= 0x20000000u;

					//*(uint8_t*)(object + 167) &= 0xCA;
					//*(uint8_t*)(object + 167) |= 0xA;
					// memset(cloneSyncCB->pad3, 0, sizeof(cloneSyncCB->pad3));
					//*(uint32_t*)(object + 160) = 0;
					//*(WORD*)(object + 164) = 0;
					//*(uint8_t*)(object + 166) = 0;
					//*(sysDependencyBatch::IpcRef**)(object + 184) = ipcEventRef;
					//*(rage::netObject**)(object + 176) = object;
					//*(uint32_t*)(object + 160) &= 0x1FFFFFFFu;
					//*(uint32_t*)(object + 160) |= 0x20000000u;

					int mtIdx = mtUpdateIndex++;
					mtUpdateList[mtIdx] = cloneSyncCB;
				}
			}

			// Add objects to list to be updated post multi-thread update
			stUpdateList[objectUpdateList++] = object;
		}

	}

	if (lock && lock->DebugInfo)
	{
		LeaveCriticalSection(lock);
	}

	objManager->PostSinglethreadedUpdate();

	if (mtUpdateIndex > 0)
	{
		*g_useLocks = true;
		//trace("mtUpdateIndex %i\n", mtUpdateIndex);
		_InterlockedExchangeAdd((volatile long*)ipcEventRef, mtUpdateIndex);
		rage__sysDependencyScheduler__InsertBatch(mtUpdateList, mtUpdateIndex);

		if (ipcEventRef != 0)
		{
			if (_InterlockedExchangeAdd((volatile long*)ipcEventRef, 0x80000000))
			{
				WaitForSingleObject(&ipcEventRef->event, 0xFFFFFFFF) == 1;
			}

			ipcEventRef->refCount = 0;
		}

		*g_useLocks = false;	
	}

	for (int i = 0; i < objectUpdateList; i++)
	{
		auto obj = stUpdateList[i];
		if (obj)
		{

			obj->PostDependencyThreadUpdate();

			// TODO: P2P Does extra logic with updating local objects, should copy them overhere (onesync previously didn't do these)
			if ((obj->GetGameObject() || obj->CanSyncWithNoGameObject()) && !obj->syncData.isRemote)
			{
				uint8_t minUpdateLevel = obj->GetMinimumUpdateLevel();


			}
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
		rage::netObjectMgr::UpdateAllNetworkObjects();
		return;
	}

	g_updateAllNetworkObjects(objMgr);
}


static bool(*g_sub_14237C5A8)(void* a1, char* a2);
static bool sub_14237C5A8(void* a1, char* a2)
{
	if (!a1 || a1 == nullptr)
	{
		__debugbreak();
	}

	return g_sub_14237C5A8(a1, a2);
}

static bool (*_unksynccb)(void*);
static bool unkSyncCB(void* a1)
{
	return _unksynccb(a1);
}



static bool* g_mtSyncTree;

static HookFunction hookFunction([]()
{
	g_mainThreadId = GetCurrentThreadId();
	//g_hasObjectMgrInitalized = hook::get_address<bool*>(hook::get_pattern("38 15 ? ? ? ? 74 ? 48 8B 05 ? ? ? ? 8B 88", 3));

	_unksynccb = hook::trampoline(hook::get_pattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 48 8B 69 ? 48 8B 79"), unkSyncCB);
	//g_preSTUpdate = hook::trampoline(hook::get_pattern("40 53 48 83 EC ? E8 ? ? ? ? 48 8B 1D ? ? ? ? 48 8B CB"), prestudpdate);
	g_sub_14237C5A8 = hook::trampoline(hook::get_pattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8D 99 ? ? ? ? 48 8B F1 48 8B 03 48 8B CB 48 8B FA FF 90"), sub_14237C5A8);

	g_objectMgr = hook::get_address<rage::netObjectMgr**>(hook::get_pattern("45 0F 57 C0 48 8B 35 ? ? ? ? 0F 57 FF", 7));
	_updateNetObjectMultiThreadedCB = hook::get_pattern<bool(void*)>("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC ? 48 8B 71 ? 48 8B 69");

	// Don't run function in onesync. (playerObjects is 32-sized and isn't given information in onesync)
	g_updateAllNetworkObjects = hook::trampoline(hook::get_call(hook::get_pattern("E8 ? ? ? ? 44 8B 35 ? ? ? ? 33 C0")), netObjectMgr__updateAllNetworkObjects);
	g_mtSyncTree = hook::get_address<bool*>(hook::get_pattern("80 3D ? ? ? ? ? 0F 84 ? ? ? ? 80 7E", 3));
	
	g_useLocks = hook::get_address<bool*>(hook::get_pattern("44 38 3D ? ? ? ? 41 8A DF 74 ? 44 38 3D ? ? ? ? 74 ? 48 8D 0D ? ? ? ? E8 ? ? ? ? B3 ? 48 8B 84 24", 3));

	g_updateNetObjectMultiThreaded = hook::trampoline(hook::get_pattern("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC ? 48 8B 71 ? 48 8B 69"), UpdateNetObjectMultiThreadedCB);
	// Don't run function in onesync. (playerObjects is 32-sized and isn't given information in onesync)
	//g_updateAllNetworkObjects = hook::trampoline(hook::get_call(hook::get_pattern("E8 ? ? ? ? 44 8B 35 ? ? ? ? 33 C0")), netObjectMgr__updateAllNetworkObjects);
});

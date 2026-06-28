#include <StdInc.h>
#include <Hooking.h>
#include <RageScheduler.h>
#include <CoreConsole.h>

static hook::cdecl_stub<int(rage::sysDependencyBatch**, int)> _sysDependencyScheduler_insertBatch([]()
{
	return hook::get_call(hook::get_pattern("E8 ? ? ? ? 41 8B 87 ? ? ? ? 85 C0 "));
});

namespace rage
{
DLL_EXPORT int InsertDependencyBatch(sysDependencyBatch** updateList, int entries)
{
	return _sysDependencyScheduler_insertBatch(updateList, entries);
}
}

// Example usage of sysDependencyScheduler
#if 0
static bool BatchTest(rage::sysDependencyBatch* batch)
{
	void* argument = batch->argument;
	rage::sysDependencyBatch::IpcRef* ref = batch->ipcEventRef;

	// ...
	
	if (_InterlockedExchangeAdd(&ref->refCount, -1) == 0x80000001)
	{
		// Signals back to the main thread to let it know the tasks have finished
		SetEvent(ref->event);
	}

	// false causes it to retry.
	return true;
}

static HookFunction hookFunction([]()
{
	static ConsoleCommand testBatchInsert("testBatchInsert", []()
	{
		rage::sysDependencyBatch::IpcRef* ref = new rage::sysDependencyBatch::IpcRef{};
		ref->event = CreateEvent(0, 0, 0, 0);

		rage::sysDependencyBatch* batch[10]{};
		for (int i = 0; i < 10; i++)
		{
			rage::sysDependencyBatch* instance = new rage::sysDependencyBatch{};
			instance->argument = (void*)i;

			instance->ipcEventRef = ref;
			instance->m_callerFunc = BatchTest;
			instance->unk_02 = 0;
			instance->unk_03 = 0;
			instance->unk_01 &= 0x1FFFFFFFu;
			instance->unk_01 |= 0x20000000u;
			instance->m_unkFlags &= 0xCA;
			instance->m_unkFlags |= 0x0A;
			batch[i] = instance;
		}

		_InterlockedExchangeAdd(&ref->refCount, 10);
		rage::InsertDependencyBatch(batch, 10);

		if (ref->refCount)
		{
			if (_InterlockedExchangeAdd(&ref->refCount, 0x80000000))
			{
				WaitForSingleObject(ref->event, 0xFFFFFFFF);
			}
			ref->refCount = 0;
			delete ref;
		}
	});
});
#endif

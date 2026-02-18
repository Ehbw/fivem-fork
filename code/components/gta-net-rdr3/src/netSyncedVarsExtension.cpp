#include <StdInc.h>

#include <Hooking.h>
#include <Hooking.Stubs.h>

#include <ICoreGameInit.h>
#include <NetLibrary.h>

#include <Error.h>
#include <rlNetBuffer.h>

static ICoreGameInit* icgi;

static uint64_t (*g_origSyncedUintFalse_GetMaxBits)(void*);
static uint64_t SyncedIntFalse_GetMaxBits(void* self)
{
	auto val = g_origSyncedUintFalse_GetMaxBits(self);
	trace("%s %i %p\n", __func__, val, _ReturnAddress());
	return (val == 13 && icgi->OneSyncBigIdEnabled) ? 16 : val;
}

static uint64_t (*g_origSyncedInt_GetMaxBits)(void*);
static uint64_t SyncedInt_GetMaxBits(void* self)
{
	auto val = g_origSyncedInt_GetMaxBits(self);
	trace("%s %i %p\n", __func__, val, _ReturnAddress());
	return (val == 13 && icgi->OneSyncBigIdEnabled) ? 16 : val;
}

static uint64_t (*g_origSyncedUnkD038_GetMaxBits)(void*);
static uint64_t SyncedUnkD038_GetMaxBits(void* self)
{
	auto val = g_origSyncedUnkD038_GetMaxBits(self);
	trace("%s %i %p\n", __func__, val, _ReturnAddress());
	return (val == 13 && icgi->OneSyncBigIdEnabled) ? 16 : val;
}

static uint64_t(*g_origSyncedVarGroup_GetMaxBits)(void*);
static uint64_t SyncedVarGroup_GetMaxBits(void* self)
{
	trace("SyncedVarGroup::GetMaxBits (vtable %p) %i\n", (void*)hook::get_unadjusted(*(uint64_t**)self), g_origSyncedVarGroup_GetMaxBits(self));
	return g_origSyncedVarGroup_GetMaxBits(self) + (static_cast<unsigned long long>(3) * 5);
}

static bool (*g_origSyncedVarGroup_UsesIdMappings)(void*);
static bool SyncedVarGroup_UsesIdMappings(void* self)
{
	trace("%s %i (%p) %p\n", __func__, g_origSyncedVarGroup_UsesIdMappings(self), (void*)hook::get_unadjusted(*(uint64_t**)self), _ReturnAddress());
	return g_origSyncedVarGroup_UsesIdMappings(self);
}

static bool (*g_origMeleeArbitationFailed)(void*, int);
static bool _meleeArbitationFailed(void* a1, int a2)
{
	trace("melee arbitation failed %p %i\n", a1, a2);
	return g_origMeleeArbitationFailed(a1, a2);
}

static void* (*g_sub_14014D)(void* a1, void* a2);
static void* sub_14014D(void* a1, void* a2)
{
	trace("a1 %p a2 %p\n", (void*)hook::get_unadjusted(*(uint64_t**)a1), (void*)hook::get_unadjusted(*(uint64_t**)a2));	
	return g_sub_14014D(a1, a2);
}

template<int BigSize, int DefaultSize>
static int64_t ReturnSize(void* self)
{
	trace("CSyncedObject returnSize<%i,%i> %p, %i %p\n", BigSize, DefaultSize, (void*)hook::get_unadjusted(*(uint64_t**)self), (icgi->OneSyncBigIdEnabled ? BigSize : DefaultSize), _ReturnAddress());
	return icgi->OneSyncBigIdEnabled ? BigSize : DefaultSize;
}

static int64_t (*g_syncedPedSerialise)(hook::FlexStruct*, void*, rage::datBitBuffer*);
static int64_t __stdcall SyncedPedSerialise(hook::FlexStruct* self, void* a2, rage::datBitBuffer* buffer)
{
	int64_t size = g_syncedPedSerialise(self, a2, buffer);
	trace("Synced ped serialise size %i %p\n", size, _ReturnAddress());
	return size;
}

static int64_t (*g_syncedTaskEntSerialise)(hook::FlexStruct*, void*, rage::datBitBuffer*);
static int64_t __stdcall SyncedTaskEntSerialise(hook::FlexStruct* self, void* a2, rage::datBitBuffer* buffer)
{
	int64_t size = g_syncedTaskEntSerialise(self, a2, buffer);
	trace("SyncedTaskEntSerialise size %i %p\n", size, _ReturnAddress());
	return size;
}

static HookFunction hookSyncedExtensions([]()
{
	g_sub_14014D = hook::trampoline(hook::get_pattern("48 8B 02 48 8B CA 48 FF 60 ? CC F3 B8"), sub_14014D);

	// Patch CSyncVars* vtable methods for Size calculator and ID Mapping to support length hack in onesync.
	static constexpr int kSyncedVarSerialiseIndex = 8;
	static constexpr int kSyncedVarGetMaxBitsIndex = 9;
	// This may be wrong/completely irrelevant to object mapping
	static constexpr int kSyncedVarUsesIdMappingsIndex = 19;

	// Patchs static CSynced* GetMaxBits functions to support 16 bit objectIds.
	// TODO: Automatically fetch these data sizes to better support future game builds.
	{
		// Entities, excluding CSyncedPed as that is dynamically calculated.
		const auto syncedEntity = hook::get_address<uintptr_t*>(hook::get_pattern("89 AB 84 06 00 00 48 8D 05 ? ? ? ? 48 89", 9));
		const auto syncedConstPed = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 66 89 AB", 3));
		const auto syncedObject = hook::get_address<uintptr_t*>(hook::get_pattern("44 89 77 0C 48 89 07", -4));
		const auto syncedVehicle = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 49 89 00 4D 89 50", 3));

		// Targets/Targetting
		const auto syncedTarget = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 45 33 C0 48 8B D6", 3));
		const auto syncedWeaponTarget = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 03 8B 86", 3));
		const auto syncedAbstractTarget = hook::get_address<uintptr_t*>((uintptr_t)hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 89 B3 ? ? ? ? 48 8D 93 ? ? ? ? 48 89 B3")) + 0x52, 3, 7);

		// Tasks
		const auto syncedTask = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 07 48 8D 57 ? 48 89 1A", 3));
		const auto syncedTaskUnk = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 C7 44 24 ? ? ? ? ? 48 89 07", 3));
		const auto syncedTaskUnk2 = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 15 ? ? ? ? 44 89 8B ? ? ? ? 4C 89 8B", 3));
		const auto syncedTaskUnk3 = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 03 48 8B C3 48 83 C4 ? 5B C3 90 26 EB", 3));

		// Synced Entities
		hook::put(&syncedEntity[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16 + 1, 14>);
		hook::put(&syncedConstPed[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16 + 1, 14>);
		hook::put(&syncedObject[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16 + 1, 14>);
		hook::put(&syncedVehicle[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16 + 1, 14>);

		// Synced Targetting
		hook::put(&syncedTarget[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<90 + 10 + 3, 90>);
		hook::put(&syncedWeaponTarget[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<90 + 10 + 6, 90>);
		hook::put(&syncedAbstractTarget[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<90 + 10 + 6, 90>);

		// Synced Tasks
		hook::put(&syncedTask[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<531 + 16, 531>);
		hook::put(&syncedTaskUnk[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<531 + 16, 531>);
		hook::put(&syncedTaskUnk2[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<531 + 16, 531>);
		hook::put(&syncedTaskUnk3[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<531 + 16, 531>);
	}

	// The length of these are dynamically calculated
	{
		const auto syncedPed = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 89 AF ? ? ? ? 48 8D 8F ? ? ? ? 48 89 87 ? ? ? ? E8 ? ? ? ? 0F 57 F6", 3));
		g_syncedPedSerialise = (decltype(g_syncedPedSerialise))syncedPed[kSyncedVarSerialiseIndex];
		hook::put(&syncedPed[kSyncedVarSerialiseIndex], (uintptr_t)SyncedPedSerialise);

		const auto syncedUnkTaskEntity = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 03 E8 ? ? ? ? 8B 87", 3));
		g_syncedTaskEntSerialise = (decltype(g_syncedTaskEntSerialise))syncedUnkTaskEntity[kSyncedVarSerialiseIndex];
		hook::put(&syncedUnkTaskEntity[kSyncedVarSerialiseIndex], (uintptr_t)SyncedTaskEntSerialise);
	}

	// The serialiser for these functions will cause the datasize to become larger then what is permitted by this synced var.
	{
		const auto syncedBitfield13 = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 02 66 89 5A ? 66 85 DB 74 ? 48 8B CA E8 ? ? ? ? 65 48 8B 04 25", 3));
		hook::put(&syncedBitfield13[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16, 13>);

		const auto syncedUnkBitfield = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 83 ? ? ? ? 48 8D 8B ? ? ? ? 44 89 B3 ? ? ? ? 4C 89 B3", 3));
		hook::put(&syncedUnkBitfield[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16, 13>);

		auto syncedUnkBitfield2 = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 02 66 89 5A ? 66 85 DB 74 ? 48 8B CA E8 ? ? ? ? 65 48 8B 04 25", 3));
		hook::put(&syncedUnkBitfield2[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16, 13>);

		// TODO: this is a really nasty pattern
		auto syncedFlags = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 0D ? ? ? ? 44 89 48 ? 48 89 08 4C 89 48 ? EB ? 49 8B C1 48 83 C4 ? C3 48 83 EC ? B9 ? ? ? ? E8 ? ? ? ? 45 33 C9 48 85 C0 74 ? 65 48 8B 14 25 ? ? ? ? 48 8D 0D ? ? ? ? 48 89 08 8B 0D ? ? ? ? 41 B8 ? ? ? ? 48 8B 0C CA 4D 39 0C 08 75 ? 33 C9 89 48 ? 4C 89 48", 3));
		hook::put(&syncedFlags[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16, 13>);

		auto syncedFlags2 = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 48 89 02 89 72 ? 85 F6 74 ? 48 8B CA E8 ? ? ? ? 48 8D 8B", 3));
		hook::put(&syncedFlags2[kSyncedVarGetMaxBitsIndex], (uintptr_t)ReturnSize<16, 13>);
	}

	// CSyncedInt, This syncvar has a dynamic length, however doesn't account for length hack.
	{
		auto syncedVtable = hook::get_address<uintptr_t*>(hook::get_pattern("4C 8D 25 ? ? ? ? 89 72", 3));
		g_origSyncedInt_GetMaxBits = (decltype(g_origSyncedInt_GetMaxBits))syncedVtable[kSyncedVarGetMaxBitsIndex];
		hook::put(&syncedVtable[kSyncedVarGetMaxBitsIndex], (uintptr_t)SyncedInt_GetMaxBits);
	}

	// CSyncedInt<uint, false>, Has a dynamic length that doesn't account for length hack
	{
		auto syncedTable = hook::get_address<uintptr_t*>(hook::get_pattern("4C 8D 3D ? ? ? ? 89 5A", 3));
		g_origSyncedUintFalse_GetMaxBits = (decltype(g_origSyncedUintFalse_GetMaxBits))syncedTable[kSyncedVarGetMaxBitsIndex];
		hook::put(&syncedTable[kSyncedVarGetMaxBitsIndex], (uintptr_t)SyncedIntFalse_GetMaxBits);
	}

	// CSyncedUnkD038
	{
		auto syncedTable = hook::get_address<uintptr_t*>(hook::get_pattern("4C 8D 35 ? ? ? ? 8B 86", 3));
		g_origSyncedUnkD038_GetMaxBits = (decltype(g_origSyncedUnkD038_GetMaxBits))syncedTable[kSyncedVarGetMaxBitsIndex];
		hook::put(&syncedTable[kSyncedVarGetMaxBitsIndex], (uintptr_t)SyncedUnkD038_GetMaxBits);
	}

	// Verbose logging should the network event 'CNetworkMeleeArbitrationFailEvent' get hit.
	g_origMeleeArbitationFailed = hook::trampoline(hook::get_pattern("89 54 24 ? 53 48 83 EC ? 48 83 B9"), _meleeArbitationFailed);
	g_origSyncedVarGroup_UsesIdMappings = hook::trampoline(hook::get_pattern("FF 90 ? ? ? ? 48 8B C8 48 8B 10 FF 92 ? ? ? ? 84 C0 75", -0x24), SyncedVarGroup_UsesIdMappings);
	// Temporary extend max bits by 15.
	g_origSyncedVarGroup_GetMaxBits = hook::trampoline(hook::get_pattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 83 79 ? ? 48 8B D9 0F 85 ? ? ? ? 48 8B 01"), SyncedVarGroup_GetMaxBits);
});

static InitFunction initFunction([]()
{
	NetLibrary::OnNetLibraryCreate.Connect([](NetLibrary* netLibrary)
	{
		icgi = Instance<ICoreGameInit>::Get();
	});
});

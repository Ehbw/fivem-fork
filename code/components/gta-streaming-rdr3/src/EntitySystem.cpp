#include <StdInc.h>
#include <EntitySystem.h>

#include <Hooking.h>

static hook::cdecl_stub<fwEntity* (int handle)> getScriptEntity([]()
{
	return hook::pattern("45 8B C1 41 C1 F8 08 45 38 0C 00 75 ? 8B 42 ? 41 0F AF C0").count(1).get(0).get<void>(-81);
});

fwEntity* rage::fwScriptGuid::GetBaseFromGuid(int handle)
{
	return getScriptEntity(handle);
}

static hook::cdecl_stub<fwArchetype*(uint32_t nameHash, rage::fwModelId& id)> getArchetype([]()
{
	return hook::get_call(hook::pattern("8B 4E 08 C1 EB 05 80 E3 01 E8").count(1).get(0).get<void>(9));
});

fwArchetype* rage::fwArchetypeManager::GetArchetypeFromHashKey(uint32_t hash, fwModelId& id)
{
	return getArchetype(hash, id);
}

class VehicleTransport
{
public:
	char m_pad[31];
	VehicleSeatManager m_seatManager;
};

static hook::cdecl_stub<VehicleTransport*(fwEntity*)> getTransport([]()
{
	return hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 8B F8 0F BE 48"));
});

fwEntity* VehicleSeatManager::GetOccupant(int index)
{
	if (index >= 17)
	{
		return nullptr;
	}

	return this->m_occupants[index];
}

static size_t g_seatManagerOffset;

VehicleSeatManager* CVehicle::GetSeatManager()
{
	VehicleTransport* transport = getTransport(this);
	if (transport)
	{
		return (VehicleSeatManager*)((uintptr_t)transport + g_seatManagerOffset);
	}

	return nullptr;
}

bool fwEntity::IsOfType(uint32_t hash)
{
	void** vtbl = *(void***)this;
	if (vtbl)
	{
		uint32_t* hashPtr = &hash;
		bool result = ((bool(*)(fwEntity*, uint32_t*)) vtbl[1])(this, hashPtr);
		return result;
	}

	return false;
}

static hook::cdecl_stub<void* (fwExtensionList*, uint32_t)> getExtension([]()
{
	return hook::get_pattern("73 ? 48 8B 19 EB", -0x2B);
});

static hook::cdecl_stub<void(fwExtensionList*, rage::fwExtension*)> addExtension([]()
{
	return hook::get_pattern("FF 10 83 F8 ? 73 ? 48 8B 03", -0x2F);
});

void fwExtensionList::Add(rage::fwExtension* extension)
{
	return addExtension(this, extension);
}

void* fwExtensionList::Get(uint32_t id)
{
	return getExtension(this, id);
}

uint32_t fwSceneUpdateExtension::GetClassId()
{
	return 0x19;
}

static HookFunction hookFunctionSeatManager([]()
{
	g_seatManagerOffset = *hook::get_pattern<uint8_t>("49 8D 4F ? E8 ? ? ? ? 4C 8B C8", 3);
});

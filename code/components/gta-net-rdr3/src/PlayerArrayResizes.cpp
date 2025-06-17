#include <StdInc.h>

#include <Hooking.h>
#include <Hooking.Stubs.h>
#include <MinHook.h>
#include <NetLibrary.h>

#include <GameInit.h>
#include <DirectXMath.h>

#include <CoreNetworking.h>
#include <CrossBuildRuntime.h>
#include <Error.h>
#include <CloneManager.h>
#include <xmmintrin.h>
#include <netPlayerManager.h>
#include <netObjectMgr.h>
#include <netObject.h>
#include <CoreConsole.h>

static const uint8_t kMaxPlayers = 128; // 32;
static const uint16_t kMaxObjects = std::numeric_limits<uint16_t>::max();

#define MAKE_HOOK(name, ret_type, args, call_args)         \
	static ret_type(*g_orig_##name) args;                  \
	static ret_type Detour_##name args                     \
	{                                                      \
		trace("name: %s called\n", #name);                   \
		return g_orig_##name call_args;                    \
	}

#define MAKE_HOOK_LOG(name, ret_type, args, call_args) \
	static ret_type(*g_orig_##name) args;          \
	static ret_type Detour_##name args             \
	{                                              \
		auto val = g_orig_##name call_args;         \
		trace("name: %s called %i\n", #name, val);         \
		return 16;            \
		}

#define INSTALL_HOOK(name, target_addr)                                            \
	MH_CreateHook(target_addr, Detour_##name, (void**)&g_orig_##name); 

namespace rage
{
	using Vec3V = DirectX::XMVECTOR;
}

//std::array<std::bitset<128>, 128> g_netPlayerVisiblePlayers;
uint32_t g_playerFocusPositionUpdateBitset[4];
uint32_t g_netPlayerCreationAcked[4];

// Object player related bitsets that aren't >32 safe
std::unordered_map<uint16_t, std::array<uint32_t, 4>> g_objCreationAckedPlayers;
std::unordered_map<uint8_t, rage::Vec3V> g_playerFocusPositions;


// Extend bitsets to handle more then 32 players at a time
constexpr size_t kBitsPerWord = 32;
constexpr size_t kBitsetArraySize = (kMaxPlayers / 2^4) / kBitsPerWord;
using PlayerBitset = uint32_t[kMaxPlayers][kBitsetArraySize];
using ObjectBitset = uint32_t[kMaxObjects][kBitsetArraySize];

PlayerBitset g_player96CBitset;
PlayerBitset g_player968Bitset;
PlayerBitset g_netPlayerVisiblePlayers;











extern ICoreGameInit* icgi;
extern CNetGamePlayer* g_players[256];
extern CNetGamePlayer* g_playerListRemote[256];
extern int g_playerListCountRemote;

template<bool Onesync, bool Legacy>
static bool Return()
{
	return icgi->OneSyncEnabled ? Onesync : Legacy;
}

struct PatternPair
{
	std::string_view pattern;
	int offset;
	int operand_remaining = 4;
};

struct PatternClampPair
{
	std::string_view pattern;
	int offset;
	bool clamp;
};

static void RelocateRelative(void* base, std::initializer_list<PatternPair> list, int intendedEntries = -1)
{
	void* oldAddress = nullptr;

	if (intendedEntries >= 0 && list.size() != intendedEntries)
	{
		__debugbreak();
		return;
	}


	for (auto& entry : list)
	{
		auto location = hook::get_pattern<int32_t>(entry.pattern, entry.offset);

		if (!oldAddress)
		{
			oldAddress = hook::get_address<void*>(location, 0, entry.operand_remaining);
		}

		auto curTarget = hook::get_address<void*>(location, 0, entry.operand_remaining);
		assert(curTarget == oldAddress);

		hook::put<int32_t>(location, (intptr_t)base - (intptr_t)location - entry.operand_remaining);
	}
}

struct PatternPatchPair
{
	std::string_view pattern;
	int offset;
	int intendedValue;
	int newValue;
};

template<class T>
static void PatchValue(std::initializer_list<PatternPatchPair> list)
{
	for (auto& entry : list)
	{
		auto location = hook::pattern(entry.pattern).count(1).get(0).get<T>(entry.offset);
		auto origVal = *location;
		trace("orig value %i %i\n", origVal, (uint32_t)origVal);
		assert(origVal == entry.intendedValue);
		hook::put<T>(location, entry.newValue);
	}
}

static hook::cdecl_stub<rage::Vec3V*(rage::Vec3V*, CNetGamePlayer*, char*)> _getNetPlayerFocusPosition([]()
{
	return hook::get_pattern("41 22 C2 41 3A C1 0F 93 C0", -0x74);
});

static void (*g_origUpdatePlayerFocusPosition)(void*, CNetGamePlayer*);
static void UpdatePlayerFocusPosition(void* objMgr, CNetGamePlayer* player)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origUpdatePlayerFocusPosition(objMgr, player);
	}

	bool isRemote = player->physicalPlayerIndex() == 31;
	uint8_t playerIndex = player->physicalPlayerIndex();

	// Find the players index if remote.
	if (isRemote)
	{
		for (int i = 0; i < 256; i++)
		{
			if (g_players[i] == player)
			{
				playerIndex = i;
				break;
			}
		}
	}

	if (playerIndex > kMaxPlayers)
	{
		return;
	}

	rage::Vec3V position;
	char outFlag = 0;
	rage::Vec3V* focusPosition = _getNetPlayerFocusPosition(&position, player, &outFlag);

	g_playerFocusPositions[playerIndex] = *focusPosition;

	if (outFlag != 0)
	{
		g_playerFocusPositionUpdateBitset[playerIndex >> 5] |= 1 << (playerIndex & 31);
	}
}

static rage::Vec3V*(*g_origGetNetPlayerRelevancePosition)(rage::Vec3V* position, CNetGamePlayer* player, void* unk);
static rage::Vec3V* GetPlayerFocusPosition(rage::Vec3V* position, CNetGamePlayer* player, uint8_t* unk)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetNetPlayerRelevancePosition(position, player, unk);
	}

	bool isRemote = player->physicalPlayerIndex() == 31;
	uint8_t playerIndex = player->physicalPlayerIndex();

	// Find the players index if remote.
	if (isRemote)
	{
		for (int i = 0; i < 256; i++)
		{
			if (g_players[i] == player)
			{
				playerIndex = i;
				break;
			}
		}
	}

	if (playerIndex > kMaxPlayers)
	{
		return nullptr;
	}

	if (unk)
	{
		uint32_t bitset = g_playerFocusPositionUpdateBitset[playerIndex >> 5];
		*unk = (bitset >> (playerIndex & 31)) & 1;
	}

	*position = g_playerFocusPositions[playerIndex];
	return position;
}

static hook::cdecl_stub<void*(CNetGamePlayer*)> getPlayerPedForNetPlayer([]()
{
#ifdef GTA_FIVE
	return hook::get_call(hook::get_pattern("84 C0 74 1C 48 8B CF E8 ? ? ? ? 48 8B D8", 7));
#elif IS_RDR3
	return hook::get_call(hook::get_pattern("48 8B CD 0F 11 06 48 8B D8 E8", -8));
#endif
});

using namespace DirectX;

float VectorDistance(XMVECTOR a, XMVECTOR b)
{
	XMVECTOR delta = XMVectorSubtract(a, b);
	float dis = XMVectorGetX(XMVector3Length(delta));
	return dis;
}

static int(*g_origUnkPlayerFlags)(CNetGamePlayer*, uint8_t);
static int unkPlayerFlags(CNetGamePlayer* player, uint8_t physicalIndex)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origUnkPlayerFlags(player, physicalIndex);
	}

	uint8_t flagIndex = physicalIndex >> 5;
	int v5 = 1 << (physicalIndex & 0x1F);

	auto& Unk968Bitset = g_player968Bitset[player->physicalPlayerIndex()];
	auto& Unk96CBitset = g_player96CBitset[player->physicalPlayerIndex()];

	if ((v5 & Unk96CBitset[flagIndex]) != 0)
	{
		return 2;
	}

	return (v5 & Unk968Bitset[flagIndex]) != 0;
}

static int (*g_origGetPlayersNearPoint)(rage::Vec3V* point, uint32_t unkIndex, void* outIndex, CNetGamePlayer* outArray[32], bool unkVal, float range, bool sorted);
static int GetPlayersNearPoint(rage::Vec3V* point, uint32_t unkIndex, void* outIndex, CNetGamePlayer* outArray[32], bool unkVal, float range, bool sorted)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_origGetPlayersNearPoint(point, unkIndex, outIndex, outArray, range, range, sorted);
	}

	CNetGamePlayer* tempArray[512];

	int idx = 0;

	auto playerList = g_playerListRemote;
	for (int i = 0; i < g_playerListCountRemote; i++)
	{
		auto player = playerList[i];

		if (getPlayerPedForNetPlayer(player))
		{
			rage::Vec3V vectorPos;

			if (range >= 100000000.0f || VectorDistance(*point, *GetPlayerFocusPosition(&vectorPos, player, nullptr)) < range)
			{
				tempArray[idx] = player;
				idx++;
			}
		}
	}

	if (sorted)
	{
		std::sort(tempArray, tempArray + idx, [point](CNetGamePlayer* a1, CNetGamePlayer* a2)
		{
			rage::Vec3V vectorPos1;
			rage::Vec3V vectorPos2;

			float d1 = VectorDistance(*point, *GetPlayerFocusPosition(&vectorPos1, a1, nullptr));
			float d2 = VectorDistance(*point, *GetPlayerFocusPosition(&vectorPos2, a2, nullptr));

			return (d1 < d2);
		});
	}

	idx = std::min(idx, 32);
	unkIndex = idx;
	std::copy(tempArray, tempArray + idx, outArray);

	return idx;
}

// NetObject Player Acknowledgment
static bool (*g_origNetobjIsPlayerAcknowledged)(rage::netObject*, CNetGamePlayer*);
static bool netObject__IsPlayerAcknowledged(rage::netObject* object, CNetGamePlayer* player)
{
	console::DPrintf("scope", "netObject__IsPlayerAcknowledged %i %i\n", object->GetObjectId(), player->physicalPlayerIndex());

	uint8_t physicalIndex = player->physicalPlayerIndex();
	if (!icgi->OneSyncEnabled || physicalIndex < 32)
	{
		return g_origNetobjIsPlayerAcknowledged(object, player);
	}

	if (physicalIndex == rage::GetLocalPlayer()->physicalPlayerIndex())
	{
		return true;
	}
	return false;
}

static uint32_t (*g_origNetObjectSetPlayerCreationAcked)(rage::netObject*, CNetGamePlayer*, uint8_t);
static uint32_t netObject__setPlayerCreationAcked(rage::netObject* object, CNetGamePlayer* player, uint8_t playerIndex)
{
	console::DPrintf("scope", "setPlayerCreationAcked %i %i %i\n", object->GetObjectId(), player->physicalPlayerIndex(), playerIndex);
	// Allow non-onesync and index 31 to behave as intended
	if (!icgi->OneSyncEnabled || playerIndex < 0x20)
	{
		return g_origNetObjectSetPlayerCreationAcked(object, player, playerIndex);
	}

	// If we are the local player we already acknowledge the creation
	if (playerIndex == rage::GetLocalPlayer()->physicalPlayerIndex())
	{
		// return value isn't used anywhere in onesync logic
		return 1;
	}
	return 0;
}

static uint32_t (*g_origSetPendingRemovalByPlayer)(rage::netObject*, CNetGamePlayer*, uint8_t);
static uint32_t netObject__setPendingRemovalByPlayer(rage::netObject* object, CNetGamePlayer* player, uint8_t newState)
{
	uint8_t physicalIndex = player->physicalPlayerIndex();
	if (!icgi->OneSyncEnabled || physicalIndex < 0x20)
	{
		return g_origSetPendingRemovalByPlayer(object, player, newState);
	}

	return 0;
}

static bool (*g_origIsPendingRemovalByPlayer)(rage::netObject*, CNetGamePlayer*);
static bool netObject__isPendingRemovalByPlayer(rage::netObject* object, CNetGamePlayer* player)
{
	uint8_t physicalIndex = player->physicalPlayerIndex();
	// Allow non-onesync and index 31 to behave as intended
	if (!icgi->OneSyncEnabled || physicalIndex < 0x20)
	{
		return g_origIsPendingRemovalByPlayer(object, player);
	}

	// If we are the local player we don't care about this result ever
	if (physicalIndex == rage::GetLocalPlayer()->physicalPlayerIndex())
	{
		return false;
	}

	return false;
}

static uint32_t (*g_netObject__setScopeState)(rage::netObject*, CNetGamePlayer*);
static uint32_t netObject__setScopeState(rage::netObject* object, CNetGamePlayer* player)
{
	if (!icgi->OneSyncEnabled || player->physicalPlayerIndex() < 0x20)
	{
		return g_netObject__setScopeState(object, player);
	}

	//TODO: Proper handling, the getter is inlined
	return 0;
}

static hook::cdecl_stub<bool(CNetGamePlayer*)> isNetPlayerLocal([]()
{
	return hook::get_pattern("48 8B 81 ? ? ? ? 48 85 C0 74 ? BA");
});

static void (*g_unkRemoteBroadcast)(void*, __int64);
static void unkRemoteBroadcast(void* a1, __int64 a2)
{
	if (!icgi->OneSyncEnabled)
	{
		return g_unkRemoteBroadcast(a1, a2);
	}
}

static void*(*g_sub_142FA454C)(void*, uint32_t*);
static void* sub_142FA454C(void* self, uint32_t* oldBitset)
{
	// a single uint32_t bitset is passed to this function and is used to set player indexes as flags.
	// This causes issues if we want to go above a playerIndex of 32.
	// In all cases the bitset above is only used for this function. So we can allocate our own bitset here
	// and pass that into the original function restoring behaviour
	uint32_t bitset[(kMaxPlayers / 32) + 1] = {};
	return g_sub_142FA454C(self, bitset);
}

static HookFunction hookFunction([]()
{
	// Expand Player Damage Array to support more players
	{
		constexpr size_t kDamageArraySize = sizeof(uint32_t) * 256;
		uint32_t* damageArrayReplacement = (uint32_t*)hook::AllocateStubMemory(kDamageArraySize);
		memset(damageArrayReplacement, 0, kDamageArraySize);

		RelocateRelative((void*)damageArrayReplacement, { 
			{ "48 8D 0D ? ? ? ? 44 21 35", 3 },
			{ "4C 8D 25 ? ? ? ? 41 83 3C B4", 3 },
			{ "48 8D 0D ? ? ? ? 85 DB 74", 3 }, // "48 8D 15 ? ? ? ? 85 FF"
			{ "48 8D 15 ? ? ? ? 8B 0C 82", 3 }
		});

		// Patch damage related comparisions
		PatchValue<uint8_t>({ 
			{"3C ? 73 ? 44 21 35", 1, 0x20, kMaxPlayers + 1},
		    {"80 F9 ? 73 ? 0F B6 D1 48 8D 0D", 2, 0x20, kMaxPlayers + 1 },
		    {"80 BB ? ? ? ? ? 73 ? 40 0F B6 C7", 6, 0x20, kMaxPlayers + 1 },
			//CWeaponDamageEvent::HandleReply
			{"40 80 FF ? 73 ? 40 38 3D", 3, 0x20, kMaxPlayers + 1},
			//CWeaponDamageEvent::_CheckIfAlreadyKilled
			{ "80 3D ? ? ? ? ? 40 8A 68", 6, 0x20, kMaxPlayers + 1 },

		});
	}

	// Expand Player Cache data
	{
		//0x10
		static size_t kCachedPlayerSize = sizeof(void*) * (kMaxPlayers + 1);
		void** cachedPlayerArray = (void**)hook::AllocateStubMemory(kCachedPlayerSize);
		memset(cachedPlayerArray, 0, kCachedPlayerSize);
		RelocateRelative((void*)cachedPlayerArray, {
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 66 83 79", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B F2 BD", 3 },
			{ "BD ? ? ? ? 8B C3 48 8D 0D", 10 },
			{ "0F B6 C3 48 8D 0D ? ? ? ? 48 8B 0C C1", 6 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C ? 48 85 C9 74 ? 8B 41", 3},
			{ "48 8D 0D ? ? ? ? 48 8B 04 C1 8A 40", 3 },
			{ "4C 8D 0D ? ? ? ? 48 63 05 ? ? ? ? 48 8B C8", 3 },
			{ "48 8D 0D ? ? ? ? 88 15 ? ? ? ? 88 15 ? ? ? ? 88 15", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 83 79", 3 },
			{ "48 8D 15 ? ? ? ? 48 8B 14 CA 48 85 D2 74 ? 39 72", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B C3 48 8B 0C D9", 3 }, // 48 8B 0C C1 48 85 C9 74 ? 8A 41 ? C3
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 8A 41 ? EB", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 40 88 79", 3 },
			{ "48 8D 15 ? ? ? ? 88 19", 3 }, // "48 8D 0D ? ? ? ? 88 ?"
			{ "48 8D 3D ? ? ? ? 48 8B 0C DF", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 66 89 ?", 3 },
			{ "48 8D 1D ? ? ? ? 48 8B 1C C3 48 85 DB 74 ? 66 39 73", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 04 C1 40 88 78", 3 }, // "48 8D 0D ? ? ? ? 48 8B 04 C1 88 58"
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 66 3B 59", 3 },
			{ "48 8D 15 ? ? ? ? 84 C9 75", 3 }
		}, 20);

		PatchValue<uint8_t>({
			// player cached getters
			{ "E8 ? ? ? ? 84 C0 74 ? 40 88 3D ? ? ? ? EB", 23, 0x20, kMaxPlayers + 1 },
			//{ "48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ? 3A D8", -24, 0x20, kMaxPlayers + 1 },
			{ "80 F9 ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? E8", 2, 0x20, kMaxPlayers + 1},
			{ "83 F9 ? 7C ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 48 8D 0D ? ? ? ? 48 8B 0C D9", 2, 0x20, kMaxPlayers + 1},
			{ "80 FA ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 8A CB", 2, 0x20, kMaxPlayers + 1},
			{ "80 7B ? ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 43 ? 48 8D 0D", 3, 0x20, kMaxPlayers + 1},
			//{ "80 F9 ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? B9", 2, 0x20, kMaxPlayers + 1},
			{ "80 F9 ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 DB", 2, 0x20, kMaxPlayers + 1},
			{ "80 F9 ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 C3", 2, 0x20, kMaxPlayers + 1},
			{ "80 FB ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 8A CB", 2, 0x20, kMaxPlayers + 1},
			{ "48 85 C0 74 ? 83 61 ? ? B8 ? ? ? ? 66 83 61", -57, 0x20, kMaxPlayers + 1 },
			//{ "80 FB ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 C3 48 8D 0D", 2, 0x20, kMaxPlayers + 1},
			{ "83 F9 ? 7C ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 48 8D 0D ? ? ? ? 48 8B C3", 2, 0x20, kMaxPlayers + 1 },
			{ "83 F8 ? 72 ? C3 CC 0F 48 8B", 2, 0x20, kMaxPlayers + 1 },
			{ "80 FB ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? B0", 2, 0x20, kMaxPlayers + 1},
		});
	}

	// Replace 32-sized bandwidth related array.
	{
		void** bandwidthRelatedArray = (void**)hook::AllocateStubMemory(sizeof(unsigned int) * kMaxPlayers + 2);

		RelocateRelative((void*)bandwidthRelatedArray, {
			{ "48 8D 3D ? ? ? ? B9 ? ? ? ? F3 AB 48 8D 8B", 3 },
			{ "48 8D 15 ? ? ? ? C7 04 82", 3 },
			{ "48 8D 0D ? ? ? ? 89 1C 81", 3 },
			{ "48 8D 05 ? ? ? ? 8B 14 B8 3B EA", 3 },
			{ "48 8D 0D ? ? ? ? 8B 04 81 C3 90 66 40 53", 3 }
		});

		PatchValue<uint32_t>({
			{ "B9 ? ? ? ? F3 AB 48 8D 8B ? ? ? ? 33 D2", 1, 0x20, kMaxPlayers },
		});

		PatchValue<uint8_t>({ 
			//{ "41 0F 47 D5 8A CB", 25, 0x20, kMaxPlayers }
			{ "80 FB ? 0F 82 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24 ? 48 83 C4 ? 41 5F 41 5E 41 5D", 2, 0x20, kMaxPlayers + 1 }
		});
	}

#if 0
	// Replace 32-sized player animation scene array.
	{
		static size_t kSceneArraySize = 3200;
		void** animSceneArray = (void**)hook::AllocateStubMemory(kSceneArraySize * kMaxPlayers + 1);

		RelocateRelative((void*)animSceneArray, {
			{ "48 8D 05 ? ? ? ? 48 03 D8 48 85 DB", 3},
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 47 ? 66 39 01 75 ? 80 79", 3 },
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 41 0F B7 46 ? 66 39 01 75 ? 8A 41 ? 2C ? 3C", 3 },
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 43", 3 },
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 41 0F B7 46 ? 66 39 01 75 ? 8A 41 ? 2C ? 75", 3 },
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 46 ? 66 39 01 75 ? 8A 41", 3 },
			{ "48 8D 05 ? ? ? ? 8B D3 48 03 C8 48 85 C9 74 ? 41 0F B7 46", 3 },
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 47 ? 66 39 01 75 ? 8A 41", 3 },
            { "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 46 ? 66 39 01 75 ? 8B D5", 3 },
			{ "48 8D 35 ? ? ? ? 33 C9 E8", 3 },
			{ "48 8D 1D ? ? ? ? 8B F5", 3 },
			{ "48 8D 15 ? ? ? ? 48 69 C9 ? ? ? ? 44 8B CD", 3 },
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 47 ? 66 39 01 74", 3},
			{ "48 8D 05 ? ? ? ? 48 69 D1 ? ? ? ? 45 33 C0", 3 }
		});

		// Patch 32-bit registers
		PatchValue<uint32_t>({ 
			{ "BD ? ? ? ? 48 8D 35 ? ? ? ? 33 C9", 1, 0x20, kMaxPlayers },
		    { "BD ? ? ? ? 48 8D 1D ? ? ? ? 8B F5", 1, 0x20, kMaxPlayers }
		});

		// Patch 8-bit registers
		PatchValue<uint8_t>({ 
			{ "40 80 FF ? 72 ? 40 8A C5", 3, 0x20, kMaxPlayers + 1},
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 47 ? 66 39 01 75 ? 80 79", 48, 0x20, kMaxPlayers + 1},
			{ "40 80 FF ? 72 ? 40 8A C6 EB ? 83 B8 ? ? ? ? ? 0F 9D C0", 3, 0x20, kMaxPlayers + 1},
			{ "40 80 FF ? 72 ? 40 8A C6 EB ? F6 80", 3, 0x20, kMaxPlayers + 1},
		    { "40 80 FF ? 72 ? 32 C0", 3, 0x20, kMaxPlayers + 1},
		    { "40 80 FF ? 72 ? 40 8A C6 EB ? 83 B8 ? ? ? ? ? 7D", 3, 0x20, kMaxPlayers + 1 },
			{ "48 8D 05 ? ? ? ? 48 03 C8 48 85 C9 74 ? 0F B7 47 ? 66 39 01 75 ? 8A 41", 51, 0x20, kMaxPlayers + 1 },
		    { "80 FB ? 0F 82 ? ? ? ? B0", 2, 0x20, kMaxPlayers + 1 }
		});
	}
#endif
	// Replace 32-sized player arrayhandler array.
#if 0
	{
		void** playerArrayHandler = (void**)hook::AllocateStubMemory(sizeof(void*) * 256);

		RelocateRelative((void*)playerArrayHandler, { 
			{ "48 8D 3D ? ? ? ? BD ? ? ? ? 48 8D 35", 3},
            { "48 8D 3D ? ? ? ? 48 8B 3C C7 48 85 FF 75", 3}, 
			{ "48 8D 1D ? ? ? ? 48 8B 33 48 85 F6 74 ? 48 8B 06", 3 }
		});

		// Patch 8-bit registers
		PatchValue<uint8_t>({ 
			//{ "80 FB ? 72 ? BA ? ? ? ? C7 40", 2, 0x20, kMaxPlayers + 1},
		});
	}
#endif

	// Patch CNetworkDamageTracker
	{
		// 32/31 comparsions
		PatchValue<uint8_t>({
			{"80 7A ? ? 0F 28 F2 48 8B F2", 3, 0x20, kMaxPlayers + 1},
			{"80 7A ? ? 48 8B F9 72", 3, 0x20, kMaxPlayers + 1}
		});
	}

	// Patch netObject to account for >32 players
	{
		// 32/31 32bit comparsions
		PatchValue<uint32_t>({ 
			// rage::netObject::DependencyThreadUpdate
			{ "41 BF ? ? ? ? 8A 8C 10", 2, 0x20, kMaxPlayers + 1}
		});

		PatchValue<uint8_t>({
			// rage::netObject::CanCreateWithNoGameObject
			{ "80 79 ? ? 73 ? 32 C0", 3, 0x20, kMaxPlayers + 1 },
			// rage::netObject::CanPassControl
			{ "3C ? 73 ? 3A 46", 1, 0x20, kMaxPlayers + 1},
			// rage::netObject::SetOwner
			{ "80 F9 ? 73 ? E8 ? ? ? ? 48 8B D8 EB", 2, 0x20, kMaxPlayers },
			// rage::netObject::IsPendingOwnerChange
			{ "80 79 ? ? 0F 92 C0 C3 48 8B 91", 3, 0x20,  kMaxPlayers + 1 },
			// rage::netObject::IsPlayerAcknowledged
			//{ "48 83 C4 ? 5F C3 CC 33 C0 8B D0", 87 - 4, 0x20, kMaxPlayers + 1 },

			// rage::netObject::CanTargetPlayer
			///{ "83 FA ? 77 ? 8B C2 44 8B C2", 2 , 0x20, kMaxPlayers + 1},

			{ "40 80 FE ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 49 8B F7", 3, 0x20, kMaxPlayers + 1 }
		});
	}

	// Adjust bit logic to support 127/128
	{
		std::initializer_list<PatternPair> list = {
			// CNetObjPlayer Update
			{ "41 83 E6 ? 45 3B F7", 3 },
		};

		for (auto& entry : list)
		{
			auto location = hook::pattern(entry.pattern).count(1).get(0).get<uint8_t>(entry.offset);
			auto origVal = *location;
			trace("orig bitlogic value %i\n", origVal);
			assert(origVal == 0x1f);
			hook::put<uint8_t>(location, 0xFF);
		}
	}

	// Replace 32/31 comparisions
	{
		std::initializer_list<PatternClampPair> list = {
			// unkAreThereTooManyAcksForThisPlayer
			{ "80 7A ? ? 41 8A F8 48 8B DA", 3, false },
			//CNetGamePlayer::IsPhysical
			{ "80 79 ? ? 0F 92 C0 C3 48 89 5C 24", 3, false },
			//rage::netPlayer::IsPhysical
			{ "80 7B ? ? 73 ? B2", 3, false },

			// unk local player related
			{ "80 7D ? ? 48 8B F8 72 ? 32 C0", 3, false },
			
			// CPhysical::_CorrectSyncedPosition
			{ "40 80 FE ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84", 3, false },
			// getNetPlayerFromGamerHandleIfInSession
			{ "48 8B C8 48 8B D6 E8 ? ? ? ? 84 C0 75 ? FE C3", 19, false },
			
			// Ped Group
			//{ "80 79 ? ? 73 ? 0F B6 41", 3, false },
			//{ "83 FB ? 72 ? EB ? E8", 2, false },
			// CPedCreateGroupNode related
			//{ "E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? 8B F8 83 F8", 17, true },

			// Related to CNetworkPopulationResetMgr
			{ "40 80 FF ? 73 ? 48 8B 4E", 3, false},
			{ "80 FB ? 72 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24", 2, false},
			{ "80 FB ? 72 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 41 5F 41 5E 41 5D 41 5C 5F C3 83 FA", 2, false },


			//{ "8A DA 8B F1 80 FA", 18, false },

			// Ped Combat related
			{ "80 FA ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 C3 BA", 2, false },
			{ "80 FA ? 0F 83 ? ? ? ? 48 8B 05", 2, false },
			//{ "41 80 FE ? 73 ? 45 33 C0", 1, false },
			{ "80 3B ? 73 ? 48 8B CE", 2, false },

			// CNetObjProximityMigrateable::_getRelevancePlayers
			{ "40 80 FF ? 0F 82 ? ? ? ? 0F 28 74 24 ? 4C 8D 5C 24 ? 49 8B 5B ? 48 8B C6", 3, false },

			//CNetObjGame::CanClone
			{ "80 7A ? ? 49 8B F8 48 8B DA 48 8B F1 72", 3, false},

			// getNetworkEntityOwner
			{ "80 F9 ? 72 ? 33 C0 C3 E9 ? ? ? ? 48 89 5C 24", 2, false },

			//{ "80 F9 ? 72 ? 33 C0 C3 E9 ? ? ? ? 8A 4A", 1, false },

			//FindNetworkPlayerPed
			{ "83 F9 ? 73 ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8", 2, false },

			//rage::netObjectIDMgr::TryToAllocateInitialObjectIDs, removes need for ScMultiplayerImpl size patches
			//Also removes the need to patch session entering logic.
			{ "80 79 ? ? 0F 83 ? ? ? ? 44 38 7B", 3, false },
			{ "80 79 ? ? 72 ? B0", 3, false },
			{ "80 7F ? ? 72 ? 41 B9 ? ? ? ? C7 44 24", 3 , false },
			{ "80 FB ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 8A CB", 2, false },

			// Native Fixes
			{ "83 FB ? 73 ? 45 33 C0", 2, false }, // 0x862C5040F4888741
			{ "83 F9 ? 0F 83 ? ? ? ? B2", 2, false }, // 0x236321F1178A5446
			{ "83 F9 ? 73 ? 80 3D", 2, false }, // 0x93DC1BE4E1ABE9D1


			// Experimental. May need additional patches
			/* 
			{ "80 FB ? 72 ? 41 B9 ? ? ? ? C7 40 ? ? ? ? ? 41 B8 ? ? ? ? 48 8D 0D ? ? ? ? 8B D6", 2, false },
			{ "83 E0 ? 8A C8 48 C1 EA ? D3 E3 40 0F B6 C5 F7 D8 33 44 96 ? 23 D8 31 5C 96 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 CC 48 8B C4", -60, false},
			{ "80 7A ? ? 48 8B DA 48 8B F9 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 4B ? 8B C1 8B D1 48 C1 E8 ? 83 E2 ? 8B 44 87 ? 0F A3 D0 0F 92 C0 48 8B 5C 24 ? 48 83 C4 ? 5F C3 90 33 C0 8B D0 48 83 C1 ? 39 01 75 ? 48 FF C2 48 83 C1 ? 48 83 FA ? 7C ? C3 B0 ? C3 90", 3, false },
			{ "80 7A ? ? 48 8B DA 48 8B F9 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 4B ? 8B C1 8B D1 48 C1 E8 ? 83 E2 ? 8B 44 87 ? 0F A3 D0 0F 92 C0 48 8B 5C 24 ? 48 83 C4 ? 5F C3 CC", 3, false },
			{ "80 7A ? ? 48 8B DA 48 8B F9 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 4B ? 8B C1 8B D1 48 C1 E8 ? 83 E2 ? 8B 44 87 ? 0F A3 D0 0F 92 C0 48 8B 5C 24 ? 48 83 C4 ? 5F C3 90 33 C0 8B D0 48 83 C1 ? 39 01 75 ? 48 FF C2 48 83 C1 ? 48 83 FA ? 7C ? C3 B0 ? C3 CC", 3, false },
			{ "80 7A ? ? 41 8A E8 48 8B FA 48 8B F1 BB ? ? ? ? 72 ? 41 B9 ? ? ? ? C7 40 ? ? ? ? ? 41 B8 ? ? ? ? 48 8D 0D ? ? ? ? 8B D3 E8 ? ? ? ? 84 C0 74 ? 0F B6 47 ? 8B D0 83 E0 ? 8A C8 48 C1 EA ? D3 E3 40 0F B6 C5 F7 D8 33 44 96 ? 23 D8 31 5C 96 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 CC 39 91", 3, false },
			{ "80 7A ? ? 41 8A E8 48 8B FA 48 8B F1 BB ? ? ? ? 72 ? 41 B9 ? ? ? ? C7 40 ? ? ? ? ? 41 B8 ? ? ? ? 48 8D 0D ? ? ? ? 8B D3 E8 ? ? ? ? 84 C0 74 ? 0F B6 47 ? 8B D0 83 E0 ? 8A C8 48 C1 EA ? D3 E3 40 0F B6 C5 F7 D8 33 44 96 ? 23 D8 31 5C 96 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 CC 88 51", 3, false },
			*/
			//{ "80 7A ? ? 41 8A E8 48 8B FA 48 8B F1 BB ? ? ? ? 72 ? 41 B9 ? ? ? ? C7 40 ? ? ? ? ? 41 B8 ? ? ? ? 48 8D 0D ? ? ? ? 8B D3 E8 ? ? ? ? 84 C0 74 ? 0F B6 47 ? 8B D0 83 E0 ? 8A C8 48 C1 EA ? D3 E3 40 0F B6 C5 F7 D8 33 44 96 ? 23 D8 31 5C 96 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 CC 48 8B C4", 3, false },



			// TMP Patterns. TODO: Improve and support older game builds
			{ "3C ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84", 1, false },
			{ "80 7F ? ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 45 84 FF 74 ? 48 8B 05 ? ? ? ? 4C 8D 4C 24 ? 44 8B C6 49 8B D6 48 8B 88 ? ? ? ? 48 89 4C 24 ? 48 8B CF E8 ? ? ? ? EB ? 8A 57 ? 44 8B CE 4D 8B C6 48 8B CD E8 ? ? ? ? 84 C0 75 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? FE C3 80 FB ? 0F 82 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 83 C4 ? 41 5F 41 5E 41 5C 5F 5E C3 CC 7C", 3, false },
			{ "40 80 FF ? 73 ? 48 8B 43 ? 40 0F B6 CF 48 8B 7C C8 ? 48 85 FF 74 ? 48 8B CF E8 ? ? ? ? 84 C0 74 ? 48 8B 1B 48 8B CF E8 ? ? ? ? 8B D0 44 8B CE 4C 8B C5 48 8B CB E8 ? ? ? ? EB ? 32 C0 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 90 40 33 48", 3, false },
		};

		for (auto& entry : list)
		{
			auto location = hook::pattern(entry.pattern).count(1).get(0).get<uint8_t>(entry.offset);
			auto origVal= *location;
			trace("orig value %i %i\n", origVal, (uint32_t)origVal);
			assert(origVal == (entry.clamp ? 31 : 32));
			hook::put<uint8_t>(location, (entry.clamp ? kMaxPlayers : kMaxPlayers + 1));
		}
	}

	// Replace 32 array iterations
#if 1
	{
		std::initializer_list<PatternClampPair> list = {
			// Player Cache Data Initalization
			{ "44 8D 41 ? 33 D2 4C 8D 0D", 3, true },
		};

		for (auto& entry : list)
		{
			auto location = hook::pattern(entry.pattern).count(1).get(0).get<uint8_t>(entry.offset);
			auto origVal = *location;
			trace("orig value %i %i\n", origVal, (uint32_t)origVal);
			assert(origVal == 32 || origVal == 31);
			hook::put<uint8_t>(location, origVal == 31 ? kMaxPlayers - 1 : kMaxPlayers);
		}
	}
#endif

	// hardcoded 32/128 array sizes in CNetObjProximityMigrateable::_passOutOfScope
	{
		auto location = hook::get_pattern<char>("41 B0 ? 41 8A D0 41 FF 51 ? 4C 8D 05 ? ? ? ? 48 8B CF", -70);

		// 0x20: scratch space
		// kMaxPlayers + 1 * 8: kMaxPlayers players, ptr size
		// kMaxPlayers + 1 * 4: kMaxPlayers players, int size
		auto ptrsBase = 0x20;
		auto rawStackSize = (ptrsBase + ((kMaxPlayers + 1) * 8) + ((kMaxPlayers + 1) * 4));
		auto intsBase = ptrsBase + ((kMaxPlayers + 1) * 8);
		// Ensure that we are aligned to 16
		auto stackSize = (rawStackSize + 15) & ~0xF;

		// stack frame ENTER
		hook::put<uint32_t>(location + 0x18, stackSize);
		// stack frame LEAVE
		hook::put<uint32_t>(location + 0x12B, stackSize);
		// var: rsp + 1A8
		hook::put<uint32_t>(location + 0xDE, intsBase);
	}

	// Same for CNetObjPedBase::_passOutOfScope
	{
		auto location = hook::get_pattern<char>("41 B0 ? 41 8A D0 41 FF 51 ? 4C 8D 05 ? ? ? ? 49 8B CE", -83);

		// 0x20: scratch space
		// kMaxPlayers + 1 * 8: kMaxPlayers players, ptr size
		// kMaxPlayers + 1 * 4: kMaxPlayers players, int size
		auto ptrsBase = 0x20;
		auto rawStackSize = (ptrsBase + ((kMaxPlayers + 1) * 8) + ((kMaxPlayers + 1) * 4));
		auto intsBase = ptrsBase + (kMaxPlayers + 1 * 8);
		// Ensure that we are aligned to 16
		auto stackSize = (rawStackSize + 15) & ~0xF;

		// stack frame ENTER
		hook::put<uint32_t>(location + 0x18, stackSize);
		// stack frame LEAVE
		hook::put<uint32_t>(location + 0x26B, stackSize);
		// var: rsp + 1A8
		hook::put<uint32_t>(location + 0x211, intsBase);
	}

	hook::call(hook::get_pattern("E8 ? ? ? ? 45 33 C9 84 C0 41 0F 94 C5"), Return<true, true>);
	hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? E8 ? ? ? ? 41 F6 46"), Return<true, true>);
	hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 8A 5C 24"), Return<true, true>);

	// Skip unused host kick related >32-unsafe arrays in onesync
	//hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 75 ? 8B 05 ? ? ? ? 33 C9 89 44 24"), Return<true, false>);

	// Rewrite functions to account for extended players
	MH_Initialize();

	// Don't broadcast script info in OneSync
	MH_CreateHook(hook::get_pattern("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 48 8B EC 48 81 EC ? ? ? ? 48 83 79"), unkRemoteBroadcast, (void**)&g_unkRemoteBroadcast);

	// Update Player Focus Positions to support 128 players.
	MH_CreateHook(hook::get_pattern("0F A3 D0 0F 92 C0 88 06", -0x76), GetPlayerFocusPosition, (void**)&g_origGetNetPlayerRelevancePosition);
	MH_CreateHook(hook::get_call(hook::get_pattern("8B 84 93 ? ? ? ? 0F AB C8 89 84 93 ? ? ? ? 48 8B D7", 28)), UpdatePlayerFocusPosition, (void**)&g_origUpdatePlayerFocusPosition);

	// Adapt network entity bitset logic to ensure unexpected behaviour does not occur
	// creation
	MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? 40 8A FB 84 C0 74")), netObject__IsPlayerAcknowledged, (void**)&g_origNetobjIsPlayerAcknowledged);
	MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? EB ? 47 85 0C 86")), netObject__setPlayerCreationAcked, (void**)&g_origNetObjectSetPlayerCreationAcked);
	// removal?
	MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? 33 FF 84 C0 75 ? 8A 4B")), netObject__isPendingRemovalByPlayer, (void**)&g_origIsPendingRemovalByPlayer);
	MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? EB ? 45 84 FF 74 ? 48 8D 4C 24")), netObject__setPendingRemovalByPlayer, (void**)&g_origSetPendingRemovalByPlayer);
	// scope
	MH_CreateHook(hook::get_pattern("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC ? 8A 5A"), netObject__setScopeState, (void**)&g_netObject__setScopeState);

	MH_CreateHook(hook::get_pattern("4D 8B 04 C0 4E 39 3C 01 75 ? 33 C0 89 02", -0x39), sub_142FA454C, (void**)&g_sub_142FA454C);
	MH_CreateHook(hook::get_pattern("33 DB 0F 29 70 D8 49 8B F9 4D 8B F0", -0x1B), GetPlayersNearPoint, (void**)&g_origGetPlayersNearPoint);
	MH_EnableHook(MH_ALL_HOOKS);
});

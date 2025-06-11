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
static const uint16_t kObjectId = std::numeric_limits<uint16_t>::max();

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

// Expanded bitsets to handle increased players
std::unordered_map<uint8_t, std::array<uint32_t, 4>> g_player96CBitset;
std::unordered_map<uint8_t, std::array<uint32_t, 4>> g_player968Bitset;

//std::array<std::bitset<128>, 128> g_netPlayerVisiblePlayers;
uint32_t g_playerFocusPositionUpdateBitset[4];
uint32_t g_netPlayerVisiblePlayers[kMaxPlayers][4];
uint32_t g_netPlayerCreationAcked[4];

// Object player related bitsets that aren't >32 safe
std::unordered_map<uint16_t, std::array<uint32_t, 4>> g_objCreationAckedPlayers;
std::unordered_map<uint8_t, rage::Vec3V> g_playerFocusPositions;

MAKE_HOOK(seamlessnetcheck, void*, (void* a1), (a1));
MAKE_HOOK(UpdateObjectOnAllPlayers, int, (void* a1, void* a2, void* a3, void* a4, char a5), (a1, a2, a3, a4, a5));
MAKE_HOOK(sub_140EB7EFC, __int64, (void* a1), (a1));
MAKE_HOOK(sub_140867C30, __int64, (void* a1, char a2), (a1, a2));
MAKE_HOOK_LOG(getPhysicalIndex, uint8_t, (void* a1), (a1));
MAKE_HOOK(sub_14259BA94, void, (void* a1, void* a2), (a1, a2));
extern ICoreGameInit* icgi;
extern CNetGamePlayer* g_players[256];
extern CNetGamePlayer* g_playerListRemote[256];
extern int g_playerListCountRemote;

template<bool Onesync, bool Legacy>
static bool Return()
{
	if (!Onesync && Legacy)
	{
		trace("called fuckass function\n ");
		return 0;
	}
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
	trace("netObject__IsPlayerAcknowledged %i %i\n", object->GetObjectId(), player->physicalPlayerIndex());

	{
		return g_origNetobjIsPlayerAcknowledged(object, player);
	}

	uint8_t physicalIndex = player->physicalPlayerIndex();
	if (physicalIndex >= kMaxPlayers + 1)
	{
		return false;
	}

	int index = physicalIndex >> 5;
	uint32_t objectId = object->GetObjectId();
	uint32_t creationAckedPlayer = g_objCreationAckedPlayers[objectId][index];
	bool result = (creationAckedPlayer >> (physicalIndex & 0x1F)) & 1;

	return result;	
}

static uint32_t (*g_origNetObjectSetPlayerCreationAcked)(rage::netObject*, CNetGamePlayer*, uint8_t);
static uint32_t netObject__setPlayerCreationAcked(rage::netObject* object, CNetGamePlayer* player, uint8_t playerIndex)
{
	trace("setPlayerCreationAcked %i %i %i\n", object->GetObjectId(), player->physicalPlayerIndex(), playerIndex);

	if (playerIndex < 0x20)
	{
		return g_origNetObjectSetPlayerCreationAcked(object, player, playerIndex);
	}

	if (playerIndex >= kMaxPlayers + 1)
	{
		return false;
	}

	int index = player->physicalPlayerIndex() >> 5;
	int bit = player->physicalPlayerIndex() & 0x1F;
	uint32_t objectId = object->GetObjectId();

	uint32_t result = g_objCreationAckedPlayers[objectId][index] ^ static_cast<uint32_t>(-playerIndex);
	g_objCreationAckedPlayers[objectId][index] ^= result & (1u << bit);
	return result;
}

static hook::cdecl_stub<bool(CNetGamePlayer*)> isNetPlayerLocal([]()
{
	return hook::get_pattern("48 8B 81 ? ? ? ? 48 85 C0 74 ? BA");
});

static void (*g_unkRemoteBroadcast)(void*, __int64);
static void unkRemoteBroadcast(void* a1, __int64 a2)
{
	trace("unkrEMOTEBROADCAST\n");
	if (!icgi->OneSyncEnabled)
	{
		return g_unkRemoteBroadcast(a1, a2);
	}
}

static void* (*g_sub_142FB17F8)(void*);
static void* sub_142FB17F8(void* a1)
{
	if (!a1 || a1 == (void*)-1)
	{
		__debugbreak();
		return nullptr;
	}

	if ((uintptr_t)a1 > (uintptr_t)0x00007FFFFFFFFFFF)
	{
		__debugbreak();
		return nullptr;
	}

	return g_sub_142FB17F8(a1);
}

static void* (*g_sub_142FB5F0C)(void*);
static void* sub_142FB5F0C(void* a1)
{
	if (!a1 || a1 == (void*)-1)
	{
		__debugbreak();
		return nullptr;
	}

	if ((uintptr_t)a1 > (uintptr_t)0x00007FFFFFFFFFFF)
	{
		__debugbreak();
		return nullptr;
	}

	return g_sub_142FB5F0C(a1);
}

/*
NET/LOG [2165351663:764] 3099323466
NET/LOG [2165351663:751] 647910438
NET/LOG [2165351663:764] 3099323466
NET/LOG [2165351663:751] 647910438
*/

// Replaced by properly patching id allocation.
#if 0
static void* g_scMultiplayerImpl = nullptr;
static size_t g_scMultiplayerImplCtorSize = 0;

static bool (*scMultiplayerComparsion)(void*, void*);
static void* (*g_origScMultiplayerImplCtor)(void*, void*);
static void* ScMultiplayerImplCtor(void* self, void* a2)
{
	void* data = g_origScMultiplayerImplCtor(self, a2);
	g_scMultiplayerImpl = data;
	memset((void**)data + g_scMultiplayerImplCtorSize, 0, (sizeof(void*) * kMaxPlayers + 1));
	return data;
}
#endif

#if 1
static HookFunction hookFunction([]()
{
	if (!xbr::IsGameBuildOrGreater<1491>())
	{
		return;
	}

	// Expand Player Damage Array to support more players
	{
		constexpr size_t kDamageArraySize = sizeof(uint32_t) * (kMaxPlayers + 1);
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
		static size_t kCachedPlayerSize = 0x10 * (kMaxPlayers + 1);
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
			//{ "41 BF ? ? ? ? 8A 8C 10", 2, 0x20, kMaxPlayers + 1}
		});

		PatchValue<uint8_t>({
			// rage::netObject::CanCreateWithNoGameObject
			{ "80 79 ? ? 73 ? 32 C0", 3, 0x20, kMaxPlayers + 1 },
			// rage::netObject::CanPassControl
			{ "3C ? 73 ? 3A 46", 1, 0x20, kMaxPlayers + 1},
			// rage::netObject::SetOwner
			{ "80 F9 ? 73 ? E8 ? ? ? ? 48 8B D8 EB", 2, 0x20,  kMaxPlayers + 1},
		});
	}

#if 0
	// Fix netObject::_isObjectSyncedWithPlayers
	{
		auto location = hook::get_call(hook::get_pattern("E8 ? ? ? ? 3A C3 74 ? B3 ? 8A C3 48 83 C4 ? 5B C3 40 53 48 83 EC ? 48 8B 01"));
		
		/*
		.text:0000000142C0C1F5 48 8B 3D 54 26 F0 02                                         mov     rdi, cs:rage__netInterface__m_PlayerMgr
		.text:0000000142C0C1FC 84 C0                                                        test    al, al
		.text:0000000142C0C1FE 74 08                                                        jz      short loc_142C0C208
		.text:0000000142C0C200 8B AF 9C 02 00 00                                            mov     ebp, [rdi+29Ch]
		.text:0000000142C0C206 EB 02                                                        jmp     short loc_142C0C20A
		*/
		static struct : jitasm::Frontend
		{
			intptr_t retnFail = 0;
			intptr_t retnSuccess = 0;

			void Init(const intptr_t success, const intptr_t fail)
			{
				this->retnSuccess = success;
				this->retnFail = fail;
			}

			virtual void InternalMain() override
			{
				mov(rdi, reinterpret_cast<uintptr_t>(g_playerListRemote));

				test(al, al);
				jz("Fail");

				mov(rax, reinterpret_cast<uintptr_t>(&g_playerListCountRemote));
				mov(ebp, dword_ptr[rax]);

				mov(r11, retnSuccess);
				jmp(r11);

				L("Fail");
				mov(r11, retnFail);
				jmp(r11);
			};
		} patchStub;

		const uintptr_t patch = (uintptr_t)location + 45;

		const uintptr_t retnFail = patch + 19; //142C0C208
		const uintptr_t retnSuccess = retnFail + 2; // 0x142C0C20A

		assert((uintptr_t)0x142C0C208 == retnFail);
		assert((uintptr_t)0x142C0C20A == retnSuccess);

		patchStub.Init(retnSuccess, retnFail);

		hook::nop(patch, 19);
		hook::nop(hook::get_pattern("48 81 C7 ? ? ? ? EB ? 33 FF 33 F6 85 ED 74 ? 48 8B 1F 49 8B CE 48 8B D3 E8 ? ? ? ? 84 C0 74 ? 49 8B 06 49 8B CE 0F B6 5B ? FF 50 ? 48 8B C8 8B D3 E8 ? ? ? ? 84 C0 74 ? FF C6 48 83 C7 ? 3B F5 72 ? B0 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24 ? 48 83 C4 ? 41 5E C3 32 C0 EB ? 90"), 6);
		hook::jump_reg<5>(patch, patchStub.GetCode());
	}
#endif

	// Fix StartSynchronising by using our own playerList
#if 1
	{
		auto location = hook::get_pattern("8A 15 ? ? ? ? 44 8A F8", 9);
		/*
		.text:0000000142C1D48D 48 8B 1D BC 13 EF 02                                            mov     rbx, cs:rage__netInterface__m_PlayerMgr
		.text:0000000142C1D494 84 D2                                                           test    dl, dl
		.text:0000000142C1D496 74 08                                                           jz      short loc_142C1D4A0
		.text:0000000142C1D498 8B 8B 9C 02 00 00                                               mov     ecx, [rbx+29Ch]
		.text:0000000142C1D49E EB 02                                                           jmp     short loc_142C1D4A2
		.text:0000000142C1D4A0                                                 ; ---------------------------------------------------------------------------
		.text:0000000142C1D4A0
		.text:0000000142C1D4A0                                                 loc_142C1D4A0:                          ; CODE XREF: rage__netObject__StartSynchronising+14A↑j
		.text:0000000142C1D4A0 33 C9                                                           xor     ecx, ecx
		.text:0000000142C1D4A2
		.text:0000000142C1D4A2                                                 loc_142C1D4A2:                          ; CODE XREF: rage__netObject__StartSynchronising+152↑j
		.text:0000000142C1D4A2 84 D2                                                           test    dl, dl
		.text:0000000142C1D4A4 74 09                                                           jz      short loc_142C1D4AF
		.text:0000000142C1D4A6 48 81 C3 98 07 00 00                                            add     rbx, 798h

		*/
		static struct : jitasm::Frontend
		{
			intptr_t retnFail = 0;
			intptr_t retnSuccess = 0;

			void Init(const intptr_t success, const intptr_t fail)
			{
				this->retnSuccess = success;
				this->retnFail = fail;
			}

			virtual void InternalMain() override
			{
				mov(rbx, reinterpret_cast<uintptr_t>(g_playerListRemote));

				test(dl, dl);
				jz("Fail");

				mov(r14, reinterpret_cast<uintptr_t>(&g_playerListCountRemote));
				mov(ecx, dword_ptr[r14]);

				mov(r11, retnSuccess);
				jmp(r11);

				L("Fail");
				mov(r11, retnFail);
				jmp(r11);
			};
		} patchStub;

		const uintptr_t retnFail = (uintptr_t)location + 19;
		const uintptr_t retnSuccess = retnFail + 2;

		//TMP ASSERTS
		assert(0x142C1D4A2 == retnSuccess);
		assert(0x142C1D4A0 == retnFail);

		patchStub.Init(retnSuccess, retnFail);

		hook::nop(location, 19);
		hook::jump_reg<5>(location, patchStub.GetCode());

		//Remove +add
		hook::nop(hook::get_pattern("48 81 C3 ? ? ? ? EB ? 33 DB 85 C9"), 7);

	}
#endif

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
			// rage::netObject::IsPendingOwnerChange
			{ "80 79 ? ? 0F 92 C0 C3 48 8B 91", 3, false },
			// 
			{ "80 7A ? ? 41 8A F8 48 8B DA", 3, false },
			//CNetGamePlayer::IsPhysical
			{ "80 79 ? ? 0F 92 C0 C3 48 89 5C 24", 3, false },
			//rage::netPlayer::IsPhysical
			{ "80 7B ? ? 73 ? B2", 3, false },
			
			// CPhysical::_CorrectSyncedPosition
			{ "40 80 FE ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84", 3, false },
			// getNetPlayerFromGamerHandleIfInSession
			{ "48 8B C8 48 8B D6 E8 ? ? ? ? 84 C0 75 ? FE C3", 19, false },
			
			// Ped Group
			//{ "80 79 ? ? 73 ? 0F B6 41", 3, false },
			//{ "83 FB ? 72 ? EB ? E8", 2, false },
			// CPedCreateGroupNode related
			//{ "E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? 8B F8 83 F8", 17, true },

			{ "40 80 FF ? 73 ? 48 8B 4E", 3, false},
			{ "80 FB ? 72 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24", 2, false},

#if 0
			//rage::rlSession::Host, TODO: This may not be needed.
			{ "83 F8 ? 7E ? BB ? ? ? ? B8", 2, false },
			{ "80 78 ? ? 72 ? 8D 56", 3, false },
#endif

			//{ "8A DA 8B F1 80 FA", 18, false },

			// Ped Combat related
			{ "80 FA ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 0F B6 C3 BA", 2, false },
			{ "80 FA ? 0F 83 ? ? ? ? 48 8B 05", 2, false },


			// CNetObjProximityMigrateable::_getRelevancePlayers
			{ "40 80 FF ? 0F 82 ? ? ? ? 0F 28 74 24 ? 4C 8D 5C 24 ? 49 8B 5B ? 48 8B C6", 3, false },

			//CNetObjGame::CanClone
			{ "80 7A ? ? 49 8B F8 48 8B DA 48 8B F1 72", 3, false},

			// rlSession::OnUpdate
			//{ "BD ? ? ? ? 49 8B 3E 48 85 FF 74", 1, false},

			//FindNetworkPlayerPed
			{ "83 F9 ? 73 ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8", 2, false },

			//rage::netObjectIDMgr::TryToAllocateInitialObjectIDs, removes need for ScMultiplayerImpl size patches
			//Also removes the need to patch session entering logic.
			{ "80 79 ? ? 0F 83 ? ? ? ? 44 38 7B", 3, false },
			{ "80 79 ? ? 72 ? B0", 3, false },
			{ "80 7F ? ? 72 ? 41 B9 ? ? ? ? C7 44 24", 3 , false },
			{ "80 FB ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 8A CB", 2, false },

			// session related slot
			// TODO: Verify if still needed with the following patches
#if 0
			{ "40 80 FF ? 72 ? 40 B7 ? 48 8B 41", 3, false },
			{ "40 80 FF ? 72 ? 40 B7 ? 48 8D 0D", 3, false },
			{ "80 79 ? ? 72 ? B2", 3, false },
			{ "40 80 FF ? 72 ? BA ? ? ? ? 48 8B CB", 3, false },
			{ "41 80 FF ? 72 ? 40 84 FF", 3, false },
			{ "40 80 FE ? 72 ? 40 84 FF", 3, false },
			{ "40 80 FF ? 72 ? 48 8B 5C 24", 3, false },
			//{ "80 FB ? 72 ? 33 C0 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4", 2, false },
			//{ "80 FB ? 72 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24", 2, false },
#endif
			// Native Fixes
			{ "83 FB ? 73 ? 45 33 C0", 2, false }, // 0x862C5040F4888741
			{ "83 F9 ? 0F 83 ? ? ? ? B2", 2, false }, // 0x236321F1178A5446


			// TMP Patterns. TODO: Improve and support older game builds

			{ "3C ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84", 1, false },

			{ "80 7F ? ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 45 84 FF 74 ? 48 8B 05 ? ? ? ? 4C 8D 4C 24 ? 44 8B C6 49 8B D6 48 8B 88 ? ? ? ? 48 89 4C 24 ? 48 8B CF E8 ? ? ? ? EB ? 8A 57 ? 44 8B CE 4D 8B C6 48 8B CD E8 ? ? ? ? 84 C0 75 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? FE C3 80 FB ? 0F 82 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 83 C4 ? 41 5F 41 5E 41 5C 5F 5E C3 CC 7C", 3, false },
			{ "40 80 FF ? 73 ? 48 8B 43 ? 40 0F B6 CF 48 8B 7C C8 ? 48 85 FF 74 ? 48 8B CF E8 ? ? ? ? 84 C0 74 ? 48 8B 1B 48 8B CF E8 ? ? ? ? 8B D0 44 8B CE 4C 8B C5 48 8B CB E8 ? ? ? ? EB ? 32 C0 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 90 40 33 48", 3, false },
#if 0
			//rage::rlSession::Join
			{ "83 FB ? 76 ? BB ? ? ? ? C7 44 24 ? ? ? ? ? 44 8B CB 48 8D 0D ? ? ? ? BA ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? B8 ? ? ? ? E9 ? ? ? ? 44 8A E7", 2, false },
			{ "83 FB ? 76 ? BB ? ? ? ? C7 44 24 ? ? ? ? ? 44 8B CB 48 8D 0D ? ? ? ? BA ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? B8 ? ? ? ? E9 ? ? ? ? 8B C3", 2, true },
#endif
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

	// hardcoded 32/128 array sizes in CNetObjProximityMigrateable::_passOutOfScope
	{
		auto location = hook::get_pattern<char>("41 B0 ? 41 8A D0 41 FF 51 ? 4C 8D 05 ? ? ? ? 48 8B CF", -70);

		// 0x20: scratch space
		// kMaxPlayers + 1 * 8: kMaxPlayers players, ptr size
		// kMaxPlayers + 1 * 4: kMaxPlayers players, int size
		auto stackSize = (0x20 + (kMaxPlayers + 1 * 8) + (kMaxPlayers + 1 * 4));
		auto ptrsBase = 0x20;
		auto intsBase = ptrsBase + (kMaxPlayers + 1 * 8);

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
		auto stackSize = (0x20 + (kMaxPlayers + 1 * 8) + (kMaxPlayers + 1 * 4));
		auto ptrsBase = 0x20;
		auto intsBase = ptrsBase + (kMaxPlayers + 1 * 8);

		// stack frame ENTER
		hook::put<uint32_t>(location + 0x18, stackSize);
		// stack frame LEAVE
		hook::put<uint32_t>(location + 0x26B, stackSize);
		// var: rsp + 1A8
		hook::put<uint32_t>(location + 0x211, intsBase);
	}

	// Skip unused host kick related >32-unsafe arrays in onesync
	//hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 75 ? 8B 05 ? ? ? ? 33 C9 89 44 24"), Return<true, false>);

	// Rewrite functions to account for extended players
	MH_Initialize();

	// Remove 32 check for netObject targetting check
	hook::call(hook::get_pattern("E8 ? ? ? ? 45 33 C9 84 C0 41 0F 94 C5"), Return<true, true>);
	hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? E8 ? ? ? ? 41 F6 46"), Return<true, true>);
	hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 8A 5C 24"), Return<true, true>);
	
	// Don't broadcast script info in OneSync
	MH_CreateHook(hook::get_pattern("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 48 8B EC 48 81 EC ? ? ? ? 48 83 79"), unkRemoteBroadcast, (void**)&g_unkRemoteBroadcast);

	// Update Player Focus Positions to support 128 players.
	MH_CreateHook(hook::get_pattern("0F A3 D0 0F 92 C0 88 06", -0x76), GetPlayerFocusPosition, (void**)&g_origGetNetPlayerRelevancePosition);
	MH_CreateHook(hook::get_call(hook::get_pattern("8B 84 93 ? ? ? ? 0F AB C8 89 84 93 ? ? ? ? 48 8B D7", 28)), UpdatePlayerFocusPosition, (void**)&g_origUpdatePlayerFocusPosition);

	// netObj player acknowledge bitset. Capped to 32 bits. Need 128
	// Breaks badly. Not sure if we need to patch this either since 31 is almost always passed to it
	//MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? 40 8A FB 84 C0 74")), netObject__IsPlayerAcknowledged, (void**)&g_origNetobjIsPlayerAcknowledged);
	//MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? EB ? 47 85 0C 86")), netObject__setPlayerCreationAcked, (void**)&g_origNetObjectSetPlayerCreationAcked);
	MH_CreateHook(hook::get_pattern("40 53 48 83 EC ? 48 8B D9 48 81 C1 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 48 8B CB"), sub_142FB5F0C, (void**)&g_sub_142FB5F0C);
	MH_CreateHook(hook::get_pattern("40 53 48 83 EC ? 44 8A 81 ? ? ? ? 48 8B D9 41 8A D0"), sub_142FB17F8, (void**)&g_sub_142FB17F8);

	MH_CreateHook(hook::get_pattern("33 DB 0F 29 70 D8 49 8B F9 4D 8B F0", -0x1B), GetPlayersNearPoint, (void**)&g_origGetPlayersNearPoint);
	MH_EnableHook(MH_ALL_HOOKS);
});
#endif

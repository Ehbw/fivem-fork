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

#include <netObjectMgr.h>
#include <netObject.h>

static const uint8_t kMaxPlayers = 128; // 32;
static const uint16_t kObjectId = std::numeric_limits<uint16_t>::max();

#define MAKE_HOOK(name, ret_type, args, call_args)         \
	static ret_type(*g_orig_##name) args;                  \
	static ret_type Detour_##name args                     \
	{                                                      \
		trace("name: %s called\n", #name);                   \
		return g_orig_##name call_args;                    \
	}

#define INSTALL_HOOK(name, target_addr)                                            \
	MH_CreateHook(target_addr, Detour_##name, (void**)&g_orig_##name); 

namespace rage
{
	using Vec3V = DirectX::XMVECTOR;
}

//NetObjectMgrList g_playerObjects[kMaxPlayers];
rage::Vec3V g_playerFocusPositions[kMaxPlayers];

// Expanded bitsets to handle increased players
uint32_t g_playerFocusPositionUpdateBitset[4];
uint32_t g_netPlayerVisiblePlayers[kMaxPlayers][4];
uint32_t g_netPlayerCreationAcked[4];

// Object player related bitsets that aren't >32 safe
std::unordered_map<uint16_t, std::array<uint32_t, 4>> g_objCreationAckedPlayers;

struct PatternPair
{
	std::string_view pattern;
	int offset;
};

struct PatternClampPair : PatternPair
{
	bool clamp;
};

MAKE_HOOK(seamlessnetcheck, void*, (void* a1), (a1));

extern ICoreGameInit* icgi;
extern CNetGamePlayer* g_players[256];
extern CNetGamePlayer* g_playerListRemote[256];
extern int g_playerListCountRemote;

template<bool Onesync, bool Legacy>
static bool Return()
{
	trace("returny\n");
	return icgi->OneSyncEnabled ? Onesync : Legacy;
}

static hook::cdecl_stub<rage::Vec3V*(rage::Vec3V*, CNetGamePlayer*, char*)> _getNetPlayerFocusPosition([]()
{
	return hook::get_pattern("41 22 C2 41 3A C1 0F 93 C0", -0x74);
});

#include <xmmintrin.h> // SSE intrinsics
#include <netPlayerManager.h>

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

static rage::Vec3V* (*g_origGetNetPlayerRelevancePosition)(rage::Vec3V* position, CNetGamePlayer* player, void* unk);
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
	// Calculate the difference vector
	XMVECTOR delta = XMVectorSubtract(a, b);

	// Compute the length (magnitude) of the difference
	float dis = XMVectorGetX(XMVector3Length(delta));
	return dis;
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

static bool (*g_origNetobjIsPlayerAcknowledged)(rage::netObject*, CNetGamePlayer*);
static bool netObject__IsPlayerAcknowledged(rage::netObject* object, CNetGamePlayer* player)
{
	trace("netObject__IsPlayerAcknowledged %i %i\n", object->GetObjectId(), player->physicalPlayerIndex());

	//if (!icgi->OneSyncEnabled)
	{
		return g_origNetobjIsPlayerAcknowledged(object, player);
	}

	uint8_t physicalIndex = player->physicalPlayerIndex();
	if (physicalIndex >= kMaxPlayers)
	{
		return false;
	}

	int index = physicalIndex >> 5;
	uint32_t objectId = object->GetObjectId();
	uint32_t creationAckedPlayer = g_objCreationAckedPlayers[objectId][index];
	bool result = (creationAckedPlayer >> (physicalIndex & 0x1F)) & 1;


	trace("result %i\n", result);

	return result;
}

static uint32_t (*g_origNetObjectSetPlayerCreationAcked)(rage::netObject*, CNetGamePlayer*, uint8_t);
static uint32_t netObject__setPlayerCreationAcked(rage::netObject* object, CNetGamePlayer* player, uint8_t playerIndex)
{
	trace("setPlayerCreationAcked %i %i %i\n", object->GetObjectId(), player->physicalPlayerIndex(), playerIndex);

	//if (!icgi->OneSyncEnabled)
	{
		return g_origNetObjectSetPlayerCreationAcked(object, player, playerIndex);
	}

	if (playerIndex >= kMaxPlayers)
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

static bool netObjMgr__IsWaitingForObjects(rage::netObjectMgr* mgr)
{
	if (!icgi->OneSyncEnabled)
	{
		return false;
	}

	std::bitset<128> playersMask;
	auto playerList = g_playerListRemote;
	for (int i = 0; i < g_playerListCountRemote; i++)
	{
		auto player = playerList[i];

		if (!player || !isNetPlayerLocal(player))
		{
			continue;
		}

		if (player != rage::GetLocalPlayer())
		{
			continue;
		}

		uint8_t activeIndex = player->activePlayerIndex();
		if (activeIndex < kMaxPlayers)
		{
			playersMask.set(activeIndex);
		}
	}

}

static void* (*g_origScMultiplayerImplCtor)(__int64, __int64);
static void* ScMultiplayerImplCtor(__int64 a1, __int64 mgr)
{
	auto result = g_origScMultiplayerImplCtor(a1, mgr);
	memset((void*)((intptr_t)result + 0x5510), 0, sizeof(void*) * 128);
	return result;
}

//@TODO: Revert this when IsWaitingForObjectIds is patched for 128
static bool(*g_unkTransitionCheck)(void*);
static bool unkTransitionCheck(void* self)
{
	bool val = g_unkTransitionCheck(self);
	trace("value %i\n", (int)val);
	return true;
}

static HookFunction hookFunction([]()
{
	if (!xbr::IsGameBuildOrGreater<1491>())
	{
		return;
	}

	// Expand Player Damage Array to support more players
	{
		std::initializer_list<PatternPair> list = {
			{"48 8D 0D ? ? ? ? 44 21 35", 3},
			{"4C 8D 25 ? ? ? ? 41 83 3C B4", 3},
			{ xbr::IsGameBuildOrGreater<1491>() ? "48 8D 0D ? ? ? ? 85 DB 74" : "48 8D 15 ? ? ? ? 85 FF", 3 },
			{"48 8D 15 ? ? ? ? 8B 0C 82", 3}
		};

		uint32_t* damageArrayReplacement = (uint32_t*)hook::AllocateStubMemory(256 * sizeof(uint32_t));
		memset(damageArrayReplacement, 0, 256 * sizeof(uint32_t));

		void* oldAddress = nullptr;
		for (auto& entry : list)
		{
			auto location = hook::get_pattern<int32_t>(entry.pattern, entry.offset);

			if (!oldAddress)
			{
				oldAddress = hook::get_address<void*>(location);
			}

			auto curTarget = hook::get_address<void*>(location);
			assert(curTarget == oldAddress);

			hook::put<int32_t>(location, (intptr_t)damageArrayReplacement - (intptr_t)location - 4);
		}
	}

	// Expand Player Cache data
	{
		static size_t kCachedPlayerSize = 0x10;
		void** cachedPlayerArray = (void**)hook::AllocateStubMemory(kCachedPlayerSize * kMaxPlayers);

		std::initializer_list<PatternPair> list = {
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
			{ xbr::IsGameBuildOrGreater<1491>() ? "48 8D 0D ? ? ? ? 48 8B C3 48 8B 0C D9" : "48 8B 0C C1 48 85 C9 74 ? 8A 41 ? C3", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 8A 41 ? EB", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 40 88 79", 3 },
			{ xbr::IsGameBuildOrGreater<1491>() ? "48 8D 15 ? ? ? ? 88 19" : "48 8D 0D ? ? ? ? 88 ?", 3 },
			{ "48 8D 3D ? ? ? ? 48 8B 0C DF", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 66 89 ?", 3 },
			{ "48 8D 1D ? ? ? ? 48 8B 1C C3 48 85 DB 74 ? 66 39 73", 3 },
			{ xbr::IsGameBuildOrGreater<1491>() ? "48 8D 0D ? ? ? ? 48 8B 04 C1 40 88 78" : "48 8D 0D ? ? ? ? 48 8B 04 C1 88 58", 3 },
			{ "48 8D 0D ? ? ? ? 48 8B 0C C1 48 85 C9 74 ? 66 3B 59", 3 },
			{ "48 8D 15 ? ? ? ? 84 C9 75", 3 }
		};

		void* oldAddress = nullptr;
		for (auto& entry : list)
		{
			auto location = hook::get_pattern<int32_t>(entry.pattern, entry.offset);

			if (!oldAddress)
			{
				oldAddress = hook::get_address<void*>(location);
			}

			auto curTarget = hook::get_address<void*>(location);
			assert(curTarget == oldAddress);

			hook::put<int32_t>(location, (intptr_t)cachedPlayerArray - (intptr_t)location - 4);
		}
	}

	// Replace 32-sized bandwidth related array.
	{
		void** bandwidthRelatedArray = (void**)hook::AllocateStubMemory(sizeof(unsigned int) * kMaxPlayers + 2);
		std::initializer_list<PatternPair> list = {
			{ "48 8D 3D ? ? ? ? B9 ? ? ? ? F3 AB 48 8D 8B", 3 },
			{ "48 8D 15 ? ? ? ? C7 04 82", 3 },
			{ "48 8D 0D ? ? ? ? 89 1C 81", 3 },
			{ "48 8D 05 ? ? ? ? 8B 14 B8 3B EA", 3 },
			{ "48 8D 0D ? ? ? ? 8B 04 81 C3 90 66 40 53", 3 }
		};

		void* oldAddress = nullptr;
		for (auto& entry : list)
		{
			auto location = hook::get_pattern<int32_t>(entry.pattern, entry.offset);

			if (!oldAddress)
			{
				oldAddress = hook::get_address<void*>(location);
			}

			auto curTarget = hook::get_address<void*>(location);
			assert(curTarget == oldAddress);

			hook::put<int32_t>(location, (intptr_t)bandwidthRelatedArray - (intptr_t)location - 4);
		}
	}


#if 0
	{
		//.text:0000000142C0C200 8B AF 9C 02 00 00 mov ebp, [rdi+29Ch]
		//.text:0000000142C0C206 EB 02 jmp short loc_142C0C20A
		static struct : jitasm::Frontend
		{
			intptr_t Retn = 0;

			void Init(const intptr_t retn)
			{
				this->Retn = retn;
			}

			virtual void InternalMain() override
			{
				mov(rdi, reinterpret_cast<uintptr_t>(g_playerListRemote));
				// Orig Code
				mov(r14, rcx);
				test(dl, dl);

				mov(rax, reinterpret_cast<uintptr_t>(&g_playerListCountRemote));
				mov(eax, dword_ptr[rax]);

				mov(rax, Retn);
				jmp(rax);
			}
		} patchStub;

		#if 0
		auto location = hook::get_pattern("48 8B 3D ? ? ? ? 45 84 C9");
		intptr_t retnLoc = (intptr_t)location + 21;
		patchStub.Init(retnLoc);
		hook::nop(location, 20);
		hook::jump(location, patchStub.GetCode());
		#endif
	}
#endif

	// Adjust bit logic to support 127/128
	{
		std::initializer_list<PatternPair> list = {
			{ "41 83 E6 ? 45 3B F7", 3 }
		};

		for (auto& entry : list)
		{
			auto location = hook::pattern(entry.pattern).count(1).get(0).get<uint8_t>(entry.offset);
			auto origVal = *location;
			trace("orig value %i\n", origVal);
			assert(origVal == 0x1f);
			hook::put<uint8_t>(location, 0x7F);
		}

	}

	// Replace 32/31 comparisions
	{
		std::initializer_list<PatternClampPair> list = {
			{ "80 79 ? ? 0F 92 C0 C3 48 8B 91", 3, false },
			{ "48 85 C0 74 ? 83 61 ? ? B8 ? ? ? ? 66 83 61", -57, false},
			{ "40 80 FF ? 73 ? 48 8B 4E", 3, false},
			{ "80 FB ? 72 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24", 2, false},

			//rage::rlSession::Host, TODO: This may not be needed.
			{ "83 F8 ? 7E ? BB ? ? ? ? B8", 2, false },
			{ "80 78 ? ? 72 ? 8D 56", 3, false },

			// player cached getters
			{ "E8 ? ? ? ? 84 C0 74 ? 40 88 3D ? ? ? ? EB", 23, false },

			// netObject SetOwner
			{ "80 F9 ? 73 ? E8 ? ? ? ? 48 8B D8 EB", 2, false},
			//CNetObjGame::CanClone
			{ "80 7A ? ? 49 8B F8 48 8B DA 48 8B F1 72", 3, false},

			// rlSession::OnUpdate
			{ "BD ? ? ? ? 49 8B 3E 48 85 FF 74", 1, false},

			// session related slot
			{ "40 80 FF ? 72 ? 40 B7 ? 48 8B 41", 3, false },
			{ "40 80 FF ? 72 ? 40 B7 ? 48 8D 0D", 3, false },
			{ "80 79 ? ? 72 ? B2", 3, false },
			{ "40 80 FF ? 72 ? BA ? ? ? ? 48 8B CB", 3, false },
			{ "41 80 FF ? 72 ? 40 84 FF", 3, false },
			{ "40 80 FE ? 72 ? 40 84 FF", 3, false },
			{ "80 FB ? 0F 82 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 83 C4 ? 41 5F 41 5E 41 5C 5F 5E C3 CC 7C", 2, false },
			{ "80 FB ? 72 ? 33 C0 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4", 2, false },
			//{ "80 FB ? 72 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24", 2, false },

			// TMP Patterns. TODO: Improve and support older game builds

			{ "80 7F ? ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 45 84 FF 74 ? 48 8B 05 ? ? ? ? 4C 8D 4C 24 ? 44 8B C6 49 8B D6 48 8B 88 ? ? ? ? 48 89 4C 24 ? 48 8B CF E8 ? ? ? ? EB ? 8A 57 ? 44 8B CE 4D 8B C6 48 8B CD E8 ? ? ? ? 84 C0 75 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? FE C3 80 FB ? 0F 82 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 83 C4 ? 41 5F 41 5E 41 5C 5F 5E C3 CC 7C", 3, false },
			{ "40 80 FF ? 73 ? 48 8B 43 ? 40 0F B6 CF 48 8B 7C C8 ? 48 85 FF 74 ? 48 8B CF E8 ? ? ? ? 84 C0 74 ? 48 8B 1B 48 8B CF E8 ? ? ? ? 8B D0 44 8B CE 4C 8B C5 48 8B CB E8 ? ? ? ? EB ? 32 C0 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 90 40 33 48", 3, false },

			//rage::rlSession::Join
			{ "83 FB ? 76 ? BB ? ? ? ? C7 44 24 ? ? ? ? ? 44 8B CB 48 8D 0D ? ? ? ? BA ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? B8 ? ? ? ? E9 ? ? ? ? 44 8A E7", 2, false },
			{ "83 FB ? 76 ? BB ? ? ? ? C7 44 24 ? ? ? ? ? 44 8B CB 48 8D 0D ? ? ? ? BA ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? B8 ? ? ? ? E9 ? ? ? ? 8B C3", 2, true },

			{ "80 FB ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? B0", 2, false},
			{ "80 FB ? 72 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 8A CB", 2, false }
		};

		for (auto& entry : list)
		{
			auto location = hook::pattern(entry.pattern).count(1).get(0).get<uint8_t>(entry.offset);
			auto origVal= *location;
			trace("orig value %i\n", origVal);
			assert(origVal == (entry.clamp ? 31 : 32));
			hook::put<uint8_t>(location, (entry.clamp ? (kMaxPlayers - 1) : kMaxPlayers));
		}
	}

	// Lord forgive me for the sins I've committed here
	// Patch allocation
	{
		auto val = hook::get_pattern<uint32_t>("B9 ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B D3 48 8B C8 E8 ? ? ? ? 48 8B 4B", 1);
		static uint32_t displacement = *val;
		hook::put<uint32_t>(val, *val + (sizeof(void*) * 128));
		g_origScMultiplayerImplCtor = hook::trampoline(hook::get_pattern("48 89 6B ? 89 6B ? 66 89 6B ? 48 89 6B ? E8", -71), ScMultiplayerImplCtor);

		static struct : jitasm::Frontend
 		{
			uintptr_t retnAddress;
			uintptr_t failAddress;

			void Init(uintptr_t retn, uintptr_t fail)
			{
				this->retnAddress = retn;
				this->failAddress = fail;
			}

			virtual void InternalMain() override
			{
				cmp(qword_ptr[rcx + rax * 8 + displacement], 0);
				jz("fail");
				mov(r11, retnAddress);
				jmp(r11);
				L("fail");
				mov(r11, failAddress);
				jmp(r11);
			}

		} patchStub;

		{
			auto location = hook::get_pattern("48 83 7C C1 ? ? 74 ? 40 FE C7");
			uintptr_t returnTo = (uintptr_t)location + 8;
			patchStub.Init(returnTo, (uintptr_t)hook::get_pattern("40 B7 ? 48 8B 41", 3));
			hook::nop(location, 8);
			hook::jump_reg<6>(location, patchStub.GetCode());
		}

		static struct : jitasm::Frontend
		{
			uintptr_t retnAddress;

			void Init(uintptr_t retn)
			{
				this->retnAddress = retn;
			}

			void InternalMain() override
			{
				lea(r14, qword_ptr[rbx + displacement]);
				mov(ebp, kMaxPlayers);

				mov(r11, retnAddress);
				jmp(r11);
			}
		} patchstub2;

		{
			auto location = hook::get_pattern("4C 8D 73 ? BD");
			uintptr_t returnTo = (uintptr_t)location + 9;
			patchstub2.Init(returnTo);
			hook::nop(location, 9);
			hook::jump_reg<5>(location, patchstub2.GetCode());
		}

		static struct : jitasm::Frontend
		{
			uintptr_t retnAddress;
			uintptr_t retnFail;

			void Init(uintptr_t retnSuccess, uintptr_t retnFail)
			{
				this->retnAddress = retnSuccess;
				this->retnFail = retnFail;
			}

			void InternalMain() override
			{
				cmp(qword_ptr[rcx + rax * 8 + displacement], rbx);
				jz("fail");
				mov(r8, retnAddress);
				jmp(r8);

				L("fail");
				mov(r8, retnFail);
				jmp(r8);
			}
		} patchStub3;

		{
			auto location = hook::get_pattern("48 39 5C C1");
			patchStub3.Init((uintptr_t)location + 7, (uintptr_t)hook::get_pattern("48 8D 0D ? ? ? ? E8 ? ? ? ? 48 85 C0 75 ? 48 8B C3"));
			hook::nop(location, 7);
			hook::jump(location, patchStub3.GetCode());
		}

		static struct : jitasm::Frontend
		{
			uintptr_t retnAddress;

			void Init(uintptr_t retn)
			{
				this->retnAddress = retn;
			}

			void InternalMain() override
			{
				mov(qword_ptr[rsi + rcx * 8 + displacement], rax);
				mov(rax, retnAddress);
				jmp(rax);
			}
		} patchStub4;

		{
			auto location = hook::get_pattern("48 89 44 CE ? 8B 86");
			patchStub4.Init((uintptr_t)location + 5);
			hook::nop(location, 5);
			hook::jump(location, patchStub4.GetCode());
		}

		static struct : jitasm::Frontend
		{
			uintptr_t retnAddress;

			void Init(uintptr_t retn)
			{
				this->retnAddress = retn;
			}

			void InternalMain() override
			{
				cmp(qword_ptr[rcx + rax * 8 + displacement], 0);
				mov(r11, retnAddress);
				jmp(r11);
			}
		} patchStub5;

		{
			auto location = hook::get_pattern("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 81 EC ? ? ? ? 48 8B EA 48 8B D9", 41);
			patchStub5.Init((uintptr_t)location + 6);
			hook::nop(location, 6);
			hook::jump_reg<6>(location, patchStub5.GetCode());
		}

		static struct : jitasm::Frontend
		{
			uintptr_t retnAddress;

			void Init(uintptr_t retn)
			{
				this->retnAddress = retn;
			}

			void InternalMain() override
			{
				push(rcx);
				mov(qword_ptr[rbx + rcx * 8 + displacement], rax);
				pop(rcx);
				mov(r11, retnAddress);
				jmp(r11);
			}
		} patchStub6;

		{
			auto location = hook::get_pattern("48 89 44 CB ? 48 89 83"); //("8A 05 ? ? ? ? 85 C9", -18);
			patchStub6.Init((uintptr_t)location + 5);
			hook::nop(location, 5);
			hook::jump_reg<5>(location, patchStub6.GetCode());
		}

		static struct : jitasm::Frontend
		{
			uintptr_t retnSuccess;
			uintptr_t retnFail;

			void Init(uintptr_t success, uintptr_t fail)
			{
				retnSuccess = success;
				retnFail = fail;
			}

			virtual void InternalMain() override
			{
				mov(rax, qword_ptr[rsi + rdi * 8 + displacement]);
				test(rax, rax);
				jz("fail");

				mov(r11, retnSuccess);
				jmp(r11);

				L("fail");
				mov(r11, retnFail);
				jmp(r11);
			}
		} patchStub7;

		{
			auto location = hook::get_pattern("48 8B 44 FE ? 48 85 C0 74 ? 48 8B 50");
			patchStub7.Init((uintptr_t)location + 10, (uintptr_t)location + 35);
			hook::nop(location, 0xA);
			hook::jump_reg<5>(location, patchStub7.GetCode());
		}
	}

	// Skip unused host kick related >32-unsafe arrays in onesync
	hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 75 ? 8B 05 ? ? ? ? 33 C9 89 44 24"), Return<true, false>);

	//hook::call(hook::get_pattern("E8 ? ? ? ? 84 C0 74 ? 8B 05 ? ? ? ? 39 43"), Return<true, true>);

	//IsWaitingForObjectIds
	//hook::return_function(hook::get_pattern("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC ? 8A 15"));

	hook::put<uint8_t>(hook::get_pattern("3C ? 77 ? BA ? ? ? ? C7 44 24", 1), 1);

	// Don't call Garage related events on player join
	// Apprently everything explodes. wow	
	//hook::return_function(hook::get_pattern("E8 ? ? ? ? 48 8B 1D ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B 4B ? 48 8B D7"));

	// Rewrite functions to account for extended players
	MH_Initialize();
	INSTALL_HOOK(seamlessnetcheck, hook::get_pattern("48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 48 8B 89 ? ? ? ? 48 8D 91"));


	//INSTALL_HOOK(rage__netObject__PlayerHasJoined, hook::get_pattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 80 79 ? ? 48 8B EA 48 8B F1"));
	//INSTALL_HOOK(sub_142460300, hook::get_pattern("48 89 5C 24 ? 55 56 57 41 56 41 57 48 83 EC ? 48 8B D9 E8"));

	// Update Player Focus Positions to support 128.
	MH_CreateHook((xbr::IsGameBuildOrGreater<1436>()) ? hook::get_pattern("0F A3 D0 0F 92 C0 88 06", -0x76) : hook::get_pattern("44 0F A3 C0 0F 92 C0 41 88 02", -0x32), GetPlayerFocusPosition, (void**)&g_origGetNetPlayerRelevancePosition);
	MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 8B CF E8 ? ? ? ? 48 85 C0 74 ? 48 8B CF E8 ? ? ? ? 48 8B 88")), UpdatePlayerFocusPosition, (void**)&g_origUpdatePlayerFocusPosition);

	// netObj player acknowledge bitset. Capped to 32 bits. Need 128
	// Breaks badly. Not sure if we need to patch this either since 31 is almost always passed to it
#if 1
	if (xbr::IsGameBuildOrGreater<1491>())
	{
		MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? 40 8A FB 84 C0 74")), netObject__IsPlayerAcknowledged, (void**)&g_origNetobjIsPlayerAcknowledged);
		MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? EB ? 47 85 0C 86")), netObject__setPlayerCreationAcked, (void**)&g_origNetObjectSetPlayerCreationAcked);
	}
#endif

	MH_CreateHook(hook::get_pattern("40 53 48 83 EC ? 80 3D ? ? ? ? ? 48 8B D9 75 ? BA ? ? ? ? C7 44 24 ? ? ? ? ? 41 B9 ? ? ? ? 48 8D 0D ? ? ? ? 41 B8 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84"), unkTransitionCheck, (void**)&g_unkTransitionCheck);

	MH_CreateHook(hook::get_pattern("33 DB 0F 29 70 D8 49 8B F9 4D 8B F0", -0x1B), GetPlayersNearPoint, (void**)&g_origGetPlayersNearPoint);

	//TODO: move to another file
	//MH_CreateHook(hook::get_pattern("48 8B 05 ? ? ? ? 45 33 C9 4C 8B 80"), GetLocalPlayerObjects, (void**)&g_origGetLocalPlayerObjects);

	// These are already handled in CloneExperiments.cpp (i hate that file with a passion)
	//MH_CreateHook(hook::get_pattern("48 8B 94 C7 ? ? ? ? 48 8B 5C 24 ? 48 8B C2 48 83 C4 ? 5F C3 48 89 5C 24 ? 57 48 83 EC ? 48 8B 3D"), NetInterfaceGetPlayerWrap, (void**)&g_getPlayerByIndex);
	//MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 8D 97 ? ? ? ? 48 63 8A")), CNetworkPlayerMgrGetPlayer, (void**)&g_networkPlayerMgrGetPlayer);
	MH_EnableHook(MH_ALL_HOOKS);
});

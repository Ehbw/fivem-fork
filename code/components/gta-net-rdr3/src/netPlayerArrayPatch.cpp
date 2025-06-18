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

/*
* Patch out inlined instances of playerLists that are intentionally capped at 32 players
* This patch also provides backwards-compatability for non-onesync ensuring behaviour remains the same
*/

extern ICoreGameInit* icgi;

extern CNetGamePlayer* g_players[256];
extern CNetGamePlayer* g_playerListRemote[256];
extern int g_playerListCountRemote;

static CNetGamePlayer** GetPlayers()
{
	if (icgi->OneSyncEnabled)
	{
		return g_players;
	}

	//@TODO: non-onesync support
	return nullptr;
}

static CNetGamePlayer** GetPlayersRemote()
{
	if (icgi->OneSyncEnabled)
	{
		return g_playerListRemote;
	}

	//@TODO: non-onesync support
	return nullptr;
}

static CNetGamePlayer* GetPlayerByIndex(uint8_t index)
{
	if (!icgi->OneSyncEnabled)
	{
		//@TODO: non-onesync support
		return nullptr;
	}

	if (index < 0 || index >= 256)
	{
		return nullptr;
	}

	return g_players[index];
}

static int GetPlayerCount()
{
	if (icgi->OneSyncEnabled)
	{
		return g_playerListCountRemote;
	}

	//@TODO: non-onesync support
	return 4848;
}

static HookFunction hookFunction([]
{
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
				push(rax);
				mov(r11, reinterpret_cast<uintptr_t>(&GetPlayersRemote));
				call(r11);
				mov(rdi, rax); // reinterpret_cast<uintptr_t>(g_playerListRemote));
				pop(rax);

				test(al, al);
				jz("Fail");

				push(rax);
				mov(r11, reinterpret_cast<uintptr_t>(&GetPlayerCount));
				call(r11);
				mov(ebp, eax);
				pop(rax);

				mov(r11, retnSuccess);
				jmp(r11);

				L("Fail");
				mov(r11, retnFail);
				jmp(r11);
			};
		} patchStub;

		const uintptr_t patch = (uintptr_t)location + 45;

		const uintptr_t retnFail = patch + 19; // 142C0C208
		const uintptr_t retnSuccess = retnFail + 2; // 0x142C0C20A

		assert((uintptr_t)0x142C0C208 == retnFail);
		assert((uintptr_t)0x142C0C20A == retnSuccess);

		patchStub.Init(retnSuccess, retnFail);

		hook::nop(patch, 19);
		hook::nop(hook::get_pattern("48 81 C7 ? ? ? ? EB ? 33 FF 33 F6 85 ED 74 ? 48 8B 1F 49 8B CE 48 8B D3 E8 ? ? ? ? 84 C0 74 ? 49 8B 06 49 8B CE 0F B6 5B ? FF 50 ? 48 8B C8 8B D3 E8 ? ? ? ? 84 C0 74 ? FF C6 48 83 C7 ? 3B F5 72 ? B0 ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 8B 7C 24 ? 48 83 C4 ? 41 5E C3 32 C0 EB ? 90"), 7);
		hook::jump_reg<5>(patch, patchStub.GetCode());
	}

	// Fix StartSynchronising by using our own playerList
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
				push(rax);
				mov(r11, reinterpret_cast<uintptr_t>(&GetPlayersRemote));
				call(r11);
				mov(rbx, rax); // reinterpret_cast<uintptr_t>(g_playerListRemote));
				pop(rax);

				test(dl, dl);
				jz("Fail");

				push(rax);
				mov(r11, reinterpret_cast<uintptr_t>(&GetPlayerCount));
				call(r11);
				mov(ecx, eax);
				pop(rax);

				/*mov(r14, reinterpret_cast<uintptr_t>(&g_playerListCountRemote));
				mov(ecx, dword_ptr[r14]);
				*/

				mov(r11, retnSuccess);
				jmp(r11);

				L("Fail");
				mov(r11, retnFail);
				jmp(r11);
			};
		} patchStub;

		const uintptr_t retnFail = (uintptr_t)location + 19;
		const uintptr_t retnSuccess = retnFail + 2;

		// TMP ASSERTS
		assert(0x142C1D4A2 == retnSuccess);
		assert(0x142C1D4A0 == retnFail);

		patchStub.Init(retnSuccess, retnFail);

		hook::nop(location, 19);
		hook::jump_reg<5>(location, patchStub.GetCode());

		// Remove +add
		hook::nop((uintptr_t)location + 0x19, 7);
	}

	// Vehicle scene related
	{
		auto location = hook::get_pattern("40 0F B6 C6 48 8B B4 C5");

		static struct : jitasm::Frontend
		{
			uintptr_t retnAddr;

			void Init(uintptr_t retn)
			{
				this->retnAddr = retn;
			}

			virtual void InternalMain() override
			{
				// Original code
				movzx(eax, si);

				// rsi should be the pointer to the player
				push(rcx);

				sub(rsp, 0x20);
				mov(rcx, rax);
				mov(r11, reinterpret_cast<uintptr_t>(&GetPlayerByIndex));
				push(rax);
				call(r11);
				add(rsp, 0x20);
				mov(rsi, rax);

				pop(rcx);

				mov(r11, retnAddr);
				jmp(r11);
			}
		} patchStub;

		hook::nop(location, 12);
		patchStub.Init((uintptr_t)location + 12);
		hook::jump_reg<5>(location, patchStub.GetCode());
	}

	// Patch netObject::DependencyThreadUpdate
	{
		auto location = hook::get_pattern("0F B6 C3 48 8B B4 C6");

		static struct : jitasm::Frontend
		{
			uintptr_t retnFail;
			uintptr_t retnSuccess;

			void Init(uintptr_t success, uintptr_t fail)
			{
				retnFail = fail;
				retnSuccess = success;
			}

			virtual void InternalMain() override
			{
				// Original code
				movzx(eax, bl);
				push(rcx);

				//sub(rsp, 0x28);
				//mov(qword_ptr[rsp + 0x20], rax);

				mov(rcx, rax);
				mov(r11, reinterpret_cast<uintptr_t>(&GetPlayerByIndex));
				call(r11);

				//mov(rax, qword_ptr[rsp + 0x20]);
				//add(rsp, 0x28);
				mov(rsi, rax);

				pop(rcx);

				test(rsi, rsi);
				jz("fail");

				mov(r11, retnSuccess);
				jmp(r11);

				L("fail");
				mov(r11, retnFail);
				jmp(r11);
			}
		} patchStub2;

		const uintptr_t retnSuccess = (uintptr_t)location + 16;
		const uintptr_t retnFail = retnSuccess + 50;

		assert(retnSuccess == (uintptr_t)0x142BFC618);
		assert(retnFail == (uintptr_t)0x142BFC64A);

		hook::nop(location, 16);
		patchStub2.Init(retnSuccess, retnFail);
		hook::jump_reg<5>(location, patchStub2.GetCode());
	}

	// Patch netObjectMgrBase::_UpdateAllInScopeStateImmediately
	{
		auto location = hook::get_pattern("74 ? 8B 83 ? ? ? ? EB ? 33 C0 84 C9 74 ? 48 81 C3 ? ? ? ? EB ? 33 DB 85 C0 74 ? 8B F0");

		static struct : jitasm::Frontend
		{
			uintptr_t retnFail;
			uintptr_t retnSuccess;

			void Init(uintptr_t success, uintptr_t fail)
			{

			}

			virtual void InternalMain() override
			{

			}
		} patchStub3;
	}

	//TODO: PATCH 0x142C0B750/sub_142C0B718 rage::netInterface::m_PlayerMgr->m_numRemotePhysicalPlayers
	//TODO: VERIFY IF sub_142C131A0 IS NEEDED TO BE PATCHED
});

#include "StdInc.h"

#include <netSyncData.h>
#include <Hooking.h>
#include <Hooking.Stubs.h>

#include <ICoreGameInit.h>
#include <GameInit.h>
#include <NetLibrary.h>

//
// By default, RDR2 (and all previous RAGE titles) used 13 bits for object ids. Allowing for a max of 8192 (in RDR3 this is capped to 8000).
// In FiveM this has already been patched for a while. In RedM this is much more complicated with the extensive use of CSyncedVars and Object ID Mapping
// Object ID mapping has a fixed length of 8000, but isn't needed under OneSync, so we can patch out the usage in a way that maintains legacy P2P sync.
//

static constexpr int kMaxObjectIdSize = 16;
static bool g_lengthHackEnabled = false;

static hook::cdecl_stub<void(rage::CSyncDataBase*, uint16_t*, char*, void*)> CSyncDataBase__serialiseObjectId([]()
{
	return hook::get_call(hook::get_pattern("0F B7 03 B9 3F 1F 00 00 66 FF C8 66 3B C1 76 04", -5));
});

#include <set>
static std::set<uintptr_t> g_accessedAddress{};

static inline void __forceinline LogObjectIdSerialise(const char* func)
{
	uintptr_t retnAddress = (uintptr_t)_ReturnAddress();
	if (g_accessedAddress.find(retnAddress) == g_accessedAddress.end())
	{
		trace("%s: %p\n", func, (void*)hook::get_unadjusted(_ReturnAddress()));
		g_accessedAddress.insert(retnAddress);
	}
}

static inline void __forceinline LogObjectIdSerialise(const char* func, uint16_t objectId)
{
	uintptr_t retnAddress = (uintptr_t)_ReturnAddress();
	if (g_accessedAddress.find(retnAddress) == g_accessedAddress.end())
	{
		trace("%s: %i %p\n", func, objectId, (void*)hook::get_unadjusted(_ReturnAddress()));
		g_accessedAddress.insert(retnAddress);
	}
}

// bool CSyncDataWriter::SerialiseObjectID(rage::CSyncDataWriter* self, uint16_t* objectId, char* prefix, void* a4)
static bool (*g_origCSyncDataWriter__SerialiseObjectIdPF)(rage::CSyncDataReader*, uint16_t*, char*, void*);
static bool CSyncDataWriter__SerialiseObjectIdPF(rage::CSyncDataReader* self, uint16_t* objectId, char* a3, void* a4)
{
	LogObjectIdSerialise(__func__, *objectId);

	if (!g_lengthHackEnabled )
	{
		return g_origCSyncDataWriter__SerialiseObjectIdPF(self, objectId, a3, a4);
	}

	CSyncDataBase__serialiseObjectId(self, objectId, a3, a4);
	return self->m_buffer->WriteUns(*objectId, kMaxObjectIdSize);
}

// static: CSyncDataWriter::SerialiseObjectID(rage::CSyncDataReader* self, uint16_t* objectId)
static bool (*g_origCSyncDataWriter__SerialiseObjectId)(rage::CSyncDataWriter* self, uint16_t* objectId);
static bool CSyncDataWriter__SerialiseObjectId(rage::CSyncDataWriter* self, uint16_t* objectId)
{
	LogObjectIdSerialise(__func__, *objectId);

	if (!g_lengthHackEnabled)
	{
		return g_origCSyncDataWriter__SerialiseObjectId(self, objectId);
	}

	return self->m_buffer->WriteUns(*objectId, kMaxObjectIdSize);
}

 static void (*g_origCSyncDataReader__SerialiseObjectIdPF)(rage::CSyncDataReader*, uint16_t*, char*, void*);
static void CSyncDataReader__SerialiseObjectIdPF(rage::CSyncDataReader* self, uint16_t* objectId, char* a3, void* a4)
{
	if (!g_lengthHackEnabled)
	{
		LogObjectIdSerialise(__func__);
		return g_origCSyncDataReader__SerialiseObjectIdPF(self, objectId, a3, a4);
	}

	uint32_t readerObjectId = 0;
	if (self->m_buffer->ReadInteger(&readerObjectId, kMaxObjectIdSize))
	{
		*objectId = readerObjectId;
	}

	LogObjectIdSerialise(__func__, *objectId);
	CSyncDataBase__serialiseObjectId(self, objectId, a3, a4);
}

static void (*g_origCSyncDataReader__SerialiseObjectId)(rage::CSyncDataReader*, uint16_t*);
static void CSyncDataReader__SerialiseObjectId(rage::CSyncDataReader* self, uint16_t* objectId)
{
	if (!g_lengthHackEnabled)
	{
		LogObjectIdSerialise(__func__);
		return g_origCSyncDataReader__SerialiseObjectId(self, objectId);
	}

	uint32_t readerObjectId = 0;
	if (self->m_buffer->ReadInteger(&readerObjectId, kMaxObjectIdSize))
	{
		*objectId = readerObjectId;
	}
	LogObjectIdSerialise(__func__, *objectId);
}

static void (*g_origSyncDataSizeCalculator_SerializeObjectId)(rage::CSyncDataSizeCalculator*);
static void SyncDataSizeCalculator_SerializeObjectId(rage::CSyncDataSizeCalculator* syncData)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSyncDataSizeCalculator_SerializeObjectId(syncData);
	}

	syncData->m_size += kMaxObjectIdSize;
}

static bool (*g_origSetVehicleExclusiveDriver__Write)(hook::FlexStruct*, rage::datBitBuffer*);
static bool SetVehicleExclusiveDriver__Write(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSetVehicleExclusiveDriver__Write(self, buffer);
	}

	buffer->WriteUns(self->Get<uint16_t>(8), kMaxObjectIdSize);
	return buffer->WriteUns(self->Get<uint32_t>(0xC), 2);
}

static bool (*g_origSetLookAtEntity__Write)(hook::FlexStruct*, rage::datBitBuffer*);
static bool SetLookAtEntity__Write(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSetLookAtEntity__Write(self, buffer);
	}

	buffer->WriteUns(self->Get<uint16_t>(8), kMaxObjectIdSize);
	buffer->WriteUns(self->Get<uint32_t>(0xC), 18);
	return buffer->WriteUns(self->Get<uint32_t>(0x14), 0xA);
}

static bool (*g_origSetVehicleTempAction__Write)(hook::FlexStruct*, rage::datBitBuffer*);
static bool SetVehicleTempAction__Write(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSetVehicleTempAction__Write(self, buffer);
	}

	buffer->WriteUns(self->Get<uint16_t>(8), kMaxObjectIdSize);
	buffer->WriteUns(self->Get<uint32_t>(0xC), 8);

	bool hasTime = self->Get<bool>(0x14);
	buffer->WriteBit(hasTime);

	bool result = buffer->WriteBit(hasTime);

	if (!hasTime)
	{
		self->Set(0x10, -1);
		return result;
	}

	return buffer->WriteUns(self->Get<uint32_t>(0x10), 16);
}

static bool (*g_origNetworkEventComponentControlBase__Serialise)(hook::FlexStruct*, rage::datBitBuffer*);
static bool NetworkEventComponentControlBase__Serialise(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origNetworkEventComponentControlBase__Serialise(self, buffer);
	}

	buffer->WriteUns(self->Get<uint16_t>(0x8), kMaxObjectIdSize);
	buffer->WriteUns(self->Get<uint16_t>(0xA), kMaxObjectIdSize);

	buffer->WriteUns(self->Get<uint8_t>(0xC), 6); // componentIndex
	return buffer->WriteBit(self->Get<bool>(0xD)); 
}

static void (*g_origNetworkEventComponentControlBase__SerialiseReply)(hook::FlexStruct*, rage::datBitBuffer*);
static void NetworkEventComponentControlBase__SerialiseReply(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__, self->Get<uint16_t>(0x18));

	if (!g_lengthHackEnabled)
	{
		return g_origNetworkEventComponentControlBase__SerialiseReply(self, buffer);
	}

	if (self->Get<uint16_t>(0xE))
	{
		buffer->WriteBit(self->Get<bool>(0x1A));
		if (self->Get<bool>(0x1A))
		{
			buffer->WriteUns(self->Get<uint16_t>(0x18), kMaxObjectIdSize);
		}
	}
}

static HookFunction objectIdMapping([]()
{
	// Patch the respective CDataSyncReader/CDataSyncWriter/CDataSyncSizeCalculator fields (static and non-static) to properly account for 16 bit objectIds.
	// Along with removing usage of object mapping.
	{
		const auto dataSyncReaderVtbl = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 0D ? ? ? ? 49 89 4B ? 48 8B C8", 3));
		const auto dataSyncWriterVtbl = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 49 89 53 ? 49 89 43 ? 33 FF", 3));
		const auto dataSyncCalcVtbl = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 05 ? ? ? ? 83 64 24 ? ? 48 8D 54 24 ? 48 8B CB", 3));

		constexpr size_t kObjectIdOffset = 26;
		constexpr size_t kObjectIdOffset2 = 27;
		constexpr size_t kObjectIdOffset3 = 28;

		// CDataSyncSizeCalculator::SerialiseObjectId(s)
		g_origSyncDataSizeCalculator_SerializeObjectId = (decltype(g_origSyncDataSizeCalculator_SerializeObjectId))dataSyncCalcVtbl[kObjectIdOffset];
		hook::put(&dataSyncCalcVtbl[kObjectIdOffset], SyncDataSizeCalculator_SerializeObjectId);
		hook::put(&dataSyncCalcVtbl[kObjectIdOffset2], SyncDataSizeCalculator_SerializeObjectId);
		hook::put(&dataSyncCalcVtbl[kObjectIdOffset3], SyncDataSizeCalculator_SerializeObjectId);

		// CDataSyncWriter::SerialiseObjectId(s)
		g_origCSyncDataWriter__SerialiseObjectIdPF = (decltype(g_origCSyncDataWriter__SerialiseObjectIdPF))dataSyncWriterVtbl[kObjectIdOffset];
		hook::put(&dataSyncWriterVtbl[kObjectIdOffset], CSyncDataWriter__SerialiseObjectIdPF);
		hook::put(&dataSyncWriterVtbl[kObjectIdOffset2], CSyncDataWriter__SerialiseObjectIdPF);
		g_origCSyncDataWriter__SerialiseObjectId = (decltype(g_origCSyncDataWriter__SerialiseObjectId))dataSyncWriterVtbl[kObjectIdOffset3];
		hook::put(&dataSyncWriterVtbl[kObjectIdOffset3], CSyncDataWriter__SerialiseObjectId);
	
		// CDataSyncReader::SerialiseObjectId(s)
		g_origCSyncDataReader__SerialiseObjectIdPF = (decltype(g_origCSyncDataReader__SerialiseObjectIdPF))dataSyncReaderVtbl[kObjectIdOffset];
		g_origCSyncDataReader__SerialiseObjectId = (decltype(g_origCSyncDataReader__SerialiseObjectId))dataSyncReaderVtbl[kObjectIdOffset3];
	
		hook::put(&dataSyncReaderVtbl[kObjectIdOffset], CSyncDataReader__SerialiseObjectIdPF);
		hook::put(&dataSyncReaderVtbl[kObjectIdOffset2], CSyncDataReader__SerialiseObjectIdPF);
		hook::put(&dataSyncReaderVtbl[kObjectIdOffset3], CSyncDataReader__SerialiseObjectId);

		hook::call(hook::pattern("C6 44 24 ? 02 45 ? ? E8 ? ? ? ? 48 ? ? 48 C3").count(6).get(1).get<void>(8), CSyncDataWriter__SerialiseObjectId);
		g_origCSyncDataReader__SerialiseObjectId = hook::trampoline(hook::get_pattern("40 53 48 83 EC ? 48 8B 49 ? 41 B8"), CSyncDataReader__SerialiseObjectId);
	}

	//DEBUG: don't allocate CNetIdMapper's object ids
	//hook::call(hook::get_pattern("E8 ? ? ? ? 48 81 C7 ? ? ? ? 48 83 EE ? 75 ? 4C 8D 83"), ReturnSelf);

	// Patch CScriptEntityStateChangeEvent(s) to remove id mapping.
	{
		const auto vehicleExclusiveDriverVtbl = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 0D ? ? ? ? EB ? 4C 8D 41", 3));
		const auto vehicleTempActionVtbl = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 0D ? ? ? ? 66 89 42 ? 41 8B C1", 3));
		const auto lookAtEntityVtbl = hook::get_address<uintptr_t*>(hook::get_pattern("48 8D 0D ? ? ? ? 66 89 42 ? B8", 3));

		constexpr size_t kReadDataOffset = 5;
		constexpr size_t kWriteDataOffset = 6;

		g_origSetVehicleExclusiveDriver__Write = (decltype(g_origSetVehicleExclusiveDriver__Write))vehicleExclusiveDriverVtbl[kWriteDataOffset];
		hook::put(&vehicleExclusiveDriverVtbl[kWriteDataOffset], SetVehicleExclusiveDriver__Write);
		g_origSetVehicleTempAction__Write = (decltype(g_origSetVehicleTempAction__Write))vehicleTempActionVtbl[kWriteDataOffset];
		hook::put(&vehicleTempActionVtbl[kWriteDataOffset], SetVehicleTempAction__Write);
		g_origSetLookAtEntity__Write = (decltype(g_origSetLookAtEntity__Write))lookAtEntityVtbl[kWriteDataOffset];
		hook::put(&lookAtEntityVtbl[kWriteDataOffset], SetLookAtEntity__Write);
	}

	// Patch CGameScriptId to remove id mapping.
	// CGameScriptId::Read
	{
		auto location = hook::get_pattern("41 80 3C 00 ? 73");

		static struct : jitasm::Frontend
		{
			intptr_t retnOrig = 0;
			intptr_t retnCode = 0;

			void Init(intptr_t retnCode, intptr_t retnOrig)
			{
				this->retnCode = retnCode;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(rdx, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(al, byte_ptr[rdx]);

				cmp(al, al);
				jz("orig");

				movzx(eax, word_ptr[rbp + 0x40]);

				mov(rcx, retnCode);
				jmp(rcx);

				L("orig");
				// Original code
				cmp(byte_ptr[r8 + rax], 0x20);

				mov(rcx, retnOrig);
				jmp(rcx);
			}
		} scriptReadStub;

		scriptReadStub.Init((uintptr_t)hook::get_pattern("89 46 ? EB ? 83 4E ? ? 83 C8"), (uintptr_t)location + 5);
		hook::nop(location, 5);
		hook::jump_rcx(location, scriptReadStub.GetCode());
	}

	// CGameScriptID::Write
	{
		auto location = hook::get_pattern("B9 ? ? ? ? 66 2B C6 66 3B C1 76");

		static struct : jitasm::Frontend
		{
			uintptr_t retn = 0;
			uintptr_t retnOrig = 0;

			void Init(uintptr_t retn, uintptr_t retnOrig)
			{
				this->retn = retn;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(rdx, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(al, byte_ptr[rdx]);

				cmp(al, al);
				jz("orig");

				movzx(edx, word_ptr[rbx + 0x18]);

				mov(rax, retn);
				jmp(rax);

				L("orig");
				// Original code
				mov(ecx, 7999);
				
				mov(rax, retnOrig);
				jmp(rax);
			}
		} scriptWriteStub;

		scriptWriteStub.Init((uintptr_t)location + 0x22, (uintptr_t)location + 5);
		hook::nop(location, 5);
		hook::jump(location, scriptWriteStub.GetCode());
	}

	// Patch out id mapping from CTaskClimbLadder
	{
		auto location = hook::get_pattern("0F B7 50 42 41 B9 3F 1F 00 00", 4);

		static struct : jitasm::Frontend
		{
			intptr_t retnSuccess = 0;
			intptr_t retnFailure = 0;
			intptr_t retnOrig = 0;

			void Init(const intptr_t retSuccess, const intptr_t retFail, const intptr_t retnOrig)
			{
				this->retnSuccess = retSuccess;
				this->retnFailure = retFail;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(rdx, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(al, byte_ptr[rdx]);

				cmp(al, al);
				jz("orig");

				// edx already contains the objectId
				movzx(eax, word_ptr[r8 + 66]);

				cmp(edx, eax);
				jbe("success");

				mov(rax, retnFailure);
				jmp(rax);

				L("success");
				mov(rax, retnSuccess);
				jmp(rax);

				L("orig");
				// Original code.
				mov(r9d, 7999);

				mov(rax, retnOrig);
				jmp(rax);
			}
		} ladderPatchStub;

		ladderPatchStub.Init((uintptr_t)location + 0x3A, (uintptr_t)location + 0x36, (uintptr_t)location + 6);
		hook::nop(location, 6);
		hook::jump(location, ladderPatchStub.GetCode());
	}

	// Patch out id mapping inside of a CPhysical function
	{
		auto location = hook::get_pattern("41 B8 ? ? ? ? 8D 41 ? 66 41 3B C0");

		static struct : jitasm::Frontend
		{
			intptr_t retn = 0;
			intptr_t retnOrig = 0;

			void Init(const intptr_t retn, const intptr_t retnOrig)
			{
				this->retn = retn;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(rdx, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(al, byte_ptr[rdx]);

				cmp(al, al);
				jz("orig");

				// ecx contains the object id.
				mov(dx, cx);

				mov(rax, retn);
				jmp(rax);

				L("orig");
				// Original code
				mov(r8d, 7999);

				mov(rax, retnOrig);
				jmp(rax);
			}
		} physicalPatchStub;

		physicalPatchStub.Init((uintptr_t)location + 0x1D, (uintptr_t)location + 6);
		hook::nop(location, 6);
		hook::jump(location, physicalPatchStub.GetCode());
	}

	// Patch "CRespawnPlayerPedEvent" event serialise function to remove object mapping
	{
		auto location = hook::get_pattern("8A 43 ? 3C ? 75 ? 65 48 8B 0C 25");

		static struct : jitasm::Frontend
		{
			intptr_t retnOriginal = 0;
			intptr_t retn = 0;

			void Init(intptr_t orig, intptr_t retn)
			{
				this->retnOriginal = orig;
				this->retn = retn;
			}

			virtual void InternalMain() override
			{
				mov(rcx, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(bl, byte_ptr[rcx]);

				cmp(bl, bl);
				jz("orig");
				
				// Original code
				mov(al, byte_ptr[rbx + 8]);
				cmp(al, 2); // type <= 2
				jg("fail");

				movzx(edi, word_ptr[r14]);
				mov(word_ptr[rsi + 0x5A], di);

				L("fail");
				mov(rcx, retn);
				jmp(rcx);

				L("orig");
				// Original code
				mov(al, byte_ptr[rbx + 8]);
				cmp(al, 1);

				mov(rcx, retnOriginal);
				jmp(rcx);
			}
		} respawnPatchStub;

		const uintptr_t retnAddress = (uintptr_t)hook::get_pattern("48 8B 05 ? ? ? ? 0F B7 3C 48 66 89 7E", 0xF);
		respawnPatchStub.Init((uintptr_t)location + 5, retnAddress);
		hook::nop(location, 5);
		hook::jump(location, respawnPatchStub.GetCode());
	}

	// Patch "CNetworkEventComponentControlBase" and derivative classes to support 16 bits.
	g_origNetworkEventComponentControlBase__Serialise = hook::trampoline(hook::get_pattern("BD ? ? ? ? 66 FF C8", -0x18), NetworkEventComponentControlBase__Serialise);
	g_origNetworkEventComponentControlBase__SerialiseReply = hook::trampoline(hook::get_pattern("41 B8 ? ? ? ? 0F B7 D3 48 8B CE E8 ? ? ? ? 48 8B 5C 24", -0x4C), NetworkEventComponentControlBase__SerialiseReply);

	// CNetworkEventVehComponentControl::Serialise
	{
		auto location = hook::get_pattern("0F B7 43 ? B9 ? ? ? ? 66 FF C8");

		static struct : jitasm::Frontend
		{
			intptr_t retnOrig = 0;
			intptr_t retn = 0;

			void Init(intptr_t retnOrig, intptr_t retn)
			{
				this->retnOrig = retnOrig;
				this->retn = retn;
			}

			virtual void InternalMain() override
			{
				mov(rdx, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(al, byte_ptr[rdx]);

				cmp(al, al);
				jz("orig");

				movzx(edx, word_ptr[rbx + 0x18]);

				mov(rax, retn);
				jmp(rax);

				L("orig");
				movzx(edx, word_ptr[rbx + 0x18]);
				mov(ecx, 7999);

				mov(rax, retnOrig);
				jmp(rax);

			}
		} serialiseWriteStub;

		serialiseWriteStub.Init((uintptr_t)location + 9, (uintptr_t)hook::get_pattern("41 B8 ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 83 C4 ? 5F C3 4C 8B DC"));
		hook::nop(location, 9);
		hook::jump(location, serialiseWriteStub.GetCode());
	}
});

static InitFunction initFunction([]()
{
	NetLibrary::OnNetLibraryCreate.Connect([](NetLibrary* netLibrary)
	{		
		Instance<ICoreGameInit>::Get()->OnGameRequestLoad.Connect([]()
		{
			g_lengthHackEnabled = Instance<ICoreGameInit>::Get()->OneSyncBigIdEnabled;
		});
	});

	OnKillNetworkDone.Connect([]()
	{
		g_lengthHackEnabled = false;
	});
});

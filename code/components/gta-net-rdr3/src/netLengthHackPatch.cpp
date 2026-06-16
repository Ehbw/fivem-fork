#include "StdInc.h"

#include <netSyncData.h>
#include <Hooking.h>
#include <Hooking.Stubs.h>

#include <ICoreGameInit.h>
#include <GameInit.h>
#include <NetLibrary.h>

///
/// All current RAGE titles (RDR2, GTAV, GTAIV) use 13 bits to store object ids for any networked entities. 
/// Allowing for a max of 8192 network ids at a time. However RDR2 has special restrictions limiting total ids to 8000.
/// From Network ID Mapping, which isn't required under OneSync and is able to be patched out.
/// 

static constexpr int kDefaultObjectIdSize = 13;
static constexpr int kBigModeObjectIdSize = 16;

static bool g_lengthHackEnabled = false;

static hook::cdecl_stub<void(rage::CSyncDataBase*, uint16_t*, char*, void*)> CSyncDataBase__serialiseObjectId([]()
{
	return hook::get_call(hook::get_pattern("0F B7 03 B9 3F 1F 00 00 66 FF C8 66 3B C1 76 04", -5));
});

static hook::cdecl_stub<bool(rage::datBitBuffer*, uint64_t, int)> datBitBuffer__writeWord([]()
{
	return hook::get_call(hook::get_pattern("E8 ? ? ? ? 0F B7 47 ? 66 FF C8"));
});

static hook::cdecl_stub<bool(rage::datBitBuffer*, int*, int, int)> datBitBuffer__readBits([]()
{
	return hook::get_pattern("48 89 5C 24 ? 57 48 83 EC ? 41 8B F8 4C 8B D2");
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

static inline void __forceinline LogObjectIdSerialiseStack(const char* func)
{
	uintptr_t retnAddress = (uintptr_t)_ReturnAddress();
	if (g_accessedAddress.find(retnAddress) == g_accessedAddress.end())
	{
		trace("%s: %p\n", func, (void*)hook::get_unadjusted(_ReturnAddress()));
		uintptr_t* traceStart = (uintptr_t*)_AddressOfReturnAddress();
		for (int i = 96; i > 0; i--)
		{
			uintptr_t addr = hook::get_unadjusted(traceStart[i]);
			if (addr > 0x140000000 && addr < hook::exe_end())
			{
				trace("-> %p\n", (void*)addr);
			}
		}
		trace("---------------------\n");
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
static bool (*g_origCSyncDataWriter__SerialiseObjectIdPF)(rage::CSyncDataWriter*, uint16_t*, char*, void*);
static bool CSyncDataWriter__SerialiseObjectIdPF(rage::CSyncDataWriter* self, uint16_t* objectId, char* a3, void* a4)
{
	LogObjectIdSerialise(__func__, *objectId);

	if (!g_lengthHackEnabled )
	{
		return g_origCSyncDataWriter__SerialiseObjectIdPF(self, objectId, a3, a4);
	}

	CSyncDataBase__serialiseObjectId(self, objectId, a3, a4);
	return datBitBuffer__writeWord(self->m_buffer, *objectId, kBigModeObjectIdSize);
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

	return datBitBuffer__writeWord(self->m_buffer, *objectId, kBigModeObjectIdSize);
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
	self->m_buffer->ReadInteger(&readerObjectId, kBigModeObjectIdSize);
	*objectId = readerObjectId;

	LogObjectIdSerialise(__func__, *objectId);
	CSyncDataBase__serialiseObjectId(self, objectId, a3, a4);
}

static uint16_t (*g_origCSyncDataReader__SerialiseObjectId)(rage::CSyncDataReader*, uint16_t*);
static uint16_t CSyncDataReader__SerialiseObjectId(rage::CSyncDataReader* self, uint16_t* objectId)
{
	if (!g_lengthHackEnabled)
	{
		LogObjectIdSerialise(__func__);
		return g_origCSyncDataReader__SerialiseObjectId(self, objectId);
	}

	uint32_t readerObjectId = 0;
	self->m_buffer->ReadInteger(&readerObjectId, kBigModeObjectIdSize);
	*objectId = readerObjectId;
	LogObjectIdSerialise(__func__, *objectId);
	return *objectId;
}

static void (*g_origSyncDataSizeCalculator_SerializeObjectId)(rage::CSyncDataSizeCalculator*);
static void SyncDataSizeCalculator_SerializeObjectId(rage::CSyncDataSizeCalculator* syncData)
{
	LogObjectIdSerialiseStack(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSyncDataSizeCalculator_SerializeObjectId(syncData);
	}

	syncData->m_size += kBigModeObjectIdSize;
}

static bool (*g_origSetVehicleExclusiveDriver__Write)(hook::FlexStruct*, rage::datBitBuffer*);
static bool SetVehicleExclusiveDriver__Write(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSetVehicleExclusiveDriver__Write(self, buffer);
	}

	datBitBuffer__writeWord(buffer, self->Get<uint16_t>(8), kBigModeObjectIdSize);
	return buffer->WriteInteger(self->Get<uint32_t>(0xC), 2);
}

static bool (*g_origSetLookAtEntity__Write)(hook::FlexStruct*, rage::datBitBuffer*);
static bool SetLookAtEntity__Write(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSetLookAtEntity__Write(self, buffer);
	}

	datBitBuffer__writeWord(buffer, self->Get<uint16_t>(8), kBigModeObjectIdSize);
	buffer->WriteInteger(self->Get<uint32_t>(0xC), 18);
	return buffer->WriteInteger(self->Get<uint32_t>(0x14), 0xA);
}

static bool (*g_origSetVehicleTempAction__Write)(hook::FlexStruct*, rage::datBitBuffer*);
static bool SetVehicleTempAction__Write(hook::FlexStruct* self, rage::datBitBuffer* buffer)
{
	LogObjectIdSerialise(__func__);

	if (!g_lengthHackEnabled)
	{
		return g_origSetVehicleTempAction__Write(self, buffer);
	}

	datBitBuffer__writeWord(buffer, self->Get<uint16_t>(8), kBigModeObjectIdSize);
	buffer->WriteInteger(self->Get<uint32_t>(0xC), 8);

	bool hasTime = self->Get<bool>(0x14);
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

	datBitBuffer__writeWord(buffer, self->Get<uint16_t>(0x8), kBigModeObjectIdSize);
	datBitBuffer__writeWord(buffer, self->Get<uint16_t>(0xA), kBigModeObjectIdSize);
	buffer->WriteUns(self->Get<uint8_t>(0xC), 6);
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

	if (self->Get<uint8_t>(0xE))
	{
		buffer->WriteBit(self->Get<bool>(0x1A));
		if (self->Get<bool>(0x1A))
		{
			datBitBuffer__writeWord(buffer, self->Get<uint16_t>(0x18), kBigModeObjectIdSize);
		}
	}
}

static void* (*g_initNetIdmapping)(void*);
static void* initNetIdMapping(void* self)
{
	memset(self, 0xCD, 64224);
	return nullptr;
}

static void (*g_CSyncDataWriter__SerialisePlayerBitfield)(rage::CSyncDataWriter*, int*, void*, void*);
static void CSyncDataWriter__SerialisePlayerBitfield(rage::CSyncDataWriter* self, int* bitset, void* a3, void* a4)
{
	if (!g_lengthHackEnabled)
	{
		return g_CSyncDataWriter__SerialisePlayerBitfield(self, bitset, a3, a4);
	}

	// In onesync, 16 and 31 are the two ids that will ever be serialised.
	constexpr int kValidPlayerBits = (1 << 16) | (1 << 31);
	int serialised[1]{ bitset[0] & kValidPlayerBits };

	self->m_buffer->WriteBits(serialised, 0x20, 0);
}

static void* (*g_CSyncDataReader__SerialisePlayerBitfield)(rage::CSyncDataReader*, int*, void*, void*);
static void* CSyncDataReader__SerialisePlayerBitfield(rage::CSyncDataReader* self, int* bitset, void* a3, void* a4)
{
	if (!g_lengthHackEnabled)
	{
		return g_CSyncDataReader__SerialisePlayerBitfield(self, bitset, a3, a4);
	}

	int bitfield[1];
	datBitBuffer__readBits(self->m_buffer, bitfield, 0x20, 0);

	// Keep 16 and 31 only in OneSync
	constexpr int kValidPlayerBits = (1 << 16) | (1 << 31);
	bitset[0] |= bitfield[0] & kValidPlayerBits;
}

static void SetObjectIdSize(bool bigMode)
{
	uint32_t bits = bigMode ? kBigModeObjectIdSize : kDefaultObjectIdSize;

	static auto gameScriptIdWrite = hook::get_pattern("41 B8 ? ? ? ? 48 8B CF E8 ? ? ? ? 84 C0 0F 85", 2);
	static auto gameScriptIdRead = hook::get_pattern("41 B8 ? ? ? ? 48 8D 55 ? E8 ? ? ? ? 84 C0 75 ? B8", 2);
	static auto vehComponentControlWrite = hook::get_pattern("41 B8 ? ? ? ? 48 8B CF E8 ? ? ? ? 48 8B 5C 24 ? 48 83 C4 ? 5F C3 4C 8B DC", 2);

	hook::put<uint32_t>(gameScriptIdWrite, bits);
	hook::put<uint32_t>(gameScriptIdWrite, bits);
	hook::put<uint32_t>(vehComponentControlWrite, bits);

	// TODO: Verify that these are/aren't used in OneSync
	static auto packObjectIdData = hook::get_pattern("41 B8 ? ? ? ? 49 8B CF 44 0F B7 64 44", 2);
	static auto packObjectIdCount = hook::get_pattern("41 B8 ? ? ? ? 8B D7 49 8B CF E8 ? ? ? ? 84 C0", 2);
	static auto packObjectIdCount2 = hook::get_pattern("41 B8 ? ? ? ? 0F B7 D7 49 8B CF", 2);
	hook::put<uint32_t>(packObjectIdCount, bits);
	hook::put<uint32_t>(packObjectIdData, bits);
	hook::put<uint32_t>(packObjectIdCount2, bits);
}

static HookFunction objectIdMapping([]()
{
	// DEBUG: don't initialize NetIdMapping, replace allocation with a known pattern to catch any missed uses of id mapping.
	g_initNetIdmapping = hook::trampoline(hook::get_pattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? BD ? ? ? ? 48 8B D9 8B F5"), initNetIdMapping);

	// Replace ID Mapping (& player iteration) for serialising player fields.
	g_CSyncDataWriter__SerialisePlayerBitfield = hook::trampoline(hook::get_pattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 65 4C 8B 14 25 ? ? ? ? 48 8B F9 8B 05 ? ? ? ? 45 33 DB"), CSyncDataWriter__SerialisePlayerBitfield);
	g_CSyncDataReader__SerialisePlayerBitfield = hook::trampoline(hook::get_pattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 8B 35 ? ? ? ? 48 8B E9"), CSyncDataReader__SerialisePlayerBitfield);

	{
		auto locations = hook::pattern("FF 90 ? ? ? ? 8A 48 ? 80 F9 20 73 ? 48 8B 05 ? ? ? ? 0F B6").count(7);

		for (size_t i = 0; i < locations.size(); i++)
		{
			auto location = locations.get(i).get<char>(9);

			struct : jitasm::Frontend
			{
				uintptr_t retn;
				uintptr_t retnOrig;

				void Init(uintptr_t retn, uintptr_t retnOrig)
				{
					this->retn = retn;
					this->retnOrig = retnOrig;
				}

				virtual void InternalMain() override
				{
					mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
					mov(r11b, byte_ptr[r11]);
					test(r11b, r11b);
					jz("Orig");

					L("skipIdMap");
					mov(rax, retn);
					jmp(rax);

					L("Orig");
					cmp(cl, 0x20);
					jnb("skipIdMap");

					mov(rax, retnOrig);
					jmp(rax);
				}
			} *stub = new std::remove_pointer_t<decltype(stub)>();

			stub->Init((uintptr_t)location + 0x13, (uintptr_t)location + 5);
			hook::nop(location, 5);
			hook::jump(location, stub->GetCode());
		}
	}

	hook::put<uint8_t>(hook::get_pattern("73 ? 0F B6 C8 48 8B 05 ? ? ? ? 8A 44 01 ? 3C ? 0F 85"), 0xEB); // jnb -> jmp

	{
		auto location = (char*)hook::get_pattern("41 80 FC ? 72 ? B1");
		static struct : jitasm::Frontend
		{
			uintptr_t retnLength;
			uintptr_t retnOrig;

			void Init(uintptr_t retnLength, uintptr_t retnOrig)
			{
				this->retnLength = retnLength;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("orig");

				// The game already guards this code with a 32 index check.
				movzx(ecx, r12b);

				mov(r11, retnLength);
				jmp(r11);

				L("orig");
				mov(r11, retnOrig);
				jmp(r11);
			}
		} patchWorldGridStub;
		patchWorldGridStub.Init((uintptr_t)location + 0x18, (uintptr_t)location + 0xA);

		hook::nop(location, 6);
		hook::jump(location, patchWorldGridStub.GetCode());
	}

	// Patch bubble join to prevent writing out of bounds for player objects
	{
		auto location = hook::get_pattern("44 0F B6 4E ? 0F B6 40");

		static struct : jitasm::Frontend
		{
			uintptr_t retnSuccess;
			uintptr_t retnFail;

			void Init(uintptr_t success, uintptr_t failure)
			{
				retnSuccess = success;
				retnFail = failure;
			}

			virtual void InternalMain() override
			{
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jnz("Fail");

				// Original code
				movzx(r9d, byte_ptr[rsi + 0x10]);
				movzx(eax, byte_ptr[rax + 0x20]);

				cmp(eax, 0x20);
				jge("Fail");

				L("Orig");

				mov(r11, retnSuccess);
				jmp(r11);

				L("Fail");
				mov(r11, retnFail);
				jmp(r11);
			}
		} bubbleJoinStub;

		const uintptr_t retnSuccess = (uintptr_t)location + 9;
		const uintptr_t retnFail = retnSuccess + 0x19;

		hook::nop(location, 9);
		bubbleJoinStub.Init(retnSuccess, retnFail);
		hook::jump_reg<5>(location, bubbleJoinStub.GetCode());
	}

	{
		auto location = (char*)hook::get_pattern("80 79 ? ? 73 ? 0F B6 49 ? 48 8B 05 ? ? ? ? 44 8A 0C 01");
		static struct : jitasm::Frontend
		{
			uintptr_t retn;
			uintptr_t retnOrig;

			void Init(uintptr_t retn, uintptr_t retnOrig)
			{
				this->retn = retn;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("Orig");

				L("skipIdMap");
				mov(rax, retn);
				jmp(rax);

				L("Orig");
				cmp(byte_ptr[rcx + 0x19], 0x20);
				jnb("skipIdMap");

				mov(rax, retnOrig);
				jmp(rax);
			}
		} patchStub;

		patchStub.Init((uintptr_t)location + 0x17, (uintptr_t)location + 6);

		hook::nop(location, 6);
		hook::jump(location, patchStub.GetCode());
	}

	{
		auto location = (char*)hook::get_pattern("80 79 ? ? 73 ? 0F B6 49 ? 48 8B 05 ? ? ? ? 44 8A 14 01");

		static struct : jitasm::Frontend
		{
			uintptr_t retn;
			uintptr_t retnOrig;

			void Init(uintptr_t retn, uintptr_t retnOrig)
			{
				this->retn = retn;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("Orig");

				L("skipIdMap");
				mov(rax, retn);
				jmp(rax);

				L("Orig");
				cmp(byte_ptr[rcx + 0x19], 0x20);
				jnb("skipIdMap");

				mov(rax, retnOrig);
				jmp(rax);
			}
		} patchStub;

		patchStub.Init((uintptr_t)location + 0x17, (uintptr_t)location + 6);
		hook::nop(location, 6);
		hook::jump(location, patchStub.GetCode());
	}

	{
		auto location = (char*)hook::get_pattern("80 FB ? 73 ? 48 8B 05");

		static struct : jitasm::Frontend
		{
			uintptr_t retn;
			uintptr_t retnOrig;
			uintptr_t retnOrigFail;

			void Init(uintptr_t retn, uintptr_t retnOrig)
			{
				this->retn = retn;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("Orig");

				L("skipIdMap");
				mov(rax, retn);
				jmp(rax);

				L("Orig");
				cmp(bl, 0x20);
				jnb("skipIdMap");

				mov(rax, retnOrig);
				jmp(rax);
			}
		} scriptEntCreationStub;

		scriptEntCreationStub.Init((uintptr_t)location + 0x12, (uintptr_t)location + 5);

		hook::nop(location, 5);
		hook::jump(location, scriptEntCreationStub.GetCode());
	}

	{
		auto location = (char*)hook::get_pattern("80 F9 ? 73 ? 48 8B 05 ? ? ? ? 0F B6 C9 8A 4C 01 ? 48 8B 07");

		static struct : jitasm::Frontend
		{
			uintptr_t retn;
			uintptr_t retnOrig;

			void Init(uintptr_t retn, uintptr_t retnOrig)
			{
				this->retn = retn;
				this->retnOrig = retnOrig;
			}

			virtual void InternalMain() override
			{
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("Orig");

				L("skipIdMap");
				mov(rax, retn);
				jmp(rax);

				L("Orig");
				cmp(cl, 0x20);
				jnb("skipIdMap");

				mov(rax, retnOrig);
				jmp(rax);
			}
		} draftVehCreatePedStub;
		draftVehCreatePedStub.Init((uintptr_t)location + 0x13, (uintptr_t)location + 0x5);

		hook::nop(location, 0x5);
		hook::jump(location, draftVehCreatePedStub.GetCode());
	}

	{
		auto location = (char*)hook::get_pattern("8A 40 ? 3C ? 73");

		static struct : jitasm::Frontend
		{
			uintptr_t retn;
			uintptr_t retnOrig;

			void Init(uintptr_t retn, uintptr_t retnOrig)
			{
				this->retn = retn;
				this->retnOrig = retnOrig;	
			}

			virtual void InternalMain() override
			{
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("Orig");

				L("skipIdMap");
				mov(rcx, retn);
				jmp(rcx);

				L("Orig");
				mov(al, byte_ptr[rax + 0xA]);
				cmp(al, 0x20);
				jnb("skipIdMap");

				mov(rcx, retnOrig);
				jmp(rcx);
			}
		} scriptEntityDeregisterStub;

		scriptEntityDeregisterStub.Init((uintptr_t)location + 0x15, (uintptr_t)location + 7);
		
		hook::nop(location, 7);
		hook::jump_rcx(location, scriptEntityDeregisterStub.GetCode());
	}

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
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
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
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("orig");

				movzx(edx, word_ptr[rbx + 0x18]);

				mov(r11, retn);
				jmp(r11);

				L("orig");
				// Original code, cx is used.
				mov(ecx, 7999);
				
				mov(r11, retnOrig);
				jmp(r11);
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
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
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
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
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
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("orig");
				
				// Original code
				mov(al, byte_ptr[rbx + 8]);
				cmp(al, 2); // type <= 2
				jg("fail");

				movzx(edi, word_ptr[r14]);
				mov(word_ptr[rsi + 0x5A], di); // event->m_respawnNetId = event->m_respawnNetObj 

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
				mov(r11, reinterpret_cast<uintptr_t>(&g_lengthHackEnabled));
				mov(r11b, byte_ptr[r11]);
				test(r11b, r11b);
				jz("orig");

				movzx(edx, word_ptr[rbx + 0x18]); // the serialiser reads from edx for the object id.
				mov(r11, retn);
				jmp(r11);

				L("orig");
				movzx(eax, word_ptr[rbx + 0x18]);
				mov(ecx, 7999);

				mov(r11, retnOrig);
				jmp(r11);

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

			if (g_lengthHackEnabled)
			{
				SetObjectIdSize(g_lengthHackEnabled);
			}
		});
	});

	OnKillNetworkDone.Connect([]()
	{
		g_lengthHackEnabled = false;
	});
});

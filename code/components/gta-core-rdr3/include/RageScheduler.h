#pragma once

#include <StdInc.h>
#include "ComponentExport.h"

namespace rage
{
struct sysDependencyBatch
{
	typedef bool(Callback)(rage::sysDependencyBatch*);

	struct IpcRef
	{
		volatile LONG refCount = 0;
		HANDLE event;
	};

	Callback* m_callerFunc;
	char pad[8];
	uint32_t unk_01;
	uint16_t unk_02;
	uint8_t unk_03;
	uint8_t m_unkFlags;
	char pad3[8];
	void* argument;
	IpcRef* ipcEventRef;
};

COMPONENT_EXPORT(GTA_CORE_RDR3) int InsertDependencyBatch(sysDependencyBatch** updateList, int entries);
}

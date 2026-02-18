#pragma once

#include <stdinc.h>
#include <rlNetBuffer.h>

namespace rage
{
enum eSyncDataSerializer : uint8_t
{
	SYNC_DATA_UNKNOWN = 0x0,
	SYNC_DATA_READER = 0x1,
	SYNC_DATA_WRITER = 0x2,
	SYNC_DATA_CLEANER = 0x3,
	SYNC_DATA_LOGGER = 0x4,
};

struct CSyncDataBase
{
	void* m_vtable;
	eSyncDataSerializer m_type;
	char m_pad[15];
};

struct CSyncDataReader : CSyncDataBase
{
	datBitBuffer* m_buffer;
};

struct CSyncDataWriter : CSyncDataBase
{
	datBitBuffer* m_buffer;
};

struct CSyncDataSizeCalculator : CSyncDataBase
{
	uint32_t m_size;
};

}

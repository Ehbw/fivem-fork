#pragma once

struct Mutex
{
	_RTL_CRITICAL_SECTION* m_lock;
	Mutex(_RTL_CRITICAL_SECTION* lock)
		: m_lock(lock)
	{
		if (m_lock && m_lock->DebugInfo)
		{
			EnterCriticalSection(m_lock);
		}
	}
	~Mutex()
	{
		if (m_lock && m_lock->DebugInfo)
		{
			LeaveCriticalSection(m_lock);
		}
	}
};

#pragma once

struct Mutex
{
	_RTL_CRITICAL_SECTION* m_lock;
	bool m_isLocked;

	Mutex(_RTL_CRITICAL_SECTION* lock)
		: m_lock(lock), m_isLocked(true)
	{
		if (m_lock && m_lock->DebugInfo)
		{
			EnterCriticalSection(m_lock);
		}
	}

	void Unlock()
	{
		if (m_isLocked && m_lock && m_lock->DebugInfo)
		{
			m_isLocked = false;
			LeaveCriticalSection(m_lock);
		}
	}

	~Mutex()
	{
		if (m_isLocked && m_lock && m_lock->DebugInfo)
		{
			m_isLocked = false;
			LeaveCriticalSection(m_lock);
		}
	}
};

#include "StdInc.h"

#include "atArray.h"

#include <Hooking.h>
#include <MinHook.h>
#include <bitset>

class CTrain
{
public:
	inline static uint8_t kDirectionFlagMask = 8;

	inline static ptrdiff_t kCruiseSpeedOffset = 5392;
	inline static ptrdiff_t kSpeedOffset = 5396;
	inline static ptrdiff_t kTrainFlagsOffset = 5468;
public:

	inline bool GetDirection()
	{
		auto location = reinterpret_cast<uint8_t*>(this) + kTrainFlagsOffset;
		int result = ((*location) & kDirectionFlagMask);
		return (*(BYTE*)(this + kTrainFlagsOffset) & kDirectionFlagMask) != 0;
	}

	inline void SetDirection()
	{
		auto location = reinterpret_cast<uint8_t*>(this) + kTrainFlagsOffset;
		*(BYTE*)(this + kTrainFlagsOffset) ^= (*(BYTE*)(this + kTrainFlagsOffset) ^ (kDirectionFlagMask * ~(*(BYTE*)(this + kTrainFlagsOffset) >> 3))) & kDirectionFlagMask;
	}
};

static void (*g_CTrain__SetSpeed)(CTrain*, float);
static void CTrain__SetSpeed(CTrain* train, float speed)
{
	// Negative speeds have a bad effect on MP
	if (speed < 0.f)
	{
		speed = abs(speed);
		train->SetDirection();
	}

	trace("CTrain::SetSpeed %.3f\n", speed);
	g_CTrain__SetSpeed(train, speed);
}

static void (*g_CTrain__SetCruiseSpeed)(CTrain*, float);
static void CTrain__SetCruiseSpeed(CTrain* train, float speed)
{
	// Negative speeds have a bad effect on MP
	if (speed < 0.f)
	{
		speed = abs(speed);
		train->GetDirection();
		train->SetDirection();
	}

	trace("CTrain::SetCruiseSpeed %.3f\n", speed);
	g_CTrain__SetCruiseSpeed(train, speed);
}

static HookFunction hookFunction([]()
{
	{
		MH_Initialize();
		MH_CreateHook(hook::get_pattern("F3 0F 11 89 ? ? ? ? C3 90 ED"), CTrain__SetCruiseSpeed, (void**)&g_CTrain__SetCruiseSpeed);
		MH_CreateHook(hook::get_pattern("F6 81 ? ? ? ? ? 75 ? 0F 57 0D"), CTrain__SetSpeed, (void**)&g_CTrain__SetSpeed);
		MH_EnableHook(MH_ALL_HOOKS);
	}
});

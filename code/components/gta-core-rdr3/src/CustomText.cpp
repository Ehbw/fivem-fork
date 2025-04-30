/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "Hooking.h"
#include "MinHook.h"

#include <mutex>
#include <stack>

#include <boost/optional.hpp>

#include <CustomText.h>

static const char* (*g_origGetText)(void* theText, int a2, uint32_t* hash, int* a4);

static std::mutex g_textMutex;
static std::unordered_map<uint32_t, std::string> g_textMap;

static const char* GetText(void* theText, int a2, uint32_t* hash, int* a4)
{
	{
		std::unique_lock<std::mutex> lock(g_textMutex);

		auto it = g_textMap.find(*hash);

		if (it != g_textMap.end())
		{
			return it->second.c_str();
		}
	}

	return g_origGetText(theText, a2, hash, a4);
}

void AddCustomText(const char* key, const char* value)
{
	std::unique_lock<std::mutex> lock(g_textMutex);
	g_textMap[HashString(key)] = value;
}

namespace game
{
	void AddCustomText(const std::string& key, const std::string& value)
	{
		AddCustomText(HashString(key.c_str()), value);
	}

	void AddCustomText(uint32_t hash, const std::string& value)
	{
		std::unique_lock<std::mutex> lock(g_textMutex);
		g_textMap[hash] = value;
	}

	void RemoveCustomText(uint32_t hash)
	{
		std::unique_lock<std::mutex> lock(g_textMutex);

		g_textMap.erase(hash);
	}
}


static const char*(*g_getInputName)(uint32_t);
static const char* GetInputName(uint32_t input)
{
	const char* inputStr = g_getInputName(input);
	trace("input str %i %s\n", input, inputStr);
	return "hahahahahahahahahahhahahahahaha!";
}

static HookFunction hookFunction([]()
{
	g_textMap[0xB3390E91] = "Build ~1~ (RedM)";

	MH_Initialize();
	MH_CreateHook(hook::get_pattern("48 81 C1 D0 00 00 00 E8 ? ? ? ? 48 8B CE 83 FB FF 74 ? 44 8B 44 24 50", -57), GetText, (void**)&g_origGetText);
	//MH_CreateHook(hook::get_pattern("48 63 D1 48 8D 0D ? ? ? ? E9"), GetInputName, (void**)&g_getInputName);
	MH_EnableHook(MH_ALL_HOOKS);
});

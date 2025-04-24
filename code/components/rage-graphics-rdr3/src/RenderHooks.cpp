#include "StdInc.h"

#include <array>
#include <mutex>

#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <vulkan/vulkan.h>

#include <Hooking.h>
#include <MinHook.h>

#include <Error.h>

#include <CrossBuildRuntime.h>
#include <CL2LaunchMode.h>

#include <CoreConsole.h>
#include <ICoreGameInit.h>

#include <HostSharedData.h>
#include <d3dcommon.h>
#include <d3d12.h>

#include <optional>

#pragma comment(lib, "vulkan-1.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

namespace WRL = Microsoft::WRL;
static void DXGIGetHighPerfAdapter(IDXGIAdapter** ppAdapter)
{
	{
		WRL::ComPtr<IDXGIFactory1> dxgiFactory;
		CreateDXGIFactory1(IID_IDXGIFactory1, &dxgiFactory);

		WRL::ComPtr<IDXGIAdapter1> adapter;
		WRL::ComPtr<IDXGIFactory6> factory6;
		HRESULT hr = dxgiFactory.As(&factory6);
		if (SUCCEEDED(hr))
		{
			for (UINT adapterIndex = 0;
			DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
			adapterIndex++)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					// Don't select the Basic Render Driver adapter.
					continue;
				}

				static auto _ = ([&desc]
				{
					AddCrashometry("gpu_name", "%s", ToNarrow(desc.Description));
					AddCrashometry("gpu_id", "%04x:%04x", desc.VendorId, desc.DeviceId);
					return true;
				})();

				adapter.CopyTo(ppAdapter);
				break;
			}
		}
	}
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	std::string prefix;

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
	{
		prefix = "VERBOSE: ";
		//prefix = "\033[32m" + prefix + "\033[0m";
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
	{
		prefix = "INFO: ";
		//prefix = "\033[36m" + prefix + "\033[0m";
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		prefix = "WARNING: ";
		//prefix = "\033[33m" + prefix + "\033[0m";
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		prefix = "ERROR: ";
		//prefix = "\033[31m" + prefix + "\033[0m";
	}

	trace("%s [%i][%s]: %s\n", prefix, pCallbackData->messageIdNumber, pCallbackData->pMessageIdName, pCallbackData->pMessage);
	return VK_FALSE;
}  

const std::vector<const char*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> extensionNames = {
	"VK_EXT_DEBUG_UTILS_EXTENSION_NAME"
};

static VkResult __stdcall vkCreateInstanceHook(VkInstanceCreateInfo* pCreateInfo, VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
	trace("Vulkan hook\n");
	/* pCreateInfo->enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	pCreateInfo->ppEnabledLayerNames = validationLayers.data();

	pCreateInfo->enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
	pCreateInfo->ppEnabledExtensionNames = extensionNames.data();

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = DebugMessageCallback;

	pCreateInfo->pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;*/
	VkResult result = vkCreateInstance(pCreateInfo, pAllocator, pInstance);

	return result;
}

static HRESULT (*g_origVkCreateDeviceHook)(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
static HRESULT vkCreateDeviceHook(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
	trace("vkCreateDevice\n");
	pCreateInfo->enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	pCreateInfo->ppEnabledLayerNames = validationLayers.data();

	return g_origVkCreateDeviceHook (physicalDevice, pCreateInfo, pAllocator, pDevice);
}

static HRESULT D3D12CreateDeviceWrap(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice)
{
	trace("D3D12 Hook\n");
	HRESULT hr = D3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);

	if (FAILED(hr))
	{
		trace("failed to create D3D12\n");
	}

	return hr;
}

bool stopLogging = false;
static int64_t LogGraphic(void* a1, const char* format, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, format);
	if (stopLogging || strcmp(format, "{") == 0)
	{
		stopLogging = true;
		return 0;
	}

	vsprintf(buffer, format, args);
	va_end(args);
	console::DPrintf("graphics", "%s\n", buffer);
	return 0;
}

char (*g_ragesgaDriverVKInitFactory)(void* a1);
char __fastcall ragesgaDriverVKInitFactory(void* a1)
{
	trace("g_ragesgaDriverVKInitFactory \n");
	return g_ragesgaDriverVKInitFactory(a1);
}

static HookFunction hookFunction([]()
{
	// Vulkan create instance hook
	{
		auto location = hook::get_pattern("FF D0 8B F0 89 84 24", -7);
		hook::nop(location, 9);
		hook::call(location, vkCreateInstanceHook);
	}

	// Vulkan CreateDevice hook
	{
		auto location = hook::get_pattern("4C 89 A5 ? ? ? ? FF 15", 6);
		hook::nop(location, 5);
		hook::call(location, vkCreateDeviceHook);
	}

	// D3D12 CreateDevice Hook
	{
		auto location = hook::get_pattern("FF 15 ? ? ? ? 85 C0 0F 88 ? ? ? ? 48 8B 8D");
		hook::nop(location, 6);
		hook::call(location, D3D12CreateDeviceWrap);
	}

	// no ShowWindow early
	{
		auto location = hook::get_pattern("0F 45 D1 48 8B CB FF 15");
		hook::nop(location, 12);
	}

	g_origVkCreateDeviceHook = hook::iat("vulkan-1.dll", vkCreateDeviceHook, "vkCreateDevice");

	// Output Debug Print 
#if 0
	MH_Initialize();
	MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? 48 8B CB 4D 8B C7")), LogGraphic, nullptr);
	MH_EnableHook(MH_ALL_HOOKS);
#endif
});

#include <StdInc.h>

#include <HostSharedData.h>
#include <CfxState.h>

#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <wrl.h>

#include <chrono>

#include <CefOverlay.h>

///
/// Handle external frame updates inline with vsync (through WaitForVBlank)
/// This was apart originally intergrated into cfx-M103 on top of the shared texture patches
/// But its more reasonable and maintainable with it being done here, in NUI.
/// This is based on the original implementation from https://github.com/citizenfx/fivem/commit/150691142e646a3bb7fd1f5c8e977a9d24379aac
///
extern nui::GameInterface* g_nuiGi;
fwEvent<std::chrono::microseconds, std::chrono::microseconds> OnVSync;

// TODO: replace with concepts when upgrading to M145+
namespace fx::traits
{
template<typename T>
struct is_d3d_device : std::bool_constant<
    std::is_base_of_v<ID3D11Device, std::remove_pointer_t<T>> ||
    std::is_base_of_v<ID3D12Device, std::remove_pointer_t<T>>> {};

template<typename T>
inline constexpr bool is_d3d_device_v = is_d3d_device<T>::value;
}

namespace WRL = Microsoft::WRL;
inline static WRL::ComPtr<IDXGIAdapter> AdapterFromDevice(ID3D11Device* device)
{
	WRL::ComPtr<IDXGIDevice> dxgiDevice;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
	{
		return nullptr;
	}

	WRL::ComPtr<IDXGIAdapter> adapter;
	if (FAILED(dxgiDevice->GetAdapter(&adapter)))
	{
		return nullptr;
	}

	return adapter;
}

inline static WRL::ComPtr<IDXGIAdapter> AdapterFromDevice(ID3D12Device* device)
{
	WRL::ComPtr<IDXGIFactory4> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
	{
		return nullptr;
	}

	WRL::ComPtr<IDXGIAdapter> adapter;
	if (FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))))
	{
		return nullptr;
	}

	return adapter;
}

template<typename D3DDevice, std::enable_if_t<fx::traits::is_d3d_device_v<D3DDevice>, int> = 0>
WRL::ComPtr<IDXGIOutput> DXGIOutputFromMonitor(HMONITOR monitor, D3DDevice* device)
{
	auto adapter = AdapterFromDevice(device);
	if (!adapter)
	{
		return nullptr;
	}

	UINT i = 0;
	while (true)
	{
		WRL::ComPtr<IDXGIOutput> output;
		if (FAILED(adapter->EnumOutputs(i++, &output)))
		{
			break;
		}

		DXGI_OUTPUT_DESC desc = {};
		if (FAILED(output->GetDesc(&desc)))
		{
			return nullptr;
		}

		if (desc.Monitor == monitor)
		{
			return output;
		}
	}
	return nullptr;
}

static InitFunction postInitFunction([]()
{
	static HostSharedData<CfxState> initState("CfxInitState");
	if (!initState->IsMasterProcess() && !initState->IsGameProcess())
	{
		return;
	}

	std::thread([]()
	{
		SetThreadName(-1, "[NUI] vSync update");
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

		HMONITOR primaryMonitor = nullptr;
		WRL::ComPtr<IDXGIOutput> primaryOutput;
		std::chrono::microseconds interval (int64_t(1000000.0 / 60));
		constexpr const auto kVBlankIntervalThreshold = std::chrono::milliseconds(1);

		auto getDevice = []()
		{
#ifdef IS_RDR3
			return g_nuiGi->GetD3D12Device();
#else
			return g_nuiGi->GetD3D11Device();
#endif
		};

		while (true)
		{
			if (!g_nuiGi || !getDevice()
#ifdef IS_RDR3
				|| g_nuiGi->GetVulkanDevice()
#endif
			)
			{
#ifdef IS_RDR3
				// Vulkan will be treated the same as DUI. as it doesn't behave the same as DirectX
				// and therefore is completely incompatable with this logic.
				if (g_nuiGi && g_nuiGi->GetVulkanDevice())
				{
					return;
				}
#endif

				auto vsyncTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
				OnVSync(vsyncTime, std::chrono::milliseconds(50));

				Sleep(50);
				continue;
			}

			POINT pt = { 0, 0 };
			HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
			if (primaryMonitor != monitor)
			{
				primaryMonitor = monitor;
				primaryOutput = DXGIOutputFromMonitor(monitor, getDevice());

				MONITORINFOEX monitorInfo = {};
				monitorInfo.cbSize = sizeof(MONITORINFOEX);
				if (monitor && GetMonitorInfo(monitor, &monitorInfo))
				{
					DEVMODE displayInfo = {};
					displayInfo.dmSize = sizeof(DEVMODE);
					displayInfo.dmDriverExtra = 0;
					if (EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &displayInfo) && displayInfo.dmDisplayFrequency > 1)
					{
						interval = std::chrono::microseconds(int64_t(1000000.0 / displayInfo.dmDisplayFrequency));
					}
				}
			}

			// WaitForVBlank returns very early instead of waiting until vblank when the
			// monitor goes to sleep.  We use 1ms as a threshold for the duration of
			// WaitForVBlank and fallback to Sleep() if it returns before that.  This
			// could happen during normal operation for the first call after the vsync
			// thread becomes non-idle, but it shouldn't happen often.
			std::chrono::steady_clock::duration blankStart = std::chrono::high_resolution_clock::now().time_since_epoch();
			bool success = primaryOutput && SUCCEEDED(primaryOutput->WaitForVBlank());
			std::chrono::steady_clock::duration blankElapsed = std::chrono::high_resolution_clock::now().time_since_epoch() - blankStart;

			if (!success || blankElapsed < kVBlankIntervalThreshold)
			{
				Sleep(static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(interval).count()));
			}

			auto vsyncTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
			OnVSync(vsyncTime, std::chrono::duration_cast<std::chrono::microseconds>(interval));
		}
	}).detach();
},
INT32_MAX);

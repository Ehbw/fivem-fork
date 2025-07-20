#include <StdInc.h>
#include "ConsoleHostWrapper.h"

#include <imgui.h>
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#include <CrossBuildRuntime.h>
#include <DrawCommands.h>
#include <ImGuiTextureHelper.h>

#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// for titles that don't/can't use their rendering for ImGui
#ifdef USE_SHARED_DLL
static ID3D11Device* device;
static ID3D11DeviceContext* immcon;
static IDXGISwapChain* g_pSwapChain = nullptr;

extern ID3D11DeviceContext* g_pd3dDeviceContext;
#endif

#if 0
static ID3D12DescriptorHeap* g_srvHeap; 
static ID3D12DescriptorHeap* g_rtvHeap;
static D3D12HeapAllocator g_srvHeapAlloc;
static D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[2] = {};
static ID3D12CommandAllocator* g_commandAllocator;
static ID3D12GraphicsCommandList* g_commandList;

static void InitD3D12Impl()
{
	ID3D12Device* device = (ID3D12Device*)GetGraphicsDriverHandle();
	ID3D12CommandQueue* queue = (ID3D12CommandQueue*)GetD3D12CommandQueue();

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&g_rtvHeap));

    SIZE_T rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < 2; i++)
	{
		g_mainRenderTargetDescriptor[i] = rtvHandle;
		rtvHandle.ptr += rtvDescriptorSize;
	}

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 64;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_srvHeap)) != S_OK)
	{
		trace("Failed to create descriptor heap for srvHeap\n");
		return;

	}
	g_srvHeapAlloc.Create(device, g_srvHeap);

	if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator)) != S_OK)
	{
		trace("Failed to create command allocator\n");
		return;
	}

	if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator, nullptr, IID_PPV_ARGS(&g_commandList)) != S_OK)
	{
		trace("Failed to create command list\n");
		return;
	}

    ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = device;
	initInfo.CommandQueue = queue;
	initInfo.NumFramesInFlight = 1;
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;

    initInfo.SrvDescriptorHeap = g_srvHeap;
	initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
	{
		return g_srvHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle);
	};
	initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
	{
		return g_srvHeapAlloc.Free(cpu_handle, gpu_handle);
	};

	ImGui_ImplDX12_Init(&initInfo);
}
#endif

#ifdef USE_SHARED_DLL
void ConHost::SetupSharedDevice()
{
	// use the system function as many proxy DLLs don't like multiple devices being made in the game process
	// and they're 'closed source' and 'undocumented' so we can't reimplement the same functionality natively

	// also, create device here and not after the game's or nui:core hacks will mismatch with proxy DLLs
#if 0
	wchar_t systemD3D11Name[512];
	GetSystemDirectoryW(systemD3D11Name, std::size(systemD3D11Name));
	wcscat(systemD3D11Name, L"\\d3d11.dll");

	auto systemD3D11 = LoadLibraryW(systemD3D11Name);
	assert(systemD3D11);

    DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = CoreGetGameWindow();
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	auto systemD3D11CreateDeviceAndSwapChain = (decltype(&D3D11CreateDeviceAndSwapChain))GetProcAddress(systemD3D11, "D3D11CreateDeviceAndSwapChain");

    UINT createDeviceFlags = 0;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = {
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0
	};

	HRESULT res = systemD3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_SOFTWARE, nullptr, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &device, &featureLevel, &immcon);
	if (res != S_OK)
	{
		trace("bad\n");
		return;
	}
#endif
	// also, create device here and not after the game's or nui:core hacks will mismatch with proxy DLLs
	wchar_t systemD3D11Name[512];
	GetSystemDirectoryW(systemD3D11Name, std::size(systemD3D11Name));
	wcscat(systemD3D11Name, L"\\d3d11.dll");

	auto systemD3D11 = LoadLibraryW(systemD3D11Name);
	assert(systemD3D11);

	auto systemD3D11CreateDevice = (decltype(&D3D11CreateDevice))GetProcAddress(systemD3D11, "D3D11CreateDevice");

	systemD3D11CreateDevice(NULL,
	D3D_DRIVER_TYPE_HARDWARE,
	nullptr,
	0,
	nullptr,
	0,
	D3D11_SDK_VERSION,
	&device,
	nullptr,
	&immcon);
}
#endif

void ConHost::InitPlatform()
{
	// Initalize Win32 rendering for all platforms
	ImGui_ImplWin32_Init(CoreGetGameWindow());

	// If the game natively uses D3D11 use that device.
#ifndef USE_SHARED_DLL
	struct
	{
		void* vtbl;
		ID3D11Device* rawDevice;
	}* deviceStuff = (decltype(deviceStuff))GetD3D11Device();

	// Initalize D3D11 Device using game's D3D11 Device
	ImGui_ImplDX11_Init(deviceStuff->rawDevice, GetD3D11DeviceContext());
#elif defined(USE_SHARED_DLL)
	ConHost::SetupSharedDevice();
	if (device && immcon)
	{
		ImGui_ImplDX11_Init(device, immcon);
	}
#endif
}

void ConHost::PlatformNewFrame()
{
#if defined(GTA_FIVE) || defined(USE_SHARED_DLL)
	ImGui_ImplDX11_NewFrame();
#endif	
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ConHost::PlatformRender()
{
	ImGuiIO& io = ImGui::GetIO();

#ifdef USE_SHARED_DLL
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	g_pd3dDeviceContext->OMGetRenderTargets(1, &oldRTV, &oldDSV);
#endif

	ImGui::Render();
#ifdef USE_SHARED_DLL
	static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };


#endif
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();

#ifdef USE_SHARED_DLL
	g_pd3dDeviceContext->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldRTV)
	{
		oldRTV->Release();
		oldRTV = nullptr;
	}

	if (oldDSV)
	{
		oldDSV->Release();
		oldDSV = nullptr;
	}
#endif
}

void ConHost::RenderGrcImage(void* textureId, void* graphicsDevice)
{
	// Get grcTexture
	rage::grcTexture* texture = ConHost::ImGuiGrcTexture::ToGrcTexture(textureId);

	if (!texture)
	{
		SetTextureGtaIm(rage::grcTextureFactory::GetNoneTexture());
		return;
	}

#ifdef GTA_FIVE
	ID3D11DeviceContext* device = reinterpret_cast<ID3D11DeviceContext*>(graphicsDevice);
	ID3D11ShaderResourceView* texture_srv = (ID3D11ShaderResourceView*)texture->srv;
	device->PSSetShaderResources(0, 1, &texture_srv);
#endif
}

rage::grcTexture* ConHost::ImGuiGrcTexture::ToGrcTexture(void* textureId)
{
	if (!textureId)
	{
		return nullptr;
	}

	// is the instance passed to this a grcTexture. If so we can directly access what we need
	if (IsTexture(textureId))
	{
		return reinterpret_cast<rage::grcTexture*>((reinterpret_cast<ImGuiGrcTexture*>(textureId)->gameTexture));
	}

	return nullptr;
}

#ifdef GTA_FIVE
void* ConHost::ImGuiGrcTexture::ToImGuiCompatableFormat(void* textureId)
{
	if (!textureId)
	{
		return nullptr;
	}

	if (IsTexture(textureId))
	{
		return reinterpret_cast<ID3D11ShaderResourceView*>(reinterpret_cast<ImGuiGrcTexture*>(textureId)->externalTexture);
	}

	return nullptr;
}

#endif

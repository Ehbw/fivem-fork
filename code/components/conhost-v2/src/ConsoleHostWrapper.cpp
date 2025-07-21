#include <StdInc.h>
#include "ConsoleHostWrapper.h"

#include <imgui.h>
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#include <CrossBuildRuntime.h>
#include <DrawCommands.h>
#include <ImGuiTextureHelper.h>

#include "backends/imgui_impl_win32.h"
#if defined(GTA_FIVE) || defined(USE_SHARED_DLL)
#include "backends/imgui_impl_dx11.h"
#elif IS_RDR3
#include "backends/imgui_impl_dx12.h"
#endif
// for titles that don't/can't use their rendering for ImGui
#if defined(IS_RDR3) && !defined(USE_SHARED_DLL)
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

	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator));
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator, nullptr, IID_PPV_ARGS(&g_commandList));
	g_commandList->Close(); // Must be closed initially

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
	ImGui_ImplDX12_CreateDeviceObjects();

}
#endif

void ConHost::InitPlatform()
{
	// Initalize Win32 rendering for all platforms
	ImGui_ImplWin32_Init(CoreGetGameWindow());

	// If the game natively uses D3D11 use that device.
#ifdef GTA_FIVE
	struct
	{
		void* vtbl;
		ID3D11Device* rawDevice;
	}* deviceStuff = (decltype(deviceStuff))GetD3D11Device();

	// Initalize D3D11 Device using game's D3D11 Device
	ImGui_ImplDX11_Init(deviceStuff->rawDevice, GetD3D11DeviceContext());
#elif defined(USE_SHARED_DLL)

#elif IS_RDR3
	if (GetCurrentGraphicsAPI() == GraphicsAPI::D3D12)
	{
		InitD3D12Impl();
	}
	else
	{
	}
#endif
}

void ConHost::PlatformNewFrame()
{
#ifdef GTA_FIVE
	ImGui_ImplDX11_NewFrame();
#elif defined(IS_RDR3)
	g_commandAllocator->Reset();
	g_commandList->Reset(g_commandAllocator, nullptr);
	ImGui_ImplDX12_NewFrame();
#endif
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ConHost::PlatformRender()
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui::Render();
#if defined(GTA_FIVE) || defined(USE_SHARED_DLL)
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData())
#elif defined(IS_RDR3)
	ID3D12DescriptorHeap* heaps[] = { g_srvHeap };
	g_commandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList);
	g_commandList->Close();
	((ID3D12CommandQueue*)GetD3D12CommandQueue())->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_commandList);
#endif
	ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
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

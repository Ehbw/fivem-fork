#include <StdInc.h>
#include "ConsoleHostWrapper.h"

#ifdef GTA_FIVE
#define IMGUI_D3D11_IMPL
#elif IS_RDR3
#define IMGUI_D3D12_IMPL
#define IMGUI_VULKAN_IMPL
#elif GTA_NY
#define IMGUI_D3D9_IMPL
#else
#error "No supported ImGUI backend Impl"
#endif

#include <imgui.h>

#include <CrossBuildRuntime.h>
#include <DrawCommands.h>
#include <ImGuiTextureHelper.h>

#include "backends/imgui_impl_win32.h"
// Load backends based on supported games
#ifdef IMGUI_D3D11_IMPL
#include "backends/imgui_impl_dx11.h"
#endif
#ifdef IMGUI_D3D12_IMPL
#include "backends/imgui_impl_dx12.h"
#endif
#ifdef IMGUI_VULKAN_IMPL
#endif

void ConHost::InitPlatform()
{
	// Initalize Win32 rendering for all platforms
	ImGui_ImplWin32_Init(CoreGetGameWindow());

#ifdef GTA_FIVE
	struct
	{
		void* vtbl;
		ID3D11Device* rawDevice;
	}* deviceStuff = (decltype(deviceStuff))GetD3D11Device();

	// Initalize D3D11 Device using game's D3D11 Device
	ImGui_ImplDX11_Init(deviceStuff->rawDevice, GetD3D11DeviceContext());
#endif

#ifdef IS_RDR3
	// DX12 setup
	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = g_pd3dDevice;
	initInfo.CommandQueue = g_pd3dCommandQueue;
	initInfo.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;

	initInfo.SrvDescriptorHeap = g_pd3dSrvDescHeap;

	initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
	{
		return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle);
	};

	initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
	{
		return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle);
	};

	ImGui_ImplDX12_Init(&init_info);
#endif
}

void ConHost::PlatformNewFrame()
{
#ifdef GTA_FIVE
	ImGui_ImplDX11_NewFrame();
#elif IS_RDR3
	ImGui_ImplDX12_NewFrame();
#endif

	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ConHost::PlatformRender()
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui::Render();

#ifndef _HAVE_GRCORE_NEWSTATES
	SetRenderState(0, grcCullModeNone);
	SetRenderState(2, 0); // alpha blending m8

	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	GetD3D9Device()->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
#else
	auto oldRasterizerState = GetRasterizerState();
	SetRasterizerState(GetStockStateIdentifier(RasterizerStateNoCulling));

	auto oldBlendState = GetBlendState();
	SetBlendState(GetStockStateIdentifier(BlendStateDefault));

	auto oldDepthStencilState = GetDepthStencilState();
	SetDepthStencilState(GetStockStateIdentifier(DepthStencilStateNoDepth));
#endif

#ifdef GTA_FIVE
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#elif IS_RDR3

#endif

#ifdef _HAVE_GRCORE_NEWSTATES
	SetRasterizerState(oldRasterizerState);

	SetBlendState(oldBlendState);

	SetDepthStencilState(oldDepthStencilState);
#else
	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);
	GetD3D9Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
	GetD3D9Device()->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
#endif

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
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

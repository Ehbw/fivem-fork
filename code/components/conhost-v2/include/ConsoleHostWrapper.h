#pragma once

// Wrapper to properly handle multiple renders for ImGui across GTA V and RDR3
// DX11, DX12 and Vulkan

namespace ConHost
{
	// For ImGUI initalization for Win32 render and the games specific DX/Vulkan runtime
	void InitPlatform();

	// For new ImGUI frame for Win32, ImGui and the games specific DX/Vulkan runtime
	void PlatformNewFrame();

	// For rendering ImGUI data for Win32, ImGui, the games specific DX/Vulkan runtime
	void PlatformRender();

	// Special case for rendering textures that were created with grcTexture
	void RenderGrcImage(void*, void*);
}

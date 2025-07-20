#pragma once
#include <cstdint>

// Wrapper around some of ImGui texture logic in order to convert usages of grcTexture to an appropriate DX11/DX12/Vulkan format to work in ImGui
#ifdef GTA_FIVE
struct ID3D11ShaderResourceView;
#endif

namespace rage
{
class grcTexture;
}

namespace ConHost
{
struct ImGuiGrcTexture
{
	static constexpr const uint32_t Magic = 'IMTX';
	uint32_t magic = Magic;
	void* gameTexture = nullptr;
	void* externalTexture = nullptr;

	inline explicit ImGuiGrcTexture(void* gameTexture, void* externalTexture)
		: gameTexture(gameTexture), externalTexture(externalTexture)
	{

	}

	static inline bool IsTexture(void* ptr)
	{
		return ptr && *reinterpret_cast<const uint32_t*>(ptr) == Magic;
	}

	static rage::grcTexture* ToGrcTexture(void* textureId);

	// in GTA V, ID3D11ShaderResourceView
	// in RDR3 D3D12, UNK
	// in RDR3 Vulkan, UNK
	// in GTA IV D3D9,
#ifdef GTA_FIVE
	static void* ToImGuiCompatableFormat(void* textureId);
#endif
};

inline ImGuiGrcTexture* MakeImGuiTexture(rage::grcTexture* gameTexture)
{
	return new ImGuiGrcTexture(gameTexture, nullptr);
}

inline ImGuiGrcTexture* MakeImGuiTexture(rage::grcTexture* gameTexture, void* extTexture)
{
	return new ImGuiGrcTexture(gameTexture, extTexture);
}

}

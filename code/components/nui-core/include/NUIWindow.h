/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#pragma once

#include <queue>
#include <shared_mutex>

#include <include/cef_client.h>
#include <include/internal/cef_types.h>
#include <include/cef_v8.h>

#include <concurrent_queue.h>

#include <wrl.h>

enum NUIPaintType
{
	NUIPaintTypeDummy,
	NUIPaintTypePostRender
};

#include <CefOverlay.h>

class
#ifdef COMPILING_NUI_CORE
	__declspec(dllexport)
#endif
	NUIWindow : public fwRefCountable
{
private:
	CefRefPtr<CefClient> m_client;

	void(__cdecl* m_onClientCreated)(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);

	void Initialize(CefString url);

	concurrency::concurrent_queue<std::function<void()>> m_onLoadQueue;

public:
	NUIWindow(bool primary, int width, int height, const std::string& windowContext);

private:
	// Tied to CefRenderHandler::PaintElementType
	static constexpr int kMaxPaintElements = 2;

	std::string m_windowContext;

	bool m_isPrimary;
	int m_width;
	int m_height;

	int m_roundedWidth;
	int m_roundedHeight;

	uint32_t m_lastFrameTime;
	uint32_t m_lastMessageTime;

	unsigned long m_dirtyFlag;

	bool m_usingSharedTextures;
	RECT m_lastDirtyRect;
	std::shared_mutex m_renderBufferLock;
	char* m_renderBuffer;

	std::queue<CefRect> m_dirtyRects;

	std::set<std::string> m_pollQueue;

	fwRefContainer<nui::GITexture> m_nuiTexture;

	fwRefContainer<nui::GITexture> m_popupTexture;

	NUIPaintType m_paintType;

	fwRefContainer<nui::GITexture> m_parentTextures[kMaxPaintElements];

	// DUI/non-primary windows are only supported in FiveM/D3D11 currently
#ifdef GTA_FIVE
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_swapTexture;

	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_swapRtv;

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_swapSrv;
#endif

	HANDLE m_lastParentHandle[kMaxPaintElements];

	CefRect m_popupRect;

	std::shared_mutex m_textureMutex;

	bool m_sharedResourceTexturesCreated[kMaxPaintElements];

	std::atomic<uint32_t> m_frameSequence[kMaxPaintElements];

	// Keep in sync with, but leave one frame as if the in-flight frame pool is full Chromium could reallocate all textures
	// leading to flickering/artifacting
	// chromium/components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.h
	static constexpr int kDesignLimitMaxFrames = 10 - 1;

	std::atomic<int> m_inflightFrames;
public:
	inline int GetWidth() const { return m_width; }
	inline int GetHeight() const { return m_height; }

	inline auto GetRenderBufferLock()
	{
		return std::unique_lock{ m_renderBufferLock };
	}

	inline void* GetRenderBuffer() const { return m_renderBuffer; }
	inline int GetRoundedWidth() const { return m_roundedWidth; }

	void TouchMessage();

	void InitializeRenderBacking();

	inline const std::string& GetName()
	{
		return m_name;
	}

	inline void SetName(const std::string& name)
	{
		m_name = name;
	}

	inline bool IsPrimary() const
	{
		return m_isPrimary;
	}

	inline void ProcessLoadQueue()
	{
		std::function<void()> fn;

		while (m_onLoadQueue.try_pop(fn))
		{
			fn();
		}
	}

	inline void PushLoadQueue(std::function<void()>&& fn)
	{
		m_onLoadQueue.push(std::move(fn));
	}

private:
	std::string m_name;

public:
	void			AddDirtyRect(const CefRect& rect);

	inline void		MarkRenderBufferDirty() { InterlockedIncrement(&m_dirtyFlag); }

public:
	static fwRefContainer<NUIWindow> Create(bool primary, int width, int height, CefString url, bool instant, const std::string& context = {});

	void DeferredCreate();

private:
	CefString m_initUrl;

	bool m_isMuted = false;
public:
	~NUIWindow();

	void UpdateFrame();

	void SendBeginFrame();

	void SetPaintType(NUIPaintType type);

	CefBrowser* GetBrowser();

	void SignalPoll(std::string& argument);

	inline void SetClientContextCreated(void(__cdecl* cb)(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context))
	{
		m_onClientCreated = cb;
	}

	inline void OnClientContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
	{
		if (m_onClientCreated)
		{
			m_onClientCreated(browser, frame, context);
		}
	}

	inline fwRefContainer<nui::GITexture> GetTexture() 
	{
		std::shared_lock<std::shared_mutex> _(m_textureMutex);
		return m_nuiTexture;
	}

#ifdef CEF_OSR_LOCK_FRAME
	inline cef_lock_frame_info_t* LockFrame(cef_paint_element_type_t type)
	{
		if (GetBrowser() && GetBrowser()->GetHost())
		{
			return (cef_lock_frame_info_t*)GetBrowser()->GetHost()->LockFrame(type);
		}
		return nullptr;
	}

	inline bool ReleaseFrame(cef_paint_element_type_t type)
	{
		if (GetBrowser() && GetBrowser()->GetHost())
		{
			return GetBrowser()->GetHost()->ReleaseFrame(type);
		}
		return false;
	}

	void UpdateSharedResource(CefRenderHandler::PaintElementType type);
#endif


	inline fwRefContainer<nui::GITexture> GetPopupTexture()
	{
		std::shared_lock<std::shared_mutex> _(m_textureMutex);
		return m_popupTexture;
	}

	inline NUIPaintType GetPaintType() const { return m_paintType; }

	inline fwRefContainer<nui::GITexture> GetParentTexture(CefRenderHandler::PaintElementType type)
	{
		return m_parentTextures[type];
	}

	inline void SetParentTexture(CefRenderHandler::PaintElementType type, fwRefContainer<nui::GITexture> texture)
	{
		m_parentTextures[type] = texture;
	}

	CefRect GetPopupRect();

	void SetPopupRect(const CefRect& rect);

	void HandlePopupShow(bool show);

	bool IsFixedSizeWindow() const;
};

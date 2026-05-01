/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "NUIWindow.h"

#include "NUIClient.h"
#include "NUIWindowManager.h"
#include "DUIShaders.h"

#include <Error.h>

#include <LaunchMode.h>
#include <CoreConsole.h>

#include <include/base/cef_bind.h>
#include <include/base/cef_callback_helpers.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/cef_request_context_handler.h>

#include <CefOverlay.h>

extern nui::GameInterface* g_nuiGi;

#include "memdbgon.h"

extern std::wstring GetNUIStoragePath();

static bool nuiFixedSizeEnabled;

namespace nui
{
extern bool g_rendererInit;
extern bool g_hasFirstRender;
extern void AddSchemeHandlerFactories(CefRefPtr<CefRequestContext> rc);
}

NUIWindow::NUIWindow(bool rawBlit, int width, int height, const std::string& windowContext)
	: m_isPrimary(rawBlit), m_width(width), m_height(height), m_renderBuffer(nullptr), m_dirtyFlag(0), m_onClientCreated(nullptr), m_nuiTexture(nullptr), m_popupTexture(nullptr),
	  m_usingSharedTextures(false), m_lastFrameTime(0), m_lastMessageTime(0), m_roundedHeight(0), m_roundedWidth(0),
	  m_paintType(NUIPaintTypeDummy), m_windowContext(windowContext)
#ifdef GTA_FIVE
	  ,m_swapTexture(nullptr), m_swapRtv(nullptr), m_swapSrv(nullptr)
#endif
{
	memset(&m_sharedResourceTexturesCreated, 0, sizeof(m_sharedResourceTexturesCreated));

	Instance<NUIWindowManager>::Get()->AddWindow(this);
}

static void CloseBrowser(CefRefPtr<CefBrowser> browser)
{
	browser->GetHost()->CloseBrowser(true);
}

NUIWindow::~NUIWindow()
{
	auto nuiClient = ((NUIClient*)m_client.get());

	if (nuiClient)
	{
		nuiClient->SetWindowValid(false);
		nuiClient->ClearWindow();

		CefRefPtr<CefBrowser> browser;
		{
			std::unique_lock _(nuiClient->GetWindowLock());
			if (nuiClient->GetBrowser() && nuiClient->GetBrowser()->GetHost())
			{
				browser = nuiClient->GetBrowser();
			}
		}

		if (browser)
		{
			if (!CefCurrentlyOn(TID_UI))
			{
				CefPostTask(TID_UI, base::BindOnce(&::CloseBrowser, scoped_refptr(browser)));
			}
			else
			{
				browser->GetHost()->CloseBrowser(true);
			}
		}
	}

	if (m_renderBuffer)
	{
		delete[] m_renderBuffer;
	}

	Instance<NUIWindowManager>::Get()->RemoveWindow(this);
}

fwRefContainer<NUIWindow> NUIWindow::Create(bool primary, int width, int height, CefString url, bool instant, const std::string& context)
{
	auto window = new NUIWindow(primary, width, height, context);

	if (instant)
	{
		window->Initialize(url);
	}
	else
	{
		window->m_initUrl = url;
	}

	return window;
}

void NUIWindow::DeferredCreate()
{
	if (!m_client)
	{
		Initialize(m_initUrl);
	}
}

static auto roundUp(int x, int y)
{
	return x + (y - (x % y));
}

void NUIWindow::Initialize(CefString url)
{
	static bool nuiSharedResourcesEnabled = true;
	static ConVar<bool> nuiSharedResources("nui_useSharedResources", ConVar_Archive, true, &nuiSharedResourcesEnabled);

	static bool nuiExternalFramebegin = false;
	static ConVar<bool> nuiExternalFrame("nui_useExternalFrame", ConVar_Archive, false, &nuiExternalFramebegin);

	if (m_renderBuffer)
	{
		delete[] m_renderBuffer;
	}

	// create the temporary backing store
	m_roundedHeight = roundUp(m_height, 16);
	m_roundedWidth = roundUp(m_width, 16);

	InitializeRenderBacking();

	// create the client/browser instance
	{
		CefRefPtr<NUIClient> client = new NUIClient(this);
		client->Initialize();

		m_client = client;
	}

	m_usingSharedTextures = (!CfxIsWine() && nuiSharedResourcesEnabled);
	m_usingExternalFrame = nuiExternalFramebegin;
	CefWindowInfo info;
	info.SetAsWindowless(NULL);
	info.shared_texture_enabled = m_usingSharedTextures;
	info.external_begin_frame_enabled = IsPrimary() && m_usingExternalFrame;
	info.bounds.x = 0;
	info.bounds.y = 0;
	info.bounds.width = m_width;
	info.bounds.height = m_height;

	CefBrowserSettings settings;
	settings.javascript_close_windows = STATE_DISABLED;
	// NOTE: CEF will only use this is external_begin_frame_enabled is false.
	settings.windowless_frame_rate = 240;
	CefString(&settings.default_encoding).FromString("utf-8");

	CefRefPtr<CefRequestContext> rc;

	if (m_windowContext.empty())
	{
		rc = CefRequestContext::GetGlobalContext();
	}
	else
	{
		auto cachePath = fmt::sprintf(L"%s\\context-%s", GetNUIStoragePath(), ToWide(m_windowContext));
		CreateDirectory(cachePath.c_str(), nullptr);

		CefRequestContextSettings rcConfig;
		CefString(&rcConfig.cache_path).FromWString(cachePath);
		rc = CefRequestContext::CreateContext(rcConfig, {});

		nui::AddSchemeHandlerFactories(rc);
	}

	CefBrowserHost::CreateBrowser(info, m_client, url, settings, {}, rc);

	if (!m_usingSharedTextures)
	{
		m_renderBuffer = new char[4 * m_roundedWidth * m_roundedHeight];
	}
}

void NUIWindow::InitializeRenderBacking()
{
	if (!nui::g_rendererInit)
	{
		return;
	}

	// create the backing texture
	{
		std::lock_guard<std::shared_mutex> _(m_textureMutex);
		m_nuiTexture = g_nuiGi->CreateTextureBacking(m_width, m_height, nui::GITextureFormat::ARGB);
	}

#ifdef GTA_FIVE
	if (!m_isPrimary)
	{
		D3D11_TEXTURE2D_DESC tgtDesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_B8G8R8A8_UNORM, m_width, m_height, 1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

		auto d3d = g_nuiGi->GetD3D11Device();
		struct
		{
			void* vtbl;
			ID3D11Device* rawDevice;
		}* deviceStuff = (decltype(deviceStuff))d3d;

		auto hr = deviceStuff->rawDevice->CreateTexture2D(&tgtDesc, nullptr, &m_swapTexture);
		if (SUCCEEDED(hr))
		{
			D3D11_RENDER_TARGET_VIEW_DESC rtDesc = CD3D11_RENDER_TARGET_VIEW_DESC(m_swapTexture.Get(), D3D11_RTV_DIMENSION_TEXTURE2D);
			deviceStuff->rawDevice->CreateRenderTargetView(m_swapTexture.Get(), &rtDesc, &m_swapRtv);
		}
		else
		{
			trace("failed to create m_swapTexture, height: %i, width: %i, error: 0x%x\n", m_width, m_height, hr);
			m_swapTexture = nullptr;
		}
	}
#endif
}

void NUIWindow::AddDirtyRect(const CefRect& rect)
{
	m_dirtyRects.push(rect);
}

CefBrowser* NUIWindow::GetBrowser()
{
	if (!m_client)
	{
		return nullptr;
	}

	return ((NUIClient*)m_client.get())->GetBrowser();
}

extern void NUI_AcceptTexture(uint64_t handle);

#include <d3d11_1.h>
#include <mmsystem.h>

void NUIWindow::TouchMessage()
{
	m_lastMessageTime = timeGetTime();
}

void NUIWindow::SendBeginFrame()
{
	auto browser = GetBrowser();
	if (!browser)
	{
		return;
	}

	auto host = browser->GetHost();
	if (host && m_usingExternalFrame)
	{
		host->SendExternalBeginFrame();
	}
}

void NUIWindow::UpdateFrame()
{
	if (
#ifdef IS_RDR3
	!g_nuiGi->IsUsingD3D12() ||
#endif
	GetPaintType() != NUIPaintTypePostRender)
	{
		SendBeginFrame();
	}

	if (m_client)
	{
		auto browser = ((NUIClient*)m_client.get())->GetBrowser();

		if (browser)
		{
			// the CEF API has a 'is muted' getter but it doesn't work
			bool shouldMute = false;
			g_nuiGi->QueryShouldMute(shouldMute);

			if (!m_isMuted && shouldMute)
			{
				browser->GetHost()->SetAudioMuted(true);
				m_isMuted = true;
			}
			else if (m_isMuted && !shouldMute)
			{
				browser->GetHost()->SetAudioMuted(false);
				m_isMuted = false;
			}
		}
	}

	if (!GetTexture().GetRef())
	{
		return;
	}

	if (m_isPrimary)
	{		
		int resX, resY;
		g_nuiGi->GetGameResolution(&resX, &resY);

		if (IsFixedSizeWindow())
		{
			resX = 1920;
			resY = 1080;
		}

		if (m_width != resX || m_height != resY)
		{
			m_width = resX;
			m_height = resY;

			{
				auto _ = GetRenderBufferLock();
				m_roundedHeight = roundUp(m_height, 16);
				m_roundedWidth = roundUp(m_width, 16);

				if (m_renderBuffer)
				{
					delete[] m_renderBuffer;
					m_renderBuffer = new char[4 * m_roundedWidth * m_roundedHeight];
				}
			}

			if (m_client)
			{
				if (!m_nuiTexture.GetRef())
				{
					InitializeRenderBacking();
				}

				memset(m_sharedResourceTexturesCreated, 0, sizeof(m_sharedResourceTexturesCreated));

				auto client = ((NUIClient*)m_client.get());
				auto browser = client->GetBrowser();

				if (browser)
				{
					browser->GetHost()->WasResized();
				}
				else
				{
					client->OnClientCreated.Connect([this](NUIClient* client)
					{
						client->GetBrowser()->GetHost()->WasResized();
					});
				}
			}
		}

		for (auto& item : m_pollQueue)
		{
			NUIClient* client = static_cast<NUIClient*>(m_client.get());
			auto browser = client->GetBrowser();

			auto message = CefProcessMessage::Create("doPoll");
			auto argList = message->GetArgumentList();

			argList->SetSize(1);
			argList->SetString(0, item);

			browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
		}
	}

	m_pollQueue.clear();

	NUIWindowManager* wm = Instance<NUIWindowManager>::Get();
	auto texture = GetParentTexture(CefRenderHandler::PaintElementType::PET_VIEW);

	if (texture.GetRef())
	{
#ifdef GTA_FIVE
		if (!m_isPrimary)
		{
			{
				struct
				{
					void* vtbl;
					ID3D11Device* rawDevice;
				}* deviceStuff = (decltype(deviceStuff))g_nuiGi->GetD3D11Device();


				if (!m_swapSrv)
				{
					auto nativeTexture = GetParentTexture(CefRenderHandler::PaintElementType::PET_VIEW)->GetNativeTexture();

					if (nativeTexture)
					{
						Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newSrv;
						deviceStuff->rawDevice->CreateShaderResourceView((ID3D11Resource*)nativeTexture, nullptr, &newSrv);
						m_swapSrv = newSrv;
					}
				}
			}

			{
				ID3D11Device* device = g_nuiGi->GetD3D11Device();

				if (device)
				{
					ID3D11DeviceContext* deviceContext = g_nuiGi->GetD3D11DeviceContext();
					assert(deviceContext);

					ID3D11Resource* nativeTexture = nullptr;
					if (auto texture = GetTexture(); texture.GetRef())
					{
						nativeTexture = (ID3D11Resource*)texture->GetNativeTexture();
					}

					if (m_swapTexture && m_swapRtv && m_swapSrv && nativeTexture)
					{
						//
						// LOTS of D3D11 garbage to flip a texture...
						//
						static ID3D11BlendState* bs;
						static ID3D11SamplerState* ss;
						static ID3D11VertexShader* vs;
						static ID3D11PixelShader* ps;

						static std::once_flag of;
						std::call_once(of, []()
						{
							D3D11_SAMPLER_DESC sd = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
							g_nuiGi->GetD3D11Device()->CreateSamplerState(&sd, &ss);

							D3D11_BLEND_DESC bd = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
							g_nuiGi->GetD3D11Device()->CreateBlendState(&bd, &bs);

							g_nuiGi->GetD3D11Device()->CreateVertexShader(fx::shaders::quadVS, sizeof(fx::shaders::quadVS), nullptr, &vs);
							g_nuiGi->GetD3D11Device()->CreatePixelShader(fx::shaders::quadPS, sizeof(fx::shaders::quadPS), nullptr, &ps);
						});

						Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> pPerf;
						deviceContext->QueryInterface(IID_PPV_ARGS(&pPerf));

						pPerf->BeginEvent(L"DrawDUI");

						ID3D11RenderTargetView* oldRtv = nullptr;
						ID3D11DepthStencilView* oldDsv = nullptr;
						deviceContext->OMGetRenderTargets(1, &oldRtv, &oldDsv);

						ID3D11SamplerState* oldSs;
						ID3D11BlendState* oldBs;
						ID3D11PixelShader* oldPs;
						ID3D11VertexShader* oldVs;
						ID3D11ShaderResourceView* oldSrv;

						D3D11_VIEWPORT oldVp;
						UINT numVPs = 1;

						D3D11_RECT oldSr;
						UINT numSRs = 1;

						deviceContext->RSGetScissorRects(&numSRs, &oldSr);
						deviceContext->RSGetViewports(&numVPs, &oldVp);

						deviceContext->OMGetBlendState(&oldBs, nullptr, nullptr);

						deviceContext->PSGetShader(&oldPs, nullptr, nullptr);
						deviceContext->PSGetSamplers(0, 1, &oldSs);
						deviceContext->PSGetShaderResources(0, 1, &oldSrv);

						deviceContext->VSGetShader(&oldVs, nullptr, nullptr);

						ID3D11RenderTargetView* rtv[] = { m_swapRtv.Get() };
						deviceContext->OMSetRenderTargets(std::size(rtv), rtv, nullptr);
						deviceContext->OMSetBlendState(bs, nullptr, 0xffffffff);

						CD3D11_VIEWPORT vp = CD3D11_VIEWPORT(0.0f, 0.0f, m_width, m_height);
						deviceContext->RSSetViewports(1, &vp);

						CD3D11_RECT sr = CD3D11_RECT(0, 0, m_width, m_height);
						deviceContext->RSSetScissorRects(1, &sr);

						deviceContext->PSSetShader(ps, nullptr, 0);
						deviceContext->PSSetSamplers(0, 1, &ss);

						ID3D11ShaderResourceView* srv[] = { m_swapSrv.Get() };
						deviceContext->PSSetShaderResources(0, std::size(srv), srv);

						deviceContext->VSSetShader(vs, nullptr, 0);

						D3D11_PRIMITIVE_TOPOLOGY oldTopo;
						deviceContext->IAGetPrimitiveTopology(&oldTopo);

						ID3D11InputLayout* oldLayout;
						deviceContext->IAGetInputLayout(&oldLayout);

						deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
						deviceContext->IASetInputLayout(nullptr);

						deviceContext->Draw(4, 0);

						deviceContext->CopyResource(nativeTexture, m_swapTexture.Get());

						deviceContext->OMSetRenderTargets(1, &oldRtv, oldDsv);

						deviceContext->IASetPrimitiveTopology(oldTopo);
						deviceContext->IASetInputLayout(oldLayout);

						deviceContext->VSSetShader(oldVs, nullptr, 0);
						deviceContext->PSSetShader(oldPs, nullptr, 0);
						deviceContext->PSSetSamplers(0, 1, &oldSs);
						deviceContext->PSSetShaderResources(0, 1, &oldSrv);
						deviceContext->OMSetBlendState(oldBs, nullptr, 0xffffffff);
						deviceContext->RSSetViewports(1, &oldVp);
						deviceContext->RSSetScissorRects(numSRs, &oldSr);

						if (oldVs)
						{
							oldVs->Release();
						}

						if (oldPs)
						{
							oldPs->Release();
						}

						if (oldBs)
						{
							oldBs->Release();
						}

						if (oldSs)
						{
							oldSs->Release();
						}

						if (oldSrv)
						{
							oldSrv->Release();
						}

						if (oldRtv)
						{
							oldRtv->Release();
						}

						if (oldDsv)
						{
							oldDsv->Release();
						}

						if (oldLayout)
						{
							oldLayout->Release();
						}

						pPerf->EndEvent();
					}
				}
			}
		}
#endif
	}
	else if (m_renderBuffer)
	{
		if (InterlockedExchange(&m_dirtyFlag, 0) > 0)
		{
			void* pBits = nullptr;
			int pitch;
			bool discarded = false;

			nui::GILockedTexture lockedTexture;

			if (GetTexture()->Map(0, 0, &lockedTexture, nui::GILockFlags::Write))
			{
				pBits = lockedTexture.pBits;
				pitch = lockedTexture.pitch;
			}
			else if (GetTexture()->Map(0, 0, &lockedTexture, nui::GILockFlags::WriteDiscard))
			{
				pBits = lockedTexture.pBits;
				pitch = lockedTexture.pitch;

				discarded = true;
			}
			else
			{
				// really
				pBits = nullptr;
			}

			if (pBits)
			{
				if (!discarded)
				{
					while (!m_dirtyRects.empty())
					{
						auto _ = GetRenderBufferLock();
						CefRect rect = m_dirtyRects.front();
						m_dirtyRects.pop();

						if (pitch >= (rect.width * 4))
						{
							int height = m_height;

							// ignore invalid height/width
							if (((rect.y + rect.height) > height) || ((rect.x + rect.width) > m_roundedWidth))
							{
								continue;
							}

							for (int y = rect.y; y < (rect.y + rect.height); y++)
							{
								int* src = &((int*)(m_renderBuffer))[(y * m_roundedWidth) + rect.x];
								int* dest = &((int*)(pBits))[(y * (pitch / 4)) + rect.x];

								memcpy(dest, src, (static_cast<size_t>(rect.width) * 4));
							}
						}
					}
				}
				else
				{
					auto _ = GetRenderBufferLock();
					m_dirtyRects = std::queue<CefRect>();

					memcpy(pBits, m_renderBuffer, static_cast<size_t>(m_height) * pitch);
				}

				GetTexture()->Unmap(&lockedTexture);
			}
		}
	}
}

void NUIWindow::SignalPoll(std::string& argument)
{
	if (m_pollQueue.find(argument) == m_pollQueue.end())
	{
		m_pollQueue.insert(argument);
	}
}

void NUIWindow::HandlePopupShow(bool show)
{
	if (!show)
	{
		auto popupTex = GetParentTexture(CefRenderHandler::PaintElementType::PET_POPUP);
		if (popupTex.GetRef())
		{
			popupTex = nullptr;	

			std::lock_guard<std::shared_mutex> _(m_textureMutex);
			m_popupTexture = nullptr;
		}
	}
}

extern void TranslateWindowRect(const fwRefContainer<NUIWindow>& window, CRect* rect);

void NUIWindow::UpdateSharedResource(CefRenderHandler::PaintElementType type)
{
	// mpMenu may start queuing up frames before the game has had change to begin rendering.
	// CEF/Chromium has a limit of inflight frames and as we are responsible for releasing frames.
	// in some cases we can execeed the max limit leading to frames being flushed and putting the renderer into a weird state.
	if (!nui::g_hasFirstRender)
	{
		return;
	}

	// PET_POPUP is not currently supported in DUI.
	if (!IsPrimary() && type == PET_POPUP)
	{
		return;
	}

	auto& texRef = type == PET_VIEW ? m_nuiTexture : m_popupTexture;
	if (!texRef.GetRef())
	{
		return;
	}

	auto frame = this->LockFrame(type);
	if (!frame || !frame->shared_handle)
	{
		if (frame)
		{
			this->ReleaseFrame(type, frame->frame_seq);
			return;
		}
		return;
	}

	auto sharedHandle = frame->shared_handle;
	auto frameSequence = frame->frame_seq - 1;

	int w = type == PET_VIEW ? m_width : m_popupRect.width;
	int h = type == PET_VIEW ? m_height : m_popupRect.height;

	{
		std::unique_lock<std::shared_mutex> textureLock(m_textureMutex, std::defer_lock);
		if (IsPrimary())
		{
			textureLock.lock();
		}

		if (!m_sharedResourceTexturesCreated[type])
		{
			m_sharedResourceTexturesCreated[type] = true;

			AddRef();
			auto cb = [this, type, frameSequence]()
			{
				ReleaseFrame(type, frameSequence);
				Release();
			};

			if (!IsPrimary())
			{
				auto faketexRef = g_nuiGi->CreateTextureFromShareHandle(sharedHandle, w, h, cb);
				SetParentTexture(type, faketexRef);
#ifdef GTA_FIVE
				m_swapSrv = nullptr;
#endif
			}
			else
			{
				texRef = g_nuiGi->CreateTextureFromShareHandle(sharedHandle, w, h, cb);
				SetParentTexture(type, texRef);
			}
		}
		else
		{
			AddRef();
			g_nuiGi->UpdateTexture(sharedHandle, texRef, nullptr, 1, w, h, [frameSequence, this, type, sharedHandle](void* srv)
			{
				ReleaseFrame(type, frameSequence);
#ifdef GTA_FIVE
				if (!IsPrimary() && srv)
				{
					m_swapSrv = static_cast<ID3D11ShaderResourceView*>(srv);
				}
#endif
				return Release();
			});
		}
	}

	MarkRenderBufferDirty();
}

CefRect NUIWindow::GetPopupRect()
{
	auto rect = m_popupRect;

	if (IsFixedSizeWindow())
	{
		CRect baseRect;
		TranslateWindowRect(this, &baseRect);

		float scaleX = (baseRect.Width() / float(m_width));
		float scaleY = (baseRect.Height() / float(m_height));

		rect.x = (rect.x * scaleX) + baseRect.Left();
		rect.y = (rect.y * scaleY) + baseRect.Top();
		rect.width *= scaleX;
		rect.height *= scaleY;
	}

	return rect;
}

void NUIWindow::SetPopupRect(const CefRect& rect)
{
	m_popupRect = rect;

	std::lock_guard<std::shared_mutex> _(m_textureMutex);
	m_popupTexture = g_nuiGi->CreateTextureBacking(rect.width, rect.height, nui::GITextureFormat::ARGB);
}

void NUIWindow::SetPaintType(NUIPaintType type)
{
	m_paintType = type;
}

bool NUIWindow::IsFixedSizeWindow() const
{
	return nuiFixedSizeEnabled && m_name == "root";
}

static InitFunction initFunction([]
{
	static ConVar<bool> nuiFixedSize("nui_useFixedSize", ConVar_Archive | ConVar_UserPref, false, &nuiFixedSizeEnabled);
});

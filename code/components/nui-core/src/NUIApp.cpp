/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "NUIApp.h"
#include "CefOverlay.h"
#include <CoreConsole.h>
#include "memdbgon.h"
#include <CrossBuildRuntime.h>
#include <PureModeState.h>

#include <include/cef_parser.h>
#include <EpoxyScript.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.UserProfile.h>
#pragma comment(lib, "runtimeobject")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::System::UserProfile;

void NUIApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar)
{
	// add the 'nui://' internal scheme
	registrar->AddCustomScheme("nui", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
	//registrar->AddCustomScheme("nui", true, false, false, true, false, true);
}

// null data resource functions
bool NUIApp::GetDataResource(int resourceID, void*& data, size_t& data_size)
{
	return false;
}

bool NUIApp::GetDataResourceForScale(int resource_id, ScaleFactor scale_factor, void*& data, size_t& data_size)
{
	return false;
}

bool NUIApp::GetLocalizedString(int messageID, CefString& string)
{
	string = "";
	return true;
}

void NUIApp::OnContextInitialized()
{
}

void NUIApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
{
	CefRefPtr<CefV8Value> window = context->GetGlobal();

	window->SetValue("registerPollFunction", CefV8Value::CreateFunction("registerPollFunction", this), V8_PROPERTY_ATTRIBUTE_READONLY);
	window->SetValue("registerFrameFunction", CefV8Value::CreateFunction("registerFrameFunction", this), V8_PROPERTY_ATTRIBUTE_READONLY);
	window->SetValue("registerPushFunction", CefV8Value::CreateFunction("registerPushFunction", this), V8_PROPERTY_ATTRIBUTE_READONLY);

	// register epoxy functions in order to provide CORS compliant messaging between game root and resource windows without breaking backwards compatibility.
	window->SetValue("sendEpoxyMessage", CefV8Value::CreateFunction("sendEpoxyMessage", this), V8_PROPERTY_ATTRIBUTE_READONLY);
	window->SetValue("registerEpoxyHandler", CefV8Value::CreateFunction("registerEpoxyHandler", this), V8_PROPERTY_ATTRIBUTE_NONE);

    // Load epoxy on context creation (and not devtools)
	if (auto parent = frame->GetParent(); parent && parent->IsMain())
	{
		frame->ExecuteJavaScript(fmt::sprintf(g_epoxyScript, frame->GetName().ToString()), "nui://epoxy", 0);
	}
	frame->ExecuteJavaScript(g_gameViewScript, "nui://game-view-wrapper", 0);


	{
		winrt::init_apartment();

		std::vector<std::wstring> langList;
		for (const auto& lang : GlobalizationPreferences::Languages())
		{
			langList.push_back(std::wstring{ lang });
		}

		auto languages = CefV8Value::CreateArray(int(langList.size()));
		for (size_t i = 0; i < langList.size(); i++)
		{
			languages->SetValue(int(i), CefV8Value::CreateString(CefString{ (char16_t*)langList[i].data(), langList[i].length(), true }));
		}

		window->SetValue("nuiSystemLanguages", languages, V8_PROPERTY_ATTRIBUTE_READONLY);
	}

	window->SetValue("invokeNative", CefV8Value::CreateFunction("invokeNative", this), V8_PROPERTY_ATTRIBUTE_READONLY);
#ifdef NUI_WITH_AUDIO_SINKS
	window->SetValue("nuiSetAudioCategory", CefV8Value::CreateFunction("nuiSetAudioCategory", this), V8_PROPERTY_ATTRIBUTE_READONLY);
#endif
	window->SetValue("nuiTargetGame", CefV8Value::CreateString(
#ifdef IS_LAUNCHER
		"launcher"
#elif defined(IS_RDR3)
		"rdr3"
#elif defined(GTA_FIVE)
		"gta5"
#elif defined (GTA_NY)
		"ny"
#else
		"unknown"
#endif
	), V8_PROPERTY_ATTRIBUTE_READONLY);
	window->SetValue("nuiTargetGameBuild", CefV8Value::CreateInt(xbr::GetRequestedGameBuild()), V8_PROPERTY_ATTRIBUTE_READONLY);
	window->SetValue("nuiTargetGamePureLevel", CefV8Value::CreateInt(fx::client::GetPureLevel()), V8_PROPERTY_ATTRIBUTE_READONLY);

	// FxDK API
	{
		std::vector<std::string> fxdkHandlers{
			"resizeGame",
			"initRGDInput",
			"setRawMouseCapture",
			"sendMouseWheel",
			"setKeyState",
			"setMouseButtonState",
			"openDevTools",
			"setFPSLimit",
			"setInputChar",
			"setWorldEditorControls",
			"setWorldEditorMouse",
			"sendGameClientEvent",
			"fxdkSendApiMessage",
			"fxdkOpenSelectFolderDialog",
			"fxdkOpenSelectFileDialog",
			"fxdkClipboardRead",
			"fxdkClipboardWrite"
		};

		for (auto const& handler : fxdkHandlers)
		{
			window->SetValue(handler, CefV8Value::CreateFunction(handler, this), V8_PROPERTY_ATTRIBUTE_READONLY);
		}
	}
}

void NUIApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
{
	for (auto& handler : m_v8ReleaseHandlers)
	{
		handler(context);
	}
}

void NUIApp::OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line)
{
	static ConVar<bool> nuiUseInProcessGpu("nui_useInProcessGpu", ConVar_Archive, true);

	static std::string defaultUiUrl = "https://nui-game-internal/ui/app/index.html";
	static ConVar<std::string> uiUrlVar("ui_url", ConVar_UserPref, defaultUiUrl);

	if (uiUrlVar.GetValue() != defaultUiUrl)
	{
		CefString uiUrl(uiUrlVar.GetValue());
		CefURLParts uiUrlParts;

		if (CefParseURL(uiUrl, uiUrlParts) && uiUrlParts.origin.length > 0)
		{
			// Allow secure context for insecure localhost
			command_line->AppendSwitchWithValue("unsafely-treat-insecure-origin-as-secure", uiUrlParts.origin.str);
		}
	}

	if (nuiUseInProcessGpu.GetValue())
	{
		command_line->AppendSwitch("in-process-gpu");
	}

	// It's not right to have this enabled and enable *all* experimental features
	// Rather any experimental feature should be added on a case-by-case 
	//command_line->AppendSwitch("enable-experimental-web-platform-features");

	// These experimental features are currently broken as of writing (April 2026, M144 build)
	// While we are also disabling web platform features, it's worth keeping a list of ones that **are** broken, why and their impact on NUI
	// 
	// WidthAndHeightAsPresentationAttributesOnNestedSvg:
	// Breaks SVG rendering in popular resources, see https://issues.chromium.org/issues/449170647 for chromium issue
	// 
	// SelectionAndFocusedVisiblePositionMatch
	// Private issue report claims that this is responsible for causing UI freezes. This might be causing some cases of UI freezes
	// but there's no public info, but better to keep here until
	// a) the issue is made public
	// b) this flag is removed or moved to stable.
	//
	command_line->AppendSwitchWithValue("disable-blink-features", "WidthAndHeightAsPresentationAttributesOnNestedSvg, SelectionAndFocusedVisiblePositionMatch");

	command_line->AppendSwitch("ignore-gpu-blocklist");
	command_line->AppendSwitch("disable-direct-composition");
	command_line->AppendSwitch("disable-gpu-driver-bug-workarounds");
	command_line->AppendSwitchWithValue("default-encoding", "utf-8");
	command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
	command_line->AppendSwitchWithValue("disable-features", "HardwareMediaKeyHandling");

	// Disable features forced into CEF by Chrome runtime/bootstrap
	command_line->AppendSwitch("disable-gaia-services");
	command_line->AppendSwitch("disable-sync");
	command_line->AppendSwitch("disable-extensions");
	command_line->AppendSwitch("disable-spell-checking");
#if !GTA_NY
	command_line->AppendSwitch("enable-gpu-rasterization");
#else
	command_line->AppendSwitch("disable-gpu-vsync");
#endif

	// Don't allow accidental zooming in on a trackpad
	command_line->AppendSwitch("disable-pinch");

	command_line->AppendSwitch("disable-gpu-process-crash-limit");

	// important switch to prevent users from mentioning 'why are there 50 chromes again'
	command_line->AppendSwitch("disable-site-isolation-trials");

	// TODO: remove this flag in the future
	command_line->AppendSwitch("disable-web-security");

	// some GPUs are in the GPU blacklist as 'forcing D3D9'
	// this just forces D3D11 anyway.
	command_line->AppendSwitchWithValue("use-angle", "d3d11");

	// disable accelerated video decoding, something in M91 upgrade broke this (instant hang when playing Twitter video)
	command_line->AppendSwitch("disable-accelerated-video-decode");
	command_line->AppendSwitch("disable-accelerated-video-encode");
	command_line->AppendSwitch("disable-accelerated-mjpeg-decode");
}

bool NUIApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
	auto handler = m_processMessageHandlers.find(message->GetName());
	bool success = false;

	if (handler != m_processMessageHandlers.end())
	{
		success = handler->second(browser, message);
	}
	else
	{
		trace("Unknown NUI process message: %s\n", message->GetName().ToString().c_str());
	}

	return success;
}

bool NUIApp::Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception)
{
	auto handler = m_v8Handlers.find(name);
	bool success = false;

	if (handler != m_v8Handlers.end())
	{
		retval = handler->second(arguments, exception);

		success = true;
	}
	else
	{
		trace("Unknown NUI function: %s\n", name.ToString().c_str());
	}

	return success;
}

void NUIApp::AddProcessMessageHandler(std::string key, TProcessMessageHandler handler)
{
	m_processMessageHandlers[key] = handler;
}

void NUIApp::AddV8Handler(std::string key, TV8Handler handler)
{
	m_v8Handlers[key] = handler;
}

void NUIApp::AddContextReleaseHandler(TContextReleaseHandler handler)
{
	m_v8ReleaseHandlers.emplace_back(handler);
}

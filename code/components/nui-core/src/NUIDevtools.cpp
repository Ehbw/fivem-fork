/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"

#include "NUIDevtools.h"
#include <CoreConsole.h>

CefRefPtr<NUIDevtoolsMessageObserver> g_devToolsObserver = new NUIDevtoolsMessageObserver();

bool NUIDevtoolsMessageObserver::OnDevToolsMessage(CefRefPtr<CefBrowser> browser, const void* message, size_t message_size)
{
	if (!m_isAllowed)
	{
		return true;
	}

	return false;
}

void NUIDevtoolsMessageObserver::OnDevToolsAgentAttached(CefRefPtr<CefBrowser> browser)
{
	if (!m_isAllowed)
	{
		browser->GetHost()->CloseDevTools();
	}
}

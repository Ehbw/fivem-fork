/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */
#pragma once


#include <CefOverlay.h>
#include <include/cef_app.h>
#include <include/cef_browser.h>

class NUIDevtoolsMessageObserver : public CefDevToolsMessageObserver 
{
public:
  virtual bool OnDevToolsMessage(CefRefPtr<CefBrowser> browser, const void* message, size_t message_size) override;

  virtual void OnDevToolsAgentAttached(CefRefPtr<CefBrowser> browser) override;

  void SetAllowed(bool allowed)
  {
	  m_isAllowed = allowed;
  }

  bool IsAllowed() const
  {
	  return m_isAllowed;
  }

private:
    std::atomic<bool> m_isAllowed = true;
	IMPLEMENT_REFCOUNTING(NUIDevtoolsMessageObserver);
};

extern CefRefPtr<NUIDevtoolsMessageObserver> g_devToolsObserver;

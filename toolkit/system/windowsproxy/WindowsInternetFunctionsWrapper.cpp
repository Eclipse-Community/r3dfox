/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>
#include <ras.h>
#include <wininet.h>

#include "WindowsInternetFunctionsWrapper.h"
#include "WinRegistry.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "nsINetworkLinkService.h"
#include "nsIObserverService.h"
#include "nsThreadUtils.h"
#include "nsXPCOM.h"

static mozilla::LazyLogModule gWinProxyLog("WindowsProxy");
#define LOG(args) MOZ_LOG(gWinProxyLog, mozilla::LogLevel::Debug, args)

namespace mozilla {
namespace toolkit {
namespace system {

class NetworkLinkObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  explicit NetworkLinkObserver(WindowsInternetFunctionsWrapper* aWrapper)
      : mWrapper(aWrapper) {
    LOG(("NetworkLinkObserver %p constructed (wrapper=%p)", this, aWrapper));
  }

 private:
  ~NetworkLinkObserver() { LOG(("NetworkLinkObserver %p destroyed", this)); }
  mozilla::WeakPtr<WindowsInternetFunctionsWrapper> mWrapper;
};

NS_IMPL_ISUPPORTS(NetworkLinkObserver, nsIObserver)

NS_IMETHODIMP NetworkLinkObserver::Observe(nsISupports* aSubject,
                                           const char* aTopic,
                                           const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    LOG(("NetworkLinkObserver %p: XPCOM shutdown, removing observers", this));
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, NS_NETWORK_LINK_TOPIC);
      obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
    }
    RefPtr<WindowsInternetFunctionsWrapper> wrapper = mWrapper.get();
    mWrapper = nullptr;
    if (wrapper) {
      wrapper->Shutdown();
    }
    return NS_OK;
  }

  if (!strcmp(aTopic, NS_NETWORK_LINK_TOPIC) && mWrapper) {
    LOG(("Network link changed, invalidating connection and proxy cache"));
    MutexAutoLock lock(mWrapper->mMutex);
    mWrapper->mConnCacheValid = false;
    mWrapper->mCacheValid = false;
    mWrapper->mAtomicFlags = UINT32_MAX;
  }
  return NS_OK;
}

static bool GetConnectionState(DWORD& aConnFlags, WCHAR* aConnName,
                               size_t aConnNameLen) {
  MOZ_SEH_TRY {
    InternetGetConnectedStateExW(&aConnFlags, aConnName, aConnNameLen, 0);
    return true;
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

WindowsInternetFunctionsWrapper::WindowsInternetFunctionsWrapper() {
  LOG(("WindowsInternetFunctionsWrapper %p constructed", this));
}

void WindowsInternetFunctionsWrapper::Init() {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("WindowsInternetFunctionsWrapper %p Init()", this));

  using namespace mozilla::widget::WinRegistry;
  Key key(HKEY_CURRENT_USER,
          u"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"_ns,
          KeyMode::Notify);
  if (key) {
    mKeyWatcher = MakeUnique<KeyWatcher>(
        std::move(key), GetCurrentSerialEventTarget(), [self = RefPtr{this}] {
          LOG(
              ("WindowsInternetFunctionsWrapper %p: registry change, "
               "invalidating cache",
               self.get()));
          MutexAutoLock lock(self->mMutex);
          self->mCacheValid = false;
          self->mAtomicFlags = UINT32_MAX;
        });
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    mNetworkLinkObserver = new NetworkLinkObserver(this);
    obs->AddObserver(mNetworkLinkObserver, NS_NETWORK_LINK_TOPIC, false);
    obs->AddObserver(mNetworkLinkObserver, NS_XPCOM_SHUTDOWN_OBSERVER_ID,
                     false);
  }
}

void WindowsInternetFunctionsWrapper::Shutdown() { mKeyWatcher = nullptr; }

WindowsInternetFunctionsWrapper::~WindowsInternetFunctionsWrapper() {
  LOG(("WindowsInternetFunctionsWrapper %p destroyed", this));
  if (mNetworkLinkObserver) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(mNetworkLinkObserver, NS_NETWORK_LINK_TOPIC);
      obs->RemoveObserver(mNetworkLinkObserver, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
    }
  }
}

nsresult WindowsInternetFunctionsWrapper::ReadAllOptionsLocked(
    DWORD aConnFlags, const nsString& aConnName) {
  mMutex.AssertCurrentThreadOwns();

  nsAutoString connNameCopy(aConnName);

  INTERNET_PER_CONN_OPTIONW options[4];
  options[0].dwOption = INTERNET_PER_CONN_FLAGS_UI;
  options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
  options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
  options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;

  INTERNET_PER_CONN_OPTION_LISTW list;
  list.dwSize = sizeof(INTERNET_PER_CONN_OPTION_LISTW);
  list.pszConnection =
      aConnFlags & INTERNET_CONNECTION_MODEM
          ? const_cast<WCHAR*>(reinterpret_cast<const WCHAR*>(
                static_cast<const char16_t*>(connNameCopy.get())))
          : nullptr;
  list.dwOptionCount = std::size(options);
  list.dwOptionError = 0;
  list.pOptions = options;

  unsigned long size = sizeof(INTERNET_PER_CONN_OPTION_LISTW);
  {
    MutexAutoUnlock unlock(mMutex);
    if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                              &list, &size)) {
    if (GetLastError() != ERROR_INVALID_PARAMETER) {
      LOG(("ReadAllOptionsLocked: InternetQueryOptionW failed"));
      return NS_ERROR_FAILURE;
    }
    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    size = sizeof(INTERNET_PER_CONN_OPTION_LISTW);
    MOZ_SEH_TRY {
      if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                                &list, &size)) {
        return NS_ERROR_FAILURE;
      }
    } MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
      return NS_ERROR_FAILURE;
    }
    }
  }

  mCachedFlags = options[0].Value.dwValue;
  mAtomicFlags = mCachedFlags;
  LOG(("ReadAllOptionsLocked: flags=0x%x", mCachedFlags));

  auto assignAndFree = [](nsString& aDest, WCHAR* aSrc, const char* aName) {
    if (aSrc) {
      aDest.Assign(aSrc);
      GlobalFree(aSrc);
    } else {
      aDest.Truncate();
    }
    LOG(("  %s=[%s]", aName, NS_ConvertUTF16toUTF8(aDest).get()));
  };

  assignAndFree(mCachedProxyServer, options[1].Value.pszValue, "ProxyServer");
  assignAndFree(mCachedProxyBypass, options[2].Value.pszValue, "ProxyBypass");
  assignAndFree(mCachedAutoConfigUrl, options[3].Value.pszValue,
                "AutoConfigUrl");

  return NS_OK;
}

nsresult WindowsInternetFunctionsWrapper::ReadInternetOption(
    uint32_t aOption, uint32_t& aFlags, nsAString& aValue) {
  // Bug 1366133: InternetGetConnectedStateExW() may cause hangs
  MOZ_ASSERT(!NS_IsMainThread());

  MutexAutoLock lock(mMutex);

  if (!mConnCacheValid) {
    DWORD connFlags = 0;
    WCHAR connName[RAS_MaxEntryName + 1];
    bool res = true;
    {
      MutexAutoUnlock unlock(mMutex);
      res = GetConnectionState(connFlags, connName, std::size(connName));
    }
    if (!res) {
      return NS_ERROR_FAILURE;
    }

    nsString currentConnName;
    if (connFlags & INTERNET_CONNECTION_MODEM) {
      currentConnName.Assign(connName);
    }

    if (currentConnName != mCachedConnName) {
      LOG(
          ("ReadInternetOption: connection changed [%s] -> [%s], invalidating "
           "cache",
           NS_ConvertUTF16toUTF8(mCachedConnName).get(),
           NS_ConvertUTF16toUTF8(currentConnName).get()));
      mCacheValid = false;
    }

    mCachedConnFlags = connFlags;
    mCachedConnName = currentConnName;
    // Only cache the connection state when we have an observer to invalidate
    // it.
    if (mNetworkLinkObserver) {
      mConnCacheValid = true;
    }
  }

  if (!mCacheValid) {
    LOG(("ReadInternetOption: cache miss, reading from WinINet"));
    nsresult rv = ReadAllOptionsLocked(mCachedConnFlags, mCachedConnName);
    if (NS_FAILED(rv)) {
      return rv;
    }
    // Only mark the cache valid when change detection is active. Without a
    // KeyWatcher to invalidate it, caching would return stale data
    // indefinitely.
    if (mKeyWatcher) {
      mCacheValid = true;
    }
  } else {
    LOG(("ReadInternetOption: cache hit"));
  }

  aFlags = mCachedFlags;
  switch (aOption) {
    case INTERNET_PER_CONN_PROXY_SERVER:
      aValue.Assign(mCachedProxyServer);
      LOG(("ReadInternetOption: -> ProxyServer=[%s] flags=0x%x",
           NS_ConvertUTF16toUTF8(mCachedProxyServer).get(), aFlags));
      break;
    case INTERNET_PER_CONN_PROXY_BYPASS:
      aValue.Assign(mCachedProxyBypass);
      LOG(("ReadInternetOption: -> ProxyBypass=[%s] flags=0x%x",
           NS_ConvertUTF16toUTF8(mCachedProxyBypass).get(), aFlags));
      break;
    case INTERNET_PER_CONN_AUTOCONFIG_URL:
      aValue.Assign(mCachedAutoConfigUrl);
      LOG(("ReadInternetOption: -> AutoConfigUrl=[%s] flags=0x%x",
           NS_ConvertUTF16toUTF8(mCachedAutoConfigUrl).get(), aFlags));
      break;
    default:
      aValue.Truncate();
      break;
  }
  return NS_OK;
}

}  // namespace system
}  // namespace toolkit
}  // namespace mozilla

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MozNewTabWallpaperProtocolHandler.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/net/NeckoParent.h"
#include "nsContentUtils.h"
#include "nsIFile.h"
#include "nsIFileChannel.h"
#include "nsIFileURL.h"
#include "nsIMIMEService.h"
#include "nsDirectoryServiceUtils.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsNetUtil.h"
#include "nsURLHelper.h"
#include "prio.h"
#include "SimpleChannel.h"

#define NEWTAB_WALLPAPER_SCHEME "moz-newtab-wallpaper"

namespace mozilla {
namespace net {

StaticRefPtr<MozNewTabWallpaperProtocolHandler>
    MozNewTabWallpaperProtocolHandler::sSingleton;

NS_IMPL_QUERY_INTERFACE(MozNewTabWallpaperProtocolHandler,
                        nsISubstitutingProtocolHandler, nsIProtocolHandler,
                        nsISupportsWeakReference)
NS_IMPL_ADDREF_INHERITED(MozNewTabWallpaperProtocolHandler,
                         SubstitutingProtocolHandler)
NS_IMPL_RELEASE_INHERITED(MozNewTabWallpaperProtocolHandler,
                          SubstitutingProtocolHandler)

already_AddRefed<MozNewTabWallpaperProtocolHandler>
MozNewTabWallpaperProtocolHandler::GetSingleton() {
  if (!sSingleton) {
    sSingleton = new MozNewTabWallpaperProtocolHandler();
    ClearOnShutdown(&sSingleton);
  }

  return do_AddRef(sSingleton);
}

// A moz-newtab-wallpaper URI is only loadable by chrome pages in the parent
// process, or privileged content running in the privileged about content
// process.
MozNewTabWallpaperProtocolHandler::MozNewTabWallpaperProtocolHandler()
    : SubstitutingProtocolHandler(NEWTAB_WALLPAPER_SCHEME) {}

RefPtr<RemoteStreamPromise> MozNewTabWallpaperProtocolHandler::NewStream(
    nsIURI* aChildURI, bool* aTerminateSender) {
  MOZ_ASSERT(!IsNeckoChild());
  MOZ_ASSERT(NS_IsMainThread());

  if (!aChildURI || !aTerminateSender) {
    return RemoteStreamPromise::CreateAndReject(NS_ERROR_INVALID_ARG, __func__);
  }

  *aTerminateSender = true;
  nsresult rv;

  bool isWallpaperScheme = false;
  if (NS_FAILED(
          aChildURI->SchemeIs(NEWTAB_WALLPAPER_SCHEME, &isWallpaperScheme)) ||
      !isWallpaperScheme) {
    return RemoteStreamPromise::CreateAndReject(NS_ERROR_UNKNOWN_PROTOCOL,
                                                __func__);
  }

  nsAutoCString host;
  if (NS_FAILED(aChildURI->GetAsciiHost(host)) || host.IsEmpty()) {
    return RemoteStreamPromise::CreateAndReject(NS_ERROR_UNEXPECTED, __func__);
  }

  *aTerminateSender = false;

  nsAutoCString resolvedSpec;
  rv = ResolveURI(aChildURI, resolvedSpec);
  if (NS_FAILED(rv)) {
    return RemoteStreamPromise::CreateAndReject(rv, __func__);
  }

  return mozilla::net::NeckoParent::CreateRemoteStreamForResolvedURI(
      aChildURI, resolvedSpec, "image/jpeg"_ns);
}

bool MozNewTabWallpaperProtocolHandler::ResolveSpecialCases(
    const nsACString& aHost, const nsACString& aPath,
    const nsACString& aPathname, nsACString& aResult) {
  if (aHost.IsEmpty()) {
    return false;
  }

  if (IsNeckoChild()) {
    // Child process: return placeholder file:// URI for
    // SubstitutingProtocolHandler. SubstituteChannel will replace with a remote
    // channel that proxies the load to the parent process.
    aResult.Assign("file://");
    aResult.Append(aHost);
    return true;
  } else {
    // Parent process: resolve to profile/wallpaper/{host} directory.
    nsCOMPtr<nsIFile> file;
    nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                         getter_AddRefs(file));
    if (NS_FAILED(rv)) {
      return false;
    }

    rv = file->AppendNative("wallpaper"_ns);
    if (NS_FAILED(rv)) {
      return false;
    }

    rv = file->AppendNative(nsCString(aHost));
    if (NS_FAILED(rv)) {
      return false;
    }

    nsCOMPtr<nsIURI> uri;
    rv = NS_NewFileURI(getter_AddRefs(uri), file);
    if (NS_FAILED(rv)) {
      return false;
    }

    return NS_SUCCEEDED(uri->GetSpec(aResult));
  }
}

nsresult MozNewTabWallpaperProtocolHandler::SubstituteChannel(
    nsIURI* aURI, nsILoadInfo* aLoadInfo, nsIChannel** aRetVal) {
  // Check if URI resolves to a file URI.
  nsAutoCString resolvedSpec;
  MOZ_TRY(ResolveURI(aURI, resolvedSpec));

  nsAutoCString scheme;
  MOZ_TRY(net_ExtractURLScheme(resolvedSpec, scheme));

  if (!scheme.EqualsLiteral("file")) {
    NS_WARNING("moz-newtab-wallpaper URIs should only resolve to file URIs.");
    return NS_ERROR_NO_INTERFACE;
  }

  if (IsNeckoChild()) {
    MOZ_TRY(SubstituteRemoteChannel(aURI, aLoadInfo, aRetVal));
  }

  return NS_OK;
}

Result<Ok, nsresult> MozNewTabWallpaperProtocolHandler::SubstituteRemoteChannel(
    nsIURI* aURI, nsILoadInfo* aLoadInfo, nsIChannel** aRetVal) {
  MOZ_ASSERT(IsNeckoChild());
  MOZ_TRY(aURI ? NS_OK : NS_ERROR_INVALID_ARG);
  MOZ_TRY(aLoadInfo ? NS_OK : NS_ERROR_INVALID_ARG);

#ifdef DEBUG
  nsAutoCString resolvedSpec;
  MOZ_TRY(ResolveURI(aURI, resolvedSpec));

  nsAutoCString scheme;
  MOZ_TRY(net_ExtractURLScheme(resolvedSpec, scheme));

  MOZ_ASSERT(scheme.EqualsLiteral("file"));
#endif /* DEBUG */

  RefPtr<RemoteStreamGetter> streamGetter =
      new RemoteStreamGetter(aURI, aLoadInfo);

  NewSimpleChannel(aURI, aLoadInfo, streamGetter, aRetVal);
  return Ok();
}

// static
void MozNewTabWallpaperProtocolHandler::NewSimpleChannel(
    nsIURI* aURI, nsILoadInfo* aLoadinfo, RemoteStreamGetter* aStreamGetter,
    nsIChannel** aRetVal) {
  nsCOMPtr<nsIChannel> channel = NS_NewSimpleChannel(
      aURI, aLoadinfo, aStreamGetter,
      [](nsIStreamListener* listener, nsIChannel* simpleChannel,
         RemoteStreamGetter* getter) -> RequestOrReason {
        return getter->GetAsync(listener, simpleChannel,
                                &NeckoChild::SendGetMozNewTabWallpaperStream);
      });

  channel.swap(*aRetVal);
}

}  // namespace net
}  // namespace mozilla

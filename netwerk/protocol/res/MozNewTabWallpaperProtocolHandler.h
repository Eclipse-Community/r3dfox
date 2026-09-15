/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MozNewTabWallpaperProtocolHandler_h_
#define MozNewTabWallpaperProtocolHandler_h_

#include "mozilla/Result.h"
#include "mozilla/MozPromise.h"
#include "mozilla/net/RemoteStreamGetter.h"
#include "SubstitutingProtocolHandler.h"
#include "nsIInputStream.h"
#include "nsWeakReference.h"

namespace mozilla {
namespace net {

class RemoteStreamGetter;

class MozNewTabWallpaperProtocolHandler final
    : public nsISubstitutingProtocolHandler,
      public SubstitutingProtocolHandler,
      public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_FORWARD_NSIPROTOCOLHANDLER(SubstitutingProtocolHandler::)
  NS_FORWARD_NSISUBSTITUTINGPROTOCOLHANDLER(SubstitutingProtocolHandler::)

  static already_AddRefed<MozNewTabWallpaperProtocolHandler> GetSingleton();

  /**
   * Obtains an input stream for a user-uploaded New Tab wallpaper.
   *
   * @param aChildURI moz-newtab-wallpaper URI from child process
   * @param aTerminateSender set to true if URI is invalid (terminates child)
   * @return RemoteStreamPromise resolving to RemoteStreamInfo on success or
   * nsresult on failure
   */
  RefPtr<RemoteStreamPromise> NewStream(nsIURI* aChildURI,
                                        bool* aTerminateSender);

 protected:
  ~MozNewTabWallpaperProtocolHandler() = default;

 private:
  explicit MozNewTabWallpaperProtocolHandler();

  [[nodiscard]] bool ResolveSpecialCases(const nsACString& aHost,
                                         const nsACString& aPath,
                                         const nsACString& aPathname,
                                         nsACString& aResult) override;

  /**
   * Substitutes the channel with a remote channel in child process.
   *
   * @param aURI the moz-newtab-wallpaper URI
   * @param aLoadInfo the loadinfo for the request
   * @param aRetVal in/out channel param for the substituted channel
   * @return NS_OK on success or NS_ERROR_NO_INTERFACE if URI doesn't
   *         resolve to file://
   */
  [[nodiscard]] virtual nsresult SubstituteChannel(
      nsIURI* aURI, nsILoadInfo* aLoadInfo, nsIChannel** aRetVal) override;

  /**
   * Replaces the channel with one that proxies the load to parent process.
   *
   * @param aURI the moz-newtab-wallpaper URI
   * @param aLoadInfo the loadinfo for the request
   * @param aRetVal in/out channel param for the substituted remote channel
   * @return NS_OK if successful, otherwise an error
   */
  Result<Ok, nsresult> SubstituteRemoteChannel(nsIURI* aURI,
                                               nsILoadInfo* aLoadInfo,
                                               nsIChannel** aRetVal);

  // To allow parent IPDL actors to invoke methods on this handler when
  // handling moz-newtab-wallpaper requests from the child.
  static StaticRefPtr<MozNewTabWallpaperProtocolHandler> sSingleton;

  // Gets a SimpleChannel that wraps the provided channel.
  static void NewSimpleChannel(nsIURI* aURI, nsILoadInfo* aLoadinfo,
                               RemoteStreamGetter* aStreamGetter,
                               nsIChannel** aRetVal);
};

}  // namespace net
}  // namespace mozilla

#endif /* MozNewTabWallpaperProtocolHandler_h_ */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAlertsService_h_
#define nsAlertsService_h_

#include "nsIAlertsService.h"
#include "nsCOMPtr.h"

class nsAlertsService : public nsIAlertsService,
                        public nsIAlertsDoNotDisturb,
                        public nsIObserver {
 public:
  NS_DECL_NSIALERTSDONOTDISTURB
  NS_DECL_NSIALERTSSERVICE
  NS_DECL_NSIOBSERVER
  NS_DECL_ISUPPORTS

  nsAlertsService();

  nsresult Init();

 protected:
  virtual ~nsAlertsService();

  bool ShouldShowAlert();
  bool ShouldUseSystemBackend();
  already_AddRefed<nsIAlertsDoNotDisturb> GetDNDBackend();
  nsCOMPtr<nsIAlertsService> mBackend;
};

#endif /* nsAlertsService_h_ */

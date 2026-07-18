/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EntryList.h"

#include "nsIWeakReferenceUtils.h"
#include "nsSHistory.h"

namespace mozilla::dom {

EntryList::EntryList(nsSHistory* aSessionHistory, const nsID& aHistoryID)
    : mSessionHistory(do_GetWeakReference(aSessionHistory)),
      mHistoryID(aHistoryID) {}

EntryList::~EntryList() {
  clear();
  if (nsCOMPtr<nsISHistory> shistory = do_QueryReferent(mSessionHistory)) {
    static_cast<nsSHistory*>(shistory.get())->RemoveEntryList(mHistoryID);
  }
}

}  // namespace mozilla::dom

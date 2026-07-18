/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_EntryList_h
#define mozilla_dom_EntryList_h

#include "mozilla/LinkedList.h"
#include "mozilla/RefCounted.h"
#include "mozilla/WeakPtr.h"
#include "nsID.h"

class nsSHistory;

namespace mozilla::dom {

class SessionHistoryEntry;

class EntryList final : public LinkedList<SessionHistoryEntry>,
                        public RefCounted<EntryList>,
                        public SupportsWeakPtr {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(EntryList)

  explicit EntryList(nsSHistory* aSessionHistory, const nsID& aHistoryID);
  ~EntryList();

 private:
  nsCOMPtr<nsIWeakReference> mSessionHistory;
  nsID mHistoryID;
};

}  // namespace mozilla::dom

#endif  // !defined(mozilla_dom_EntryList_h)

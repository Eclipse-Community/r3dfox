/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ReferrerInfoUtils.h"

#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "nsQueryObject.h"

namespace IPC {
void ParamTraits<nsIReferrerInfo*>::Write(MessageWriter* aWriter,
                                          nsIReferrerInfo* aParam) {
  bool isNull = !aParam;
  WriteParam(aWriter, isNull);
  if (!isNull) {
    RefPtr<mozilla::dom::ReferrerInfo> info = do_QueryObject(aParam);
    MOZ_ASSERT(info);
    info->Serialize(aWriter);
  }
}

bool ParamTraits<nsIReferrerInfo*>::Read(MessageReader* aReader,
                                         RefPtr<nsIReferrerInfo>* aResult) {
  bool isNull;
  if (!ReadParam(aReader, &isNull)) {
    return false;
  }
  if (isNull) {
    *aResult = nullptr;
    return true;
  }
  return mozilla::dom::ReferrerInfo::Deserialize(aReader, aResult);
}

}  // namespace IPC

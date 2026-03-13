/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef IPC_ErrorIPCUtils_h
#define IPC_ErrorIPCUtils_h

#include "ipc/EnumSerializer.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/ErrorResult.h"

namespace IPC {

template <>
struct ParamTraits<mozilla::dom::ErrNum>
    : public ContiguousEnumSerializer<
          mozilla::dom::ErrNum, mozilla::dom::ErrNum(0),
          mozilla::dom::ErrNum(mozilla::dom::Err_Limit)> {};

template <>
struct ParamTraits<mozilla::ErrorResult> {
  static void Write(MessageWriter* aWriter,
                    const mozilla::ErrorResult& aParam) {
    aParam.SerializeErrorResult(aWriter);
  }

  static void Write(MessageWriter* aWriter, mozilla::ErrorResult&& aParam) {
    aParam.SerializeErrorResult(aWriter);
    aParam.SuppressException();
  }

  static bool Read(MessageReader* aReader, mozilla::ErrorResult* aResult) {
    return aResult->DeserializeErrorResult(aReader);
  }
};

template <>
struct ParamTraits<mozilla::CopyableErrorResult> {
  typedef mozilla::CopyableErrorResult paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    aParam.SerializeErrorResult(aWriter);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return aResult->DeserializeErrorResult(aReader);
  }
};

}  // namespace IPC

#endif

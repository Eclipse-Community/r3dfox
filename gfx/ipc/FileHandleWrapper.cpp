/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileHandleWrapper.h"

namespace mozilla::gfx {

FileHandleWrapper::FileHandleWrapper(mozilla::UniqueFileHandle&& aHandle)
    : mHandle(std::move(aHandle)) {}

FileHandleWrapper::~FileHandleWrapper() = default;

mozilla::detail::FileHandleType FileHandleWrapper::GetHandle() {
  return mHandle.get();
}

}  // namespace mozilla::gfx

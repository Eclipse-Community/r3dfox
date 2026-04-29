/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemSecurity.h"
#include "FileSystemUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/StaticPtr.h"

namespace mozilla::dom {

namespace {

StaticRefPtr<FileSystemSecurity> gFileSystemSecurity;

#if defined(XP_WIN)
constexpr char16_t kWindowsPathSeparator = '\\';
constexpr char16_t kPlatformPathSeparator = kWindowsPathSeparator;
#else
constexpr char16_t kPosixPathSeparator = '/';
constexpr char16_t kPlatformPathSeparator = kPosixPathSeparator;
#endif

bool IsDescendantPath(const nsAString& aAuthorizedRoot,
                      const nsAString& aRequestedDescendant) {
  // Check the sub-directory path to see if it has the parent path as prefix.
  if (aRequestedDescendant.Equals(aAuthorizedRoot)) {
    return true;
  }

  if (!StringBeginsWith(/*aSource*/ aRequestedDescendant,
                        /*aSubstring*/ aAuthorizedRoot)) {
    return false;
  }

  // Require a path separator immediately after the granted prefix.
  const uint32_t prefixLen = aAuthorizedRoot.Length();
  if (prefixLen > 0 && aAuthorizedRoot.Last() == kPlatformPathSeparator) {
    return true;
  }

  if (aRequestedDescendant.Length() <= prefixLen ||
      aRequestedDescendant.CharAt(prefixLen) != kPlatformPathSeparator) {
    return false;
  }

  return true;
}

}  // namespace

/* static */
already_AddRefed<FileSystemSecurity> FileSystemSecurity::Get() {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::AssertIsInMainProcess();

  RefPtr<FileSystemSecurity> service = gFileSystemSecurity.get();
  return service.forget();
}

/* static */
already_AddRefed<FileSystemSecurity> FileSystemSecurity::GetOrCreate() {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::AssertIsInMainProcess();

  if (!gFileSystemSecurity) {
    gFileSystemSecurity = new FileSystemSecurity();
    ClearOnShutdown(&gFileSystemSecurity);
  }

  RefPtr<FileSystemSecurity> service = gFileSystemSecurity.get();
  return service.forget();
}

FileSystemSecurity::FileSystemSecurity() {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::AssertIsInMainProcess();
}

FileSystemSecurity::~FileSystemSecurity() {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::AssertIsInMainProcess();
}

void FileSystemSecurity::GrantAccessToContentProcess(
    ContentParentId aId, const nsAString& aDirectoryPath) {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::AssertIsInMainProcess();

  mPaths.WithEntryHandle(aId, [&](auto&& entry) {
    if (entry && entry.Data()->Contains(aDirectoryPath)) {
      return;
    }

    entry.OrInsertWith([] { return MakeUnique<nsTArray<nsString>>(); })
        ->AppendElement(aDirectoryPath);
  });
}

void FileSystemSecurity::Forget(ContentParentId aId) {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::AssertIsInMainProcess();

  mPaths.Remove(aId);
}

bool FileSystemSecurity::ContentProcessHasAccessTo(ContentParentId aId,
                                                   const nsAString& aPath) {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::ipc::AssertIsInMainProcess();

#if defined(XP_WIN)
  if (StringBeginsWith(aPath, u"..\\"_ns) ||
      FindInReadable(u"\\..\\"_ns, aPath) ||
      StringEndsWith(aPath, u"\\.."_ns)) {
    return false;
  }
#endif
  if (StringBeginsWith(aPath, u"../"_ns) || FindInReadable(u"/../"_ns, aPath) ||
      StringEndsWith(aPath, u"/.."_ns) || aPath.EqualsLiteral("..")) {
    return false;
  }

  nsTArray<nsString>* paths;
  if (!mPaths.Get(aId, &paths)) {
    return false;
  }

  MOZ_DIAGNOSTIC_ASSERT(paths);
  for (const auto& authorizedRoot : *paths) {
    if (IsDescendantPath(authorizedRoot, aPath)) {
      return true;
    }
  }

  return false;
}

}  // namespace mozilla::dom

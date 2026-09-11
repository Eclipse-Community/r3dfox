/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_NativeNt_h
#define mozilla_NativeNt_h

#include <stdint.h>
#include <windows.h>
#include <winnt.h>
#include <winternl.h>

#include <algorithm>
#include <utility>

#include "mozilla/ArrayUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"
#include "mozilla/Range.h"
#include "mozilla/Span.h"
#include "mozilla/WinHeaderOnlyUtils.h"
#include "mozilla/interceptor/MMPolicies.h"
#include "mozilla/interceptor/TargetFunction.h"

#if defined(MOZILLA_INTERNAL_API)
#  include "nsString.h"
#endif  // defined(MOZILLA_INTERNAL_API)

// The declarations within this #if block are intended to be used for initial
// process initialization ONLY. You probably don't want to be using these in
// normal Gecko code!
#if !defined(MOZILLA_INTERNAL_API)

extern "C" {

#  if !defined(STATUS_ACCESS_DENIED)
#    define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022L)
#  endif  // !defined(STATUS_ACCESS_DENIED)

#  if !defined(STATUS_DLL_NOT_FOUND)
#    define STATUS_DLL_NOT_FOUND ((NTSTATUS)0xC0000135L)
#  endif  // !defined(STATUS_DLL_NOT_FOUND)

#  if !defined(STATUS_UNSUCCESSFUL)
#    define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#  endif  // !defined(STATUS_UNSUCCESSFUL)

#  if !defined(STATUS_INFO_LENGTH_MISMATCH)
#    define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#  endif

enum SECTION_INHERIT { ViewShare = 1, ViewUnmap = 2 };

NTSTATUS NTAPI NtMapViewOfSection(
    HANDLE aSection, HANDLE aProcess, PVOID* aBaseAddress, ULONG_PTR aZeroBits,
    SIZE_T aCommitSize, PLARGE_INTEGER aSectionOffset, PSIZE_T aViewSize,
    SECTION_INHERIT aInheritDisposition, ULONG aAllocationType,
    ULONG aProtectionFlags);

NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE aProcess, PVOID aBaseAddress);

enum MEMORY_INFORMATION_CLASS {
  MemoryBasicInformation = 0,
  MemorySectionName = 2
};

// NB: When allocating, space for the buffer must also be included
typedef struct _MEMORY_SECTION_NAME {
  UNICODE_STRING mSectionFileName;
} MEMORY_SECTION_NAME, *PMEMORY_SECTION_NAME;

NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE aProcess, PVOID aBaseAddress,
                                    MEMORY_INFORMATION_CLASS aMemInfoClass,
                                    PVOID aMemInfo, SIZE_T aMemInfoLen,
                                    PSIZE_T aReturnLen);

LONG NTAPI RtlCompareUnicodeString(PCUNICODE_STRING aStr1,
                                   PCUNICODE_STRING aStr2,
                                   BOOLEAN aCaseInsensitive);

BOOLEAN NTAPI RtlEqualUnicodeString(PCUNICODE_STRING aStr1,
                                    PCUNICODE_STRING aStr2,
                                    BOOLEAN aCaseInsensitive);

NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW aOutVersionInformation);

VOID NTAPI RtlAcquireSRWLockExclusive(PSRWLOCK aLock);
VOID NTAPI RtlAcquireSRWLockShared(PSRWLOCK aLock);

VOID NTAPI RtlReleaseSRWLockExclusive(PSRWLOCK aLock);
VOID NTAPI RtlReleaseSRWLockShared(PSRWLOCK aLock);

ULONG NTAPI RtlNtStatusToDosError(NTSTATUS aStatus);
VOID NTAPI RtlSetLastWin32Error(DWORD aError);
DWORD NTAPI RtlGetLastWin32Error();

VOID NTAPI RtlRunOnceInitialize(PRTL_RUN_ONCE aRunOnce);

NTSTATUS NTAPI NtReadVirtualMemory(HANDLE aProcessHandle, PVOID aBaseAddress,
                                   PVOID aBuffer, SIZE_T aNumBytesToRead,
                                   PSIZE_T aNumBytesRead);

NTSTATUS NTAPI LdrLoadDll(PWCHAR aDllPath, PULONG aFlags,
                          PUNICODE_STRING aDllName, PHANDLE aOutHandle);

typedef ULONG(NTAPI* PRTL_RUN_ONCE_INIT_FN)(PRTL_RUN_ONCE, PVOID, PVOID*);
NTSTATUS NTAPI RtlRunOnceExecuteOnce(PRTL_RUN_ONCE aRunOnce,
                                     PRTL_RUN_ONCE_INIT_FN aInitFn,
                                     PVOID aContext, PVOID* aParameter);

}  // extern "C"

#endif  // !defined(MOZILLA_INTERNAL_API)

extern "C" {
PVOID NTAPI RtlAllocateHeap(PVOID aHeapHandle, ULONG aFlags, SIZE_T aSize);

PVOID NTAPI RtlReAllocateHeap(PVOID aHeapHandle, ULONG aFlags, LPVOID aMem,
                              SIZE_T aNewSize);

BOOLEAN NTAPI RtlFreeHeap(PVOID aHeapHandle, ULONG aFlags, PVOID aHeapBase);

BOOLEAN NTAPI RtlQueryPerformanceCounter(LARGE_INTEGER* aPerfCount);

#define RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE 1
#define RTL_DUPLICATE_UNICODE_STRING_ALLOCATE_NULL_STRING 2
NTSTATUS NTAPI RtlDuplicateUnicodeString(ULONG aFlags, PCUNICODE_STRING aSrc,
                                         PUNICODE_STRING aDest);

VOID NTAPI RtlFreeUnicodeString(PUNICODE_STRING aUnicodeString);
}  // extern "C"

namespace mozilla {
namespace nt {

/**
 * This class encapsulates a UNICODE_STRING that owns its own buffer. The
 * buffer is always NULL terminated, thus allowing us to cast to a wide C-string
 * without requiring any mutation.
 *
 * We only allow creation of this owned buffer from outside XUL.
 */
class AllocatedUnicodeString final {
 public:
  AllocatedUnicodeString() : mUnicodeString() {}

#if defined(MOZILLA_INTERNAL_API)
  AllocatedUnicodeString(const AllocatedUnicodeString& aOther) = delete;

  AllocatedUnicodeString& operator=(const AllocatedUnicodeString& aOther) =
      delete;
#else
  explicit AllocatedUnicodeString(PCUNICODE_STRING aSrc) {
    if (!aSrc) {
      mUnicodeString = {};
      return;
    }

    Duplicate(aSrc);
  }

  explicit AllocatedUnicodeString(const char* aSrc) {
    if (!aSrc) {
      mUnicodeString = {};
      return;
    }

    Duplicate(aSrc);
  }

  AllocatedUnicodeString(const AllocatedUnicodeString& aOther) {
    Duplicate(&aOther.mUnicodeString);
  }

  AllocatedUnicodeString& operator=(const AllocatedUnicodeString& aOther) {
    Clear();
    Duplicate(&aOther.mUnicodeString);
    return *this;
  }

  AllocatedUnicodeString& operator=(PCUNICODE_STRING aSrc) {
    Clear();
    Duplicate(aSrc);
    return *this;
  }
#endif  // defined(MOZILLA_INTERNAL_API)

  AllocatedUnicodeString(AllocatedUnicodeString&& aOther)
      : mUnicodeString(aOther.mUnicodeString) {
    aOther.mUnicodeString = {};
  }

  AllocatedUnicodeString& operator=(AllocatedUnicodeString&& aOther) {
    Clear();
    mUnicodeString = aOther.mUnicodeString;
    aOther.mUnicodeString = {};
    return *this;
  }

  ~AllocatedUnicodeString() { Clear(); }

  bool IsEmpty() const {
    return !mUnicodeString.Buffer || !mUnicodeString.Length;
  }

  operator PCUNICODE_STRING() const { return &mUnicodeString; }

  operator const WCHAR*() const { return mUnicodeString.Buffer; }

  USHORT CharLen() const { return mUnicodeString.Length / sizeof(WCHAR); }

#if defined(MOZILLA_INTERNAL_API)
  nsDependentString AsString() const {
    if (!mUnicodeString.Buffer) {
      return nsDependentString();
    }

    // We can use nsDependentString here as we guaranteed null termination
    // when we allocated the string.
    return nsDependentString(mUnicodeString.Buffer, CharLen());
  }
#endif  // defined(MOZILLA_INTERNAL_API)

 private:
#if !defined(MOZILLA_INTERNAL_API)
  void Duplicate(PCUNICODE_STRING aSrc) {
    MOZ_ASSERT(aSrc);

    // We duplicate with null termination so that this string may be used
    // as a wide C-string without any further manipulation.
    NTSTATUS ntStatus = ::RtlDuplicateUnicodeString(
        RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE, aSrc, &mUnicodeString);
    MOZ_ASSERT(NT_SUCCESS(ntStatus));
    if (!NT_SUCCESS(ntStatus)) {
      // Make sure that mUnicodeString does not contain bogus data
      // (since not all callers zero it out before invoking)
      mUnicodeString = {};
    }
  }

  void Duplicate(const char* aSrc) {
    MOZ_ASSERT(aSrc);

    ANSI_STRING ansiStr;
    RtlInitAnsiString(&ansiStr, aSrc);
    NTSTATUS ntStatus =
        ::RtlAnsiStringToUnicodeString(&mUnicodeString, &ansiStr, TRUE);
    MOZ_ASSERT(NT_SUCCESS(ntStatus));
    if (!NT_SUCCESS(ntStatus)) {
      mUnicodeString = {};
    }
  }
#endif  // !defined(MOZILLA_INTERNAL_API)

  void Clear() {
    if (!mUnicodeString.Buffer) {
      return;
    }

    ::RtlFreeUnicodeString(&mUnicodeString);
    mUnicodeString = {};
  }

  UNICODE_STRING mUnicodeString;
};

#if !defined(MOZILLA_INTERNAL_API)

struct MemorySectionNameBuf : public _MEMORY_SECTION_NAME {
  MemorySectionNameBuf() {
    mSectionFileName.Length = 0;
    mSectionFileName.MaximumLength = sizeof(mBuf);
    mSectionFileName.Buffer = mBuf;
  }

  MemorySectionNameBuf(const MemorySectionNameBuf& aOther) { *this = aOther; }

  MemorySectionNameBuf(MemorySectionNameBuf&& aOther) {
    *this = std::move(aOther);
  }

  // We cannot use default copy here because mSectionFileName.Buffer needs to
  // be updated to point to |this->mBuf|, not |aOther.mBuf|.
  MemorySectionNameBuf& operator=(const MemorySectionNameBuf& aOther) {
    mSectionFileName.Length = aOther.mSectionFileName.Length;
    mSectionFileName.MaximumLength = sizeof(mBuf);
    MOZ_ASSERT(mSectionFileName.Length <= mSectionFileName.MaximumLength);
    mSectionFileName.Buffer = mBuf;
    memcpy(mBuf, aOther.mBuf, aOther.mSectionFileName.Length);
    return *this;
  }

  MemorySectionNameBuf& operator=(MemorySectionNameBuf&& aOther) {
    mSectionFileName.Length = aOther.mSectionFileName.Length;
    aOther.mSectionFileName.Length = 0;
    mSectionFileName.MaximumLength = sizeof(mBuf);
    MOZ_ASSERT(mSectionFileName.Length <= mSectionFileName.MaximumLength);
    aOther.mSectionFileName.MaximumLength = sizeof(aOther.mBuf);
    mSectionFileName.Buffer = mBuf;
    memmove(mBuf, aOther.mBuf, mSectionFileName.Length);
    return *this;
  }

  // Native NT paths, so we can't assume MAX_PATH. Use a larger buffer.
  WCHAR mBuf[2 * MAX_PATH];

  bool IsEmpty() const {
    return !mSectionFileName.Buffer || !mSectionFileName.Length;
  }

  operator PCUNICODE_STRING() const { return &mSectionFileName; }
};

class MemorySectionNameOnHeap {
  UniquePtr<uint8_t[]> mBuffer;

  MemorySectionNameOnHeap() = default;
  explicit MemorySectionNameOnHeap(size_t aBufferLen)
      : mBuffer(MakeUnique<uint8_t[]>(aBufferLen)) {}

 public:
  static MemorySectionNameOnHeap GetBackingFilePath(HANDLE aProcess,
                                                    void* aSectionAddr) {
    SIZE_T bufferLen = MAX_PATH * 2;
    do {
      MemorySectionNameOnHeap sectionName(bufferLen);

      SIZE_T requiredBytes;
      NTSTATUS ntStatus = ::NtQueryVirtualMemory(
          aProcess, aSectionAddr, MemorySectionName, sectionName.mBuffer.get(),
          bufferLen, &requiredBytes);
      if (NT_SUCCESS(ntStatus)) {
        return sectionName;
      }

      if (ntStatus != STATUS_INFO_LENGTH_MISMATCH ||
          bufferLen >= requiredBytes) {
        break;
      }

      bufferLen = requiredBytes;
    } while (1);

    return MemorySectionNameOnHeap();
  }

  // Allow move & Disallow copy
  MemorySectionNameOnHeap(MemorySectionNameOnHeap&&) = default;
  MemorySectionNameOnHeap& operator=(MemorySectionNameOnHeap&&) = default;
  MemorySectionNameOnHeap(const MemorySectionNameOnHeap&) = delete;
  MemorySectionNameOnHeap& operator=(const MemorySectionNameOnHeap&) = delete;

  PCUNICODE_STRING AsUnicodeString() const {
    return reinterpret_cast<PCUNICODE_STRING>(mBuffer.get());
  }
};

inline bool FindCharInUnicodeString(const UNICODE_STRING& aStr, WCHAR aChar,
                                    uint16_t& aPos, uint16_t aStartIndex = 0) {
  const uint16_t aMaxIndex = aStr.Length / sizeof(WCHAR);

  for (uint16_t curIndex = aStartIndex; curIndex < aMaxIndex; ++curIndex) {
    if (aStr.Buffer[curIndex] == aChar) {
      aPos = curIndex;
      return true;
    }
  }

  return false;
}

inline bool IsHexDigit(WCHAR aChar) {
  return (aChar >= L'0' && aChar <= L'9') || (aChar >= L'A' && aChar <= L'F') ||
         (aChar >= L'a' && aChar <= L'f');
}

inline bool MatchUnicodeString(const UNICODE_STRING& aStr,
                               bool (*aPredicate)(WCHAR)) {
  WCHAR* cur = aStr.Buffer;
  WCHAR* end = &aStr.Buffer[aStr.Length / sizeof(WCHAR)];
  while (cur < end) {
    if (!aPredicate(*cur)) {
      return false;
    }

    ++cur;
  }

  return true;
}

inline bool Contains12DigitHexString(const UNICODE_STRING& aLeafName) {
  // Quick check: If the string is too short, don't bother
  // (We need at least 12 hex digits, one char for '.', and 3 for extension)
  const USHORT kMinLen = (12 + 1 + 3) * sizeof(wchar_t);
  if (aLeafName.Length < kMinLen) {
    return false;
  }

  uint16_t start, end;
  if (!FindCharInUnicodeString(aLeafName, L'.', start)) {
    return false;
  }

  ++start;
  if (!FindCharInUnicodeString(aLeafName, L'.', end, start)) {
    return false;
  }

  if (end - start != 12) {
    return false;
  }

  UNICODE_STRING test;
  test.Buffer = &aLeafName.Buffer[start];
  test.Length = (end - start) * sizeof(WCHAR);
  test.MaximumLength = test.Length;

  return MatchUnicodeString(test, &IsHexDigit);
}

inline bool IsFileNameAtLeast16HexDigits(const UNICODE_STRING& aLeafName) {
  // Quick check: If the string is too short, don't bother
  // (We need 16 hex digits, one char for '.', and 3 for extension)
  const USHORT kMinLen = (16 + 1 + 3) * sizeof(wchar_t);
  if (aLeafName.Length < kMinLen) {
    return false;
  }

  uint16_t dotIndex;
  if (!FindCharInUnicodeString(aLeafName, L'.', dotIndex)) {
    return false;
  }

  if (dotIndex < 16) {
    return false;
  }

  UNICODE_STRING test;
  test.Buffer = aLeafName.Buffer;
  test.Length = dotIndex * sizeof(WCHAR);
  test.MaximumLength = aLeafName.MaximumLength;

  return MatchUnicodeString(test, &IsHexDigit);
}

inline void GetLeafName(PUNICODE_STRING aDestString,
                        PCUNICODE_STRING aSrcString) {
  WCHAR* buf = aSrcString->Buffer;
  WCHAR* end = &aSrcString->Buffer[(aSrcString->Length / sizeof(WCHAR)) - 1];
  WCHAR* cur = end;
  while (cur >= buf) {
    if (*cur == L'\\') {
      break;
    }

    --cur;
  }

  // At this point, either cur points to the final backslash, or it points to
  // buf - 1. Either way, we're interested in cur + 1 as the desired buffer.
  aDestString->Buffer = cur + 1;
  aDestString->Length = (end - aDestString->Buffer + 1) * sizeof(WCHAR);
  aDestString->MaximumLength = aDestString->Length;
}

#endif  // !defined(MOZILLA_INTERNAL_API)

#if defined(MOZILLA_INTERNAL_API)

inline const nsDependentSubstring GetLeafName(const nsAString& aString) {
  auto it = aString.EndReading();
  size_t pos = aString.Length();
  while (it > aString.BeginReading()) {
    if (*(it - 1) == u'\\') {
      return Substring(aString, pos);
    }

    MOZ_ASSERT(pos > 0);
    --pos;
    --it;
  }

  return Substring(aString, 0);  // No backslash in the string
}

#endif  // defined(MOZILLA_INTERNAL_API)

inline char EnsureLowerCaseASCII(char aChar) {
  if (aChar >= 'A' && aChar <= 'Z') {
    aChar -= 'A' - 'a';
  }

  return aChar;
}

inline int StricmpASCII(const char* aLeft, const char* aRight) {
  char curLeft, curRight;

  do {
    curLeft = EnsureLowerCaseASCII(*(aLeft++));
    curRight = EnsureLowerCaseASCII(*(aRight++));
  } while (curLeft && curLeft == curRight);

  return curLeft - curRight;
}

inline int StrcmpASCII(const char* aLeft, const char* aRight) {
  char curLeft, curRight;

  do {
    curLeft = *(aLeft++);
    curRight = *(aRight++);
  } while (curLeft && curLeft == curRight);

  return curLeft - curRight;
}

inline size_t StrlenASCII(const char* aStr) {
  size_t len = 0;

  while (*(aStr++)) {
    ++len;
  }

  return len;
}

struct CodeViewRecord70 {
  uint32_t signature;
  GUID pdbSignature;
  uint32_t pdbAge;
  // A UTF-8 string, according to
  // https://github.com/Microsoft/microsoft-pdb/blob/082c5290e5aff028ae84e43affa8be717aa7af73/PDB/dbi/locator.cpp#L785
  char pdbFileName[1];
};

class MOZ_RAII PEHeaders final {
  /**
   * This structure is documented on MSDN as VS_VERSIONINFO, but is not present
   * in SDK headers because it cannot be specified as a C struct. The following
   * structure contains the fixed-length fields at the beginning of
   * VS_VERSIONINFO.
   */
  struct VS_VERSIONINFO_HEADER {
    WORD wLength;
    WORD wValueLength;
    WORD wType;
    WCHAR szKey[16];  // ArrayLength(L"VS_VERSION_INFO")
    // Additional data goes here, aligned on a 4-byte boundary
  };

 public:
  // The lowest two bits of an HMODULE are used as flags. Stripping those bits
  // from the HMODULE yields the base address of the binary's memory mapping.
  // (See LoadLibraryEx docs on MSDN)
  template <typename T>
  static T HModuleToBaseAddr(HMODULE aModule) {
    return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(aModule) &
                               ~uintptr_t(3));
  }

  explicit PEHeaders(void* aBaseAddress)
      : PEHeaders(reinterpret_cast<PIMAGE_DOS_HEADER>(aBaseAddress)) {}

  explicit PEHeaders(HMODULE aModule)
      : PEHeaders(HModuleToBaseAddr<PIMAGE_DOS_HEADER>(aModule)) {}

  explicit PEHeaders(PIMAGE_DOS_HEADER aMzHeader)
      : mMzHeader(aMzHeader),
        mPeHeader(nullptr),
        mImageLimit(nullptr),
        mIsImportDirectoryTampered(false) {
    if (!mMzHeader || mMzHeader->e_magic != IMAGE_DOS_SIGNATURE) {
      return;
    }

    mPeHeader = RVAToPtrUnchecked<PIMAGE_NT_HEADERS>(mMzHeader->e_lfanew);
    if (!mPeHeader || mPeHeader->Signature != IMAGE_NT_SIGNATURE) {
      return;
    }

    if (mPeHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) {
      return;
    }

    DWORD imageSize = mPeHeader->OptionalHeader.SizeOfImage;
    // This is a coarse-grained check to ensure that the image size is
    // reasonable. It we aren't big enough to contain headers, we have a
    // problem!
    if (imageSize < sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS)) {
      return;
    }

    mImageLimit = RVAToPtrUnchecked<void*>(imageSize - 1UL);

    PIMAGE_DATA_DIRECTORY importDirEntry =
        GetImageDirectoryEntryPtr(IMAGE_DIRECTORY_ENTRY_IMPORT);
    if (!importDirEntry) {
      return;
    }

    mIsImportDirectoryTampered = (importDirEntry->VirtualAddress >= imageSize);
  }

  explicit operator bool() const { return !!mImageLimit; }

  /**
   * This overload computes absolute virtual addresses relative to the base
   * address of the binary.
   */
  template <typename T, typename R>
  T RVAToPtr(R aRva) const {
    return RVAToPtr<T>(mMzHeader, aRva);
  }

  /**
   * This overload computes a result by adding aRva to aBase, but also ensures
   * that the resulting pointer falls within the bounds of this binary's memory
   * mapping.
   */
  template <typename T, typename R>
  T RVAToPtr(void* aBase, R aRva) const {
    if (!mImageLimit) {
      return nullptr;
    }

    char* absAddress = reinterpret_cast<char*>(aBase) + aRva;
    if (absAddress < reinterpret_cast<char*>(mMzHeader) ||
        absAddress > reinterpret_cast<char*>(mImageLimit)) {
      return nullptr;
    }

    return reinterpret_cast<T>(absAddress);
  }

  Maybe<Range<const uint8_t>> GetBounds() const {
    if (!mImageLimit) {
      return Nothing();
    }

    auto base = reinterpret_cast<const uint8_t*>(mMzHeader);
    DWORD imageSize = mPeHeader->OptionalHeader.SizeOfImage;
    return Some(Range(base, imageSize));
  }

  DWORD GetFileCharacteristics() const {
    return mPeHeader ? mPeHeader->FileHeader.Characteristics : 0;
  }

  bool IsWithinImage(const void* aAddress) const {
    uintptr_t addr = reinterpret_cast<uintptr_t>(aAddress);
    uintptr_t imageBase = reinterpret_cast<uintptr_t>(mMzHeader);
    uintptr_t imageLimit = reinterpret_cast<uintptr_t>(mImageLimit);
    return addr >= imageBase && addr <= imageLimit;
  }

  PIMAGE_IMPORT_DESCRIPTOR GetImportDirectory() const {
    // If the import directory is already tampered, we skip bounds check
    // because it could be located outside the mapped image.
    return mIsImportDirectoryTampered
               ? GetImageDirectoryEntry<PIMAGE_IMPORT_DESCRIPTOR,
                                        BoundsCheckPolicy::Skip>(
                     IMAGE_DIRECTORY_ENTRY_IMPORT)
               : GetImageDirectoryEntry<PIMAGE_IMPORT_DESCRIPTOR>(
                     IMAGE_DIRECTORY_ENTRY_IMPORT);
  }

  PIMAGE_RESOURCE_DIRECTORY GetResourceTable() const {
    return GetImageDirectoryEntry<PIMAGE_RESOURCE_DIRECTORY>(
        IMAGE_DIRECTORY_ENTRY_RESOURCE);
  }

  PIMAGE_DATA_DIRECTORY GetImageDirectoryEntryPtr(
      const uint32_t aDirectoryIndex, uint32_t* aOutRva = nullptr) const {
    if (aOutRva) {
      *aOutRva = 0;
    }

    IMAGE_OPTIONAL_HEADER& optionalHeader = mPeHeader->OptionalHeader;

    const uint32_t maxIndex = std::min(optionalHeader.NumberOfRvaAndSizes,
                                       DWORD(IMAGE_NUMBEROF_DIRECTORY_ENTRIES));
    if (aDirectoryIndex >= maxIndex) {
      return nullptr;
    }

    PIMAGE_DATA_DIRECTORY dirEntry =
        &optionalHeader.DataDirectory[aDirectoryIndex];
    if (aOutRva) {
      *aOutRva = reinterpret_cast<char*>(dirEntry) -
                 reinterpret_cast<char*>(mMzHeader);
      MOZ_ASSERT(*aOutRva);
    }

    return dirEntry;
  }

  bool GetVersionInfo(uint64_t& aOutVersion) const {
    // RT_VERSION == 16
    // Version resources require an id of 1
    auto root = FindResourceLeaf<VS_VERSIONINFO_HEADER*>(16, 1);
    if (!root) {
      return false;
    }

    VS_FIXEDFILEINFO* fixedInfo = GetFixedFileInfo(root);
    if (!fixedInfo) {
      return false;
    }

    aOutVersion = ((static_cast<uint64_t>(fixedInfo->dwFileVersionMS) << 32) |
                   static_cast<uint64_t>(fixedInfo->dwFileVersionLS));
    return true;
  }

  bool GetTimeStamp(DWORD& aResult) const {
    if (!(*this)) {
      return false;
    }

    aResult = mPeHeader->FileHeader.TimeDateStamp;
    return true;
  }

  bool GetImageSize(DWORD& aResult) const {
    if (!(*this)) {
      return false;
    }

    aResult = mPeHeader->OptionalHeader.SizeOfImage;
    return true;
  }

  bool GetCheckSum(DWORD& aResult) const {
    if (!(*this)) {
      return false;
    }

    aResult = mPeHeader->OptionalHeader.CheckSum;
    return true;
  }

  PIMAGE_IMPORT_DESCRIPTOR
  GetImportDescriptor(const char* aModuleNameASCII) const {
    for (PIMAGE_IMPORT_DESCRIPTOR curImpDesc = GetImportDirectory();
         IsValid(curImpDesc); ++curImpDesc) {
      auto curName = mIsImportDirectoryTampered
                         ? RVAToPtrUnchecked<const char*>(curImpDesc->Name)
                         : RVAToPtr<const char*>(curImpDesc->Name);
      if (!curName) {
        return nullptr;
      }

      if (StricmpASCII(aModuleNameASCII, curName)) {
        continue;
      }

      // curImpDesc now points to the IAT for the module we're interested in
      return curImpDesc;
    }

    return nullptr;
  }

  template <typename CallbackT>
  void EnumImportChunks(const CallbackT& aCallback) const {
    for (PIMAGE_IMPORT_DESCRIPTOR curImpDesc = GetImportDirectory();
         IsValid(curImpDesc); ++curImpDesc) {
      auto curName = mIsImportDirectoryTampered
                         ? RVAToPtrUnchecked<const char*>(curImpDesc->Name)
                         : RVAToPtr<const char*>(curImpDesc->Name);
      if (!curName) {
        continue;
      }

      aCallback(curName);
    }
  }

  /**
   * If |aBoundaries| is given, this method checks whether each IAT entry is
   * within the given range, and if any entry is out of the range, we return
   * Nothing().
   */
  Maybe<Span<IMAGE_THUNK_DATA>> GetIATThunksForModule(
      const char* aModuleNameASCII,
      const Range<const uint8_t>* aBoundaries = nullptr) const {
    PIMAGE_IMPORT_DESCRIPTOR impDesc = GetImportDescriptor(aModuleNameASCII);
    if (!impDesc) {
      return Nothing();
    }

    auto firstIatThunk =
        this->template RVAToPtr<PIMAGE_THUNK_DATA>(impDesc->FirstThunk);
    if (!firstIatThunk) {
      return Nothing();
    }

    // Find the length by iterating through the table until we find a null entry
    PIMAGE_THUNK_DATA curIatThunk = firstIatThunk;
    while (IsValid(curIatThunk)) {
      if (aBoundaries) {
        auto iatEntry =
            reinterpret_cast<const uint8_t*>(curIatThunk->u1.Function);
        if (iatEntry < aBoundaries->begin().get() ||
            iatEntry >= aBoundaries->end().get()) {
          return Nothing();
        }
      }

      ++curIatThunk;
    }

    return Some(Span(firstIatThunk, curIatThunk));
  }

  /**
   * Resources are stored in a three-level tree. To locate a particular entry,
   * you must supply a resource type, the resource id, and then the language id.
   * If aLangId == 0, we just resolve the first entry regardless of language.
   */
  template <typename T>
  T FindResourceLeaf(WORD aType, WORD aResId, WORD aLangId = 0) const {
    PIMAGE_RESOURCE_DIRECTORY topLevel = GetResourceTable();
    if (!topLevel) {
      return nullptr;
    }

    PIMAGE_RESOURCE_DIRECTORY_ENTRY typeEntry =
        FindResourceEntry(topLevel, aType);
    if (!typeEntry || !typeEntry->DataIsDirectory) {
      return nullptr;
    }

    auto idDir = RVAToPtr<PIMAGE_RESOURCE_DIRECTORY>(
        topLevel, typeEntry->OffsetToDirectory);
    PIMAGE_RESOURCE_DIRECTORY_ENTRY idEntry = FindResourceEntry(idDir, aResId);
    if (!idEntry || !idEntry->DataIsDirectory) {
      return nullptr;
    }

    auto langDir = RVAToPtr<PIMAGE_RESOURCE_DIRECTORY>(
        topLevel, idEntry->OffsetToDirectory);
    PIMAGE_RESOURCE_DIRECTORY_ENTRY langEntry;
    if (aLangId) {
      langEntry = FindResourceEntry(langDir, aLangId);
    } else {
      langEntry = FindFirstResourceEntry(langDir);
    }

    if (!langEntry || langEntry->DataIsDirectory) {
      return nullptr;
    }

    auto dataEntry =
        RVAToPtr<PIMAGE_RESOURCE_DATA_ENTRY>(topLevel, langEntry->OffsetToData);
    return dataEntry ? RVAToPtr<T>(dataEntry->OffsetToData) : nullptr;
  }

  template <size_t N>
  Maybe<Span<const uint8_t>> FindSection(const char (&aSecName)[N],
                                         DWORD aCharacteristicsMask) const {
    static_assert((N - 1) <= IMAGE_SIZEOF_SHORT_NAME,
                  "Section names must be at most 8 characters excluding null "
                  "terminator");

    if (!(*this)) {
      return Nothing();
    }

    Span<IMAGE_SECTION_HEADER> sectionTable = GetSectionTable();
    for (auto&& sectionHeader : sectionTable) {
      if (strncmp(reinterpret_cast<const char*>(sectionHeader.Name), aSecName,
                  IMAGE_SIZEOF_SHORT_NAME)) {
        continue;
      }

      if (!(sectionHeader.Characteristics & aCharacteristicsMask)) {
        // We found the section but it does not have the expected
        // characteristics
        return Nothing();
      }

      DWORD rva = sectionHeader.VirtualAddress;
      if (!rva) {
        return Nothing();
      }

      DWORD size = sectionHeader.Misc.VirtualSize;
      if (!size) {
        return Nothing();
      }

      auto base = RVAToPtr<const uint8_t*>(rva);
      return Some(Span(base, size));
    }

    return Nothing();
  }

  // There may be other code sections in the binary besides .text
  Maybe<Span<const uint8_t>> GetTextSectionInfo() const {
    return FindSection(".text", IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                                    IMAGE_SCN_MEM_READ);
  }

  static bool IsValid(PIMAGE_IMPORT_DESCRIPTOR aImpDesc) {
    return aImpDesc && aImpDesc->OriginalFirstThunk != 0;
  }

  static bool IsValid(PIMAGE_THUNK_DATA aImgThunk) {
    return aImgThunk && aImgThunk->u1.Ordinal != 0;
  }

  bool IsImportDirectoryTampered() const { return mIsImportDirectoryTampered; }

  FARPROC GetEntryPoint() const {
    // Use the unchecked version because the entrypoint may be tampered.
    return RVAToPtrUnchecked<FARPROC>(
        mPeHeader->OptionalHeader.AddressOfEntryPoint);
  }

  const CodeViewRecord70* GetPdbInfo() const {
    PIMAGE_DEBUG_DIRECTORY debugDirectory =
        GetImageDirectoryEntry<PIMAGE_DEBUG_DIRECTORY>(
            IMAGE_DIRECTORY_ENTRY_DEBUG);
    if (!debugDirectory) {
      return nullptr;
    }

    const CodeViewRecord70* debugInfo =
        RVAToPtr<CodeViewRecord70*>(debugDirectory->AddressOfRawData);
    return (debugInfo && debugInfo->signature == 'SDSR') ? debugInfo : nullptr;
  }

 private:
  enum class BoundsCheckPolicy { Default, Skip };

  template <typename T, BoundsCheckPolicy Policy = BoundsCheckPolicy::Default>
  T GetImageDirectoryEntry(const uint32_t aDirectoryIndex) const {
    PIMAGE_DATA_DIRECTORY dirEntry = GetImageDirectoryEntryPtr(aDirectoryIndex);
    if (!dirEntry) {
      return nullptr;
    }

    return Policy == BoundsCheckPolicy::Skip
               ? RVAToPtrUnchecked<T>(dirEntry->VirtualAddress)
               : RVAToPtr<T>(dirEntry->VirtualAddress);
  }

  // This private variant does not have bounds checks, because we need to be
  // able to resolve the bounds themselves.
  template <typename T, typename R>
  T RVAToPtrUnchecked(R aRva) const {
    return reinterpret_cast<T>(reinterpret_cast<char*>(mMzHeader) + aRva);
  }

  Span<IMAGE_SECTION_HEADER> GetSectionTable() const {
    MOZ_ASSERT(*this);
    auto base = RVAToPtr<PIMAGE_SECTION_HEADER>(
        &mPeHeader->OptionalHeader, mPeHeader->FileHeader.SizeOfOptionalHeader);
    // The Windows loader has an internal limit of 96 sections (per PE spec)
    auto numSections =
        std::min(mPeHeader->FileHeader.NumberOfSections, WORD(96));
    return Span{base, numSections};
  }

  PIMAGE_RESOURCE_DIRECTORY_ENTRY
  FindResourceEntry(PIMAGE_RESOURCE_DIRECTORY aCurLevel, WORD aId) const {
    if (!aCurLevel) {
      return nullptr;
    }

    // Immediately after the IMAGE_RESOURCE_DIRECTORY structure is an array
    // of IMAGE_RESOURCE_DIRECTORY_ENTRY structures. Since this function
    // searches by ID, we need to skip past any named entries before iterating.
    auto dirEnt =
        reinterpret_cast<PIMAGE_RESOURCE_DIRECTORY_ENTRY>(aCurLevel + 1) +
        aCurLevel->NumberOfNamedEntries;
    if (!(IsWithinImage(dirEnt) &&
          IsWithinImage(&dirEnt[aCurLevel->NumberOfIdEntries - 1].Id))) {
      return nullptr;
    }

    for (WORD i = 0; i < aCurLevel->NumberOfIdEntries; ++i) {
      if (dirEnt[i].Id == aId) {
        return &dirEnt[i];
      }
    }

    return nullptr;
  }

  PIMAGE_RESOURCE_DIRECTORY_ENTRY
  FindFirstResourceEntry(PIMAGE_RESOURCE_DIRECTORY aCurLevel) const {
    // Immediately after the IMAGE_RESOURCE_DIRECTORY structure is an array
    // of IMAGE_RESOURCE_DIRECTORY_ENTRY structures. We just return the first
    // entry, regardless of whether it is indexed by name or by id.
    auto dirEnt =
        reinterpret_cast<PIMAGE_RESOURCE_DIRECTORY_ENTRY>(aCurLevel + 1);
    WORD numEntries =
        aCurLevel->NumberOfNamedEntries + aCurLevel->NumberOfIdEntries;
    if (!numEntries) {
      return nullptr;
    }

    return dirEnt;
  }

  VS_FIXEDFILEINFO* GetFixedFileInfo(VS_VERSIONINFO_HEADER* aVerInfo) const {
    WORD length = aVerInfo->wLength;
    if (length < sizeof(VS_VERSIONINFO_HEADER)) {
      return nullptr;
    }

    const wchar_t kVersionInfoKey[] = L"VS_VERSION_INFO";
    if (::RtlCompareMemory(aVerInfo->szKey, kVersionInfoKey,
                           ArrayLength(kVersionInfoKey)) !=
        ArrayLength(kVersionInfoKey)) {
      return nullptr;
    }

    if (aVerInfo->wValueLength != sizeof(VS_FIXEDFILEINFO)) {
      // Fixed file info does not exist
      return nullptr;
    }

    WORD offset = sizeof(VS_VERSIONINFO_HEADER);

    uintptr_t base = reinterpret_cast<uintptr_t>(aVerInfo);
    // Align up to 4-byte boundary
#pragma warning(suppress : 4146)
    offset += (-(base + offset) & 3);

    if (offset >= length) {
      return nullptr;
    }

    auto result = reinterpret_cast<VS_FIXEDFILEINFO*>(base + offset);
    if (result->dwSignature != 0xFEEF04BD) {
      return nullptr;
    }

    return result;
  }

 private:
  PIMAGE_DOS_HEADER mMzHeader;
  PIMAGE_NT_HEADERS mPeHeader;
  void* mImageLimit;
  bool mIsImportDirectoryTampered;
};

// This class represents an export section of a local/remote process.
template <typename MMPolicy>
class MOZ_RAII PEExportSection {
  const MMPolicy& mMMPolicy;
  uintptr_t mImageBase;
  DWORD mOrdinalBase;
  DWORD mRvaDirStart;
  DWORD mRvaDirEnd;
  mozilla::interceptor::TargetObjectArray<MMPolicy, DWORD> mExportAddressTable;
  mozilla::interceptor::TargetObjectArray<MMPolicy, DWORD> mExportNameTable;
  mozilla::interceptor::TargetObjectArray<MMPolicy, WORD> mExportOrdinalTable;

  explicit PEExportSection(const MMPolicy& aMMPolicy)
      : mMMPolicy(aMMPolicy),
        mImageBase(0),
        mOrdinalBase(0),
        mRvaDirStart(0),
        mRvaDirEnd(0),
        mExportAddressTable(mMMPolicy),
        mExportNameTable(mMMPolicy),
        mExportOrdinalTable(mMMPolicy) {}

  PEExportSection(const MMPolicy& aMMPolicy, uintptr_t aImageBase,
                  DWORD aRvaDirStart, DWORD aRvaDirEnd,
                  const IMAGE_EXPORT_DIRECTORY& exportDir)
      : mMMPolicy(aMMPolicy),
        mImageBase(aImageBase),
        mOrdinalBase(exportDir.Base),
        mRvaDirStart(aRvaDirStart),
        mRvaDirEnd(aRvaDirEnd),
        mExportAddressTable(mMMPolicy,
                            mImageBase + exportDir.AddressOfFunctions,
                            exportDir.NumberOfFunctions),
        mExportNameTable(mMMPolicy, mImageBase + exportDir.AddressOfNames,
                         exportDir.NumberOfNames),
        mExportOrdinalTable(mMMPolicy,
                            mImageBase + exportDir.AddressOfNameOrdinals,
                            exportDir.NumberOfNames) {}

  static const PEExportSection Get(uintptr_t aImageBase,
                                   const MMPolicy& aMMPolicy) {
    mozilla::interceptor::TargetObject<MMPolicy, IMAGE_DOS_HEADER> mzHeader(
        aMMPolicy, aImageBase);
    if (!mzHeader || mzHeader->e_magic != IMAGE_DOS_SIGNATURE) {
      return PEExportSection(aMMPolicy);
    }

    mozilla::interceptor::TargetObject<MMPolicy, IMAGE_NT_HEADERS> peHeader(
        aMMPolicy, aImageBase + mzHeader->e_lfanew);
    if (!peHeader || peHeader->Signature != IMAGE_NT_SIGNATURE) {
      return PEExportSection(aMMPolicy);
    }

    if (peHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) {
      return PEExportSection(aMMPolicy);
    }

    const IMAGE_OPTIONAL_HEADER& optionalHeader = peHeader->OptionalHeader;

    DWORD imageSize = optionalHeader.SizeOfImage;
    // This is a coarse-grained check to ensure that the image size is
    // reasonable. It we aren't big enough to contain headers, we have a
    // problem!
    if (imageSize < sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS)) {
      return PEExportSection(aMMPolicy);
    }

    if (optionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
      return PEExportSection(aMMPolicy);
    }

    const IMAGE_DATA_DIRECTORY& exportDirectoryEntry =
        optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exportDirectoryEntry.VirtualAddress || !exportDirectoryEntry.Size) {
      return PEExportSection(aMMPolicy);
    }

    mozilla::interceptor::TargetObject<MMPolicy, IMAGE_EXPORT_DIRECTORY>
        exportDirectory(aMMPolicy,
                        aImageBase + exportDirectoryEntry.VirtualAddress);
    if (!exportDirectory || !exportDirectory->NumberOfFunctions) {
      return PEExportSection(aMMPolicy);
    }

    return PEExportSection(
        aMMPolicy, aImageBase, exportDirectoryEntry.VirtualAddress,
        exportDirectoryEntry.VirtualAddress + exportDirectoryEntry.Size,
        *exportDirectory.GetLocalBase());
  }

  FARPROC GetProcAddressByOrdinal(WORD aOrdinal) const {
    if (aOrdinal < mOrdinalBase) {
      return nullptr;
    }

    auto rvaToFunction = mExportAddressTable[aOrdinal - mOrdinalBase];
    if (!rvaToFunction) {
      return nullptr;
    }
    return reinterpret_cast<FARPROC>(mImageBase + *rvaToFunction);
  }

 public:
  static const PEExportSection Get(HMODULE aModule, const MMPolicy& aMMPolicy) {
    return Get(PEHeaders::HModuleToBaseAddr<uintptr_t>(aModule), aMMPolicy);
  }

  explicit operator bool() const {
    // Because PEExportSection doesn't use MMPolicy::Reserve(), a boolified
    // mMMPolicy is expected to be false.  We don't check mMMPolicy here.
    return mImageBase && mRvaDirStart && mRvaDirEnd && mExportAddressTable &&
           mExportNameTable && mExportOrdinalTable;
  }

  template <typename T>
  T RVAToPtr(uint32_t aRva) const {
    return reinterpret_cast<T>(mImageBase + aRva);
  }

  PIMAGE_EXPORT_DIRECTORY GetExportDirectory() const {
    if (!*this) {
      return nullptr;
    }

    return RVAToPtr<PIMAGE_EXPORT_DIRECTORY>(mRvaDirStart);
  }

  /**
   * This functions searches the export table for a given string as
   * GetProcAddress does, but this returns a matched entry of the Export
   * Address Table i.e. a pointer to an RVA of a matched function instead
   * of a function address.  If the entry is forwarded, this function
   * returns nullptr.
   */
  const DWORD* FindExportAddressTableEntry(
      const char* aFunctionNameASCII) const {
    if (!*this || !aFunctionNameASCII) {
      return nullptr;
    }

    struct NameTableComparator {
      NameTableComparator(const PEExportSection<MMPolicy>& aExportSection,
                          const char* aTarget)
          : mExportSection(aExportSection),
            mTargetName(aTarget),
            mTargetNamelength(StrlenASCII(aTarget)) {}

      int operator()(DWORD aRVAToString) const {
        mozilla::interceptor::TargetObjectArray<MMPolicy, char> itemString(
            mExportSection.mMMPolicy, mExportSection.mImageBase + aRVAToString,
            mTargetNamelength + 1);
        return StrcmpASCII(mTargetName, itemString[0]);
      }

      const PEExportSection<MMPolicy>& mExportSection;
      const char* mTargetName;
      size_t mTargetNamelength;
    };

    const NameTableComparator comp(*this, aFunctionNameASCII);

    size_t match;
    if (!mExportNameTable.BinarySearchIf(comp, &match)) {
      return nullptr;
    }

    const WORD* index = mExportOrdinalTable[match];
    if (!index) {
      return nullptr;
    }

    const DWORD* rvaToFunction = mExportAddressTable[*index];
    if (!rvaToFunction) {
      return nullptr;
    }

    if (*rvaToFunction >= mRvaDirStart && *rvaToFunction < mRvaDirEnd) {
      // If an entry points to an address within the export section, the
      // field is a forwarder RVA.  We return nullptr because the entry is
      // not a function address but a null-terminated string used for export
      // forwarding.
      return nullptr;
    }

    return rvaToFunction;
  }

  /**
   * This functions behaves the same as the native ::GetProcAddress except
   * the following cases:
   * - Returns nullptr if a target entry is forwarded to another dll.
   */
  FARPROC GetProcAddress(const char* aFunctionNameASCII) const {
    uintptr_t maybeOdrinal = reinterpret_cast<uintptr_t>(aFunctionNameASCII);
    // When the high-order word of |aFunctionNameASCII| is zero, it's not
    // a string but an ordinal value.
    if (maybeOdrinal < 0x10000) {
      return GetProcAddressByOrdinal(static_cast<WORD>(maybeOdrinal));
    }

    auto rvaToFunction = FindExportAddressTableEntry(aFunctionNameASCII);
    if (!rvaToFunction) {
      return nullptr;
    }
    return reinterpret_cast<FARPROC>(mImageBase + *rvaToFunction);
  }
};

inline HANDLE RtlGetProcessHeap() {
  PTEB teb = ::NtCurrentTeb();
  PPEB peb = teb->ProcessEnvironmentBlock;
  return peb->Reserved4[1];
}

inline PVOID RtlGetThreadLocalStoragePointer() {
  return ::NtCurrentTeb()->Reserved1[11];
}

inline void RtlSetThreadLocalStoragePointerForTestingOnly(PVOID aNewValue) {
  ::NtCurrentTeb()->Reserved1[11] = aNewValue;
}

inline DWORD RtlGetCurrentThreadId() {
  PTEB teb = ::NtCurrentTeb();
  CLIENT_ID* cid = reinterpret_cast<CLIENT_ID*>(&teb->Reserved1[8]);
  return static_cast<DWORD>(reinterpret_cast<uintptr_t>(cid->UniqueThread) &
                            0xFFFFFFFFUL);
}

inline PVOID RtlGetThreadStackBase() {
  return reinterpret_cast<_NT_TIB*>(::NtCurrentTeb())->StackBase;
}

inline PVOID RtlGetThreadStackLimit() {
  return reinterpret_cast<_NT_TIB*>(::NtCurrentTeb())->StackLimit;
}

const HANDLE kCurrentProcess = reinterpret_cast<HANDLE>(-1);

inline LauncherResult<DWORD> GetParentProcessId() {
  struct PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PPEB PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
  };

  ULONG returnLength;
  PROCESS_BASIC_INFORMATION pbi = {};
  NTSTATUS status =
      ::NtQueryInformationProcess(kCurrentProcess, ProcessBasicInformation,
                                  &pbi, sizeof(pbi), &returnLength);
  if (!NT_SUCCESS(status)) {
    return LAUNCHER_ERROR_FROM_NTSTATUS(status);
  }

  return static_cast<DWORD>(pbi.InheritedFromUniqueProcessId & 0xFFFFFFFF);
}

inline SIZE_T WINAPI VirtualQueryEx(HANDLE aProcess, LPCVOID aAddress,
                                    PMEMORY_BASIC_INFORMATION aMemInfo,
                                    SIZE_T aMemInfoLen) {
#if defined(MOZILLA_INTERNAL_API)
  return ::VirtualQueryEx(aProcess, aAddress, aMemInfo, aMemInfoLen);
#else
  SIZE_T returnedLength;
  NTSTATUS status = ::NtQueryVirtualMemory(
      aProcess, const_cast<PVOID>(aAddress), MemoryBasicInformation, aMemInfo,
      aMemInfoLen, &returnedLength);
  if (!NT_SUCCESS(status)) {
    ::RtlSetLastWin32Error(::RtlNtStatusToDosError(status));
    returnedLength = 0;
  }
  return returnedLength;
#endif  // defined(MOZILLA_INTERNAL_API)
}

inline SIZE_T WINAPI VirtualQuery(LPCVOID aAddress,
                                  PMEMORY_BASIC_INFORMATION aMemInfo,
                                  SIZE_T aMemInfoLen) {
  return nt::VirtualQueryEx(kCurrentProcess, aAddress, aMemInfo, aMemInfoLen);
}

struct DataDirectoryEntry : public _IMAGE_DATA_DIRECTORY {
  DataDirectoryEntry() : _IMAGE_DATA_DIRECTORY() {}

  MOZ_IMPLICIT DataDirectoryEntry(const _IMAGE_DATA_DIRECTORY& aOther)
      : _IMAGE_DATA_DIRECTORY(aOther) {}

  DataDirectoryEntry(const DataDirectoryEntry& aOther) = default;

  bool operator==(const DataDirectoryEntry& aOther) const {
    return VirtualAddress == aOther.VirtualAddress && Size == aOther.Size;
  }

  bool operator!=(const DataDirectoryEntry& aOther) const {
    return !(*this == aOther);
  }
};

inline LauncherResult<void*> GetProcessPebPtr(HANDLE aProcess) {
  ULONG returnLength;
  PROCESS_BASIC_INFORMATION pbi;
  NTSTATUS status = ::NtQueryInformationProcess(
      aProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &returnLength);
  if (!NT_SUCCESS(status)) {
    return LAUNCHER_ERROR_FROM_NTSTATUS(status);
  }

  return pbi.PebBaseAddress;
}

/**
 * This function relies on a specific offset into the mostly-undocumented PEB
 * structure. The risk is reduced thanks to the fact that the Chromium sandbox
 * relies on the location of this field. It is unlikely to change at this point.
 * To further reduce the risk, we also check for the magic 'MZ' signature that
 * should indicate the beginning of a PE image.
 */
inline LauncherResult<HMODULE> GetProcessExeModule(HANDLE aProcess) {
  LauncherResult<void*> ppeb = GetProcessPebPtr(aProcess);
  if (ppeb.isErr()) {
    return ppeb.propagateErr();
  }

  PEB peb;
  SIZE_T bytesRead;

#if defined(MOZILLA_INTERNAL_API)
  if (!::ReadProcessMemory(aProcess, ppeb.unwrap(), &peb, sizeof(peb),
                           &bytesRead) ||
      bytesRead != sizeof(peb)) {
    return LAUNCHER_ERROR_FROM_LAST();
  }
#else
  NTSTATUS ntStatus = ::NtReadVirtualMemory(aProcess, ppeb.unwrap(), &peb,
                                            sizeof(peb), &bytesRead);
  if (!NT_SUCCESS(ntStatus) || bytesRead != sizeof(peb)) {
    return LAUNCHER_ERROR_FROM_NTSTATUS(ntStatus);
  }
#endif

  // peb.ImageBaseAddress
  void* baseAddress = peb.Reserved3[1];

  char mzMagic[2];
#if defined(MOZILLA_INTERNAL_API)
  if (!::ReadProcessMemory(aProcess, baseAddress, mzMagic, sizeof(mzMagic),
                           &bytesRead) ||
      bytesRead != sizeof(mzMagic)) {
    return LAUNCHER_ERROR_FROM_LAST();
  }
#else
  ntStatus = ::NtReadVirtualMemory(aProcess, baseAddress, mzMagic,
                                   sizeof(mzMagic), &bytesRead);
  if (!NT_SUCCESS(ntStatus) || bytesRead != sizeof(mzMagic)) {
    return LAUNCHER_ERROR_FROM_NTSTATUS(ntStatus);
  }
#endif

  MOZ_ASSERT(mzMagic[0] == 'M' && mzMagic[1] == 'Z');
  if (mzMagic[0] != 'M' || mzMagic[1] != 'Z') {
    return LAUNCHER_ERROR_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
  }

  return static_cast<HMODULE>(baseAddress);
}

#if defined(_MSC_VER)
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif

// This class manages data transfer from the local process's executable
// to another process's executable via WriteProcessMemory.
// Bug 1662560 told us the same executable may be mapped onto a different
// address in a different process.  This means when we transfer data within
// the mapped executable such as a global variable or IAT from the current
// process to another process, we need to shift its address by the difference
// between two executable's mapped imagebase.
class CrossExecTransferManager final {
  HANDLE mRemoteProcess;
  uint8_t* mLocalImagebase;
  PEHeaders mLocalExec;
  uint8_t* mRemoteImagebase;

  static HMODULE GetLocalExecModule() {
#if defined(_MSC_VER)
    return reinterpret_cast<HMODULE>(&__ImageBase);
#else
    return ::GetModuleHandleW(nullptr);
#endif
  }

  LauncherVoidResult EnsureRemoteImagebase() {
    if (!mRemoteImagebase) {
      LauncherResult<HMODULE> remoteImageBaseResult =
          GetProcessExeModule(mRemoteProcess);
      if (remoteImageBaseResult.isErr()) {
        return remoteImageBaseResult.propagateErr();
      }

      mRemoteImagebase =
          reinterpret_cast<uint8_t*>(remoteImageBaseResult.unwrap());
    }
    return Ok();
  }

  template <typename T>
  T* LocalExecToRemoteExec(T* aLocalAddress) const {
    MOZ_ASSERT(mRemoteImagebase);
    MOZ_ASSERT(mLocalExec.IsWithinImage(aLocalAddress));

    if (!mRemoteImagebase || !mLocalExec.IsWithinImage(aLocalAddress)) {
      return aLocalAddress;
    }

    uintptr_t offset = reinterpret_cast<uintptr_t>(aLocalAddress) -
                       reinterpret_cast<uintptr_t>(mLocalImagebase);
    return reinterpret_cast<T*>(mRemoteImagebase + offset);
  }

 public:
  explicit CrossExecTransferManager(HANDLE aRemoteProcess)
      : mRemoteProcess(aRemoteProcess),
        mLocalImagebase(
            PEHeaders::HModuleToBaseAddr<uint8_t*>(GetLocalExecModule())),
        mLocalExec(mLocalImagebase),
        mRemoteImagebase(nullptr) {}

  CrossExecTransferManager(HANDLE aRemoteProcess, HMODULE aLocalImagebase)
      : mRemoteProcess(aRemoteProcess),
        mLocalImagebase(
            PEHeaders::HModuleToBaseAddr<uint8_t*>(aLocalImagebase)),
        mLocalExec(mLocalImagebase),
        mRemoteImagebase(nullptr) {}

  explicit operator bool() const { return !!mLocalExec; }
  HANDLE RemoteProcess() const { return mRemoteProcess; }
  const PEHeaders& LocalPEHeaders() const { return mLocalExec; }

  AutoVirtualProtect Protect(void* aLocalAddress, size_t aLength,
                             DWORD aProtFlags) {
    // If EnsureRemoteImagebase() fails, a subsequent operaion will fail.
    Unused << EnsureRemoteImagebase();
    return AutoVirtualProtect(LocalExecToRemoteExec(aLocalAddress), aLength,
                              aProtFlags, mRemoteProcess);
  }

  LauncherVoidResult Transfer(LPVOID aDestinationAddress,
                              LPCVOID aBufferToWrite, SIZE_T aBufferSize) {
    LauncherVoidResult result = EnsureRemoteImagebase();
    if (result.isErr()) {
      return result.propagateErr();
    }

    if (!::WriteProcessMemory(mRemoteProcess,
                              LocalExecToRemoteExec(aDestinationAddress),
                              aBufferToWrite, aBufferSize, nullptr)) {
      return LAUNCHER_ERROR_FROM_LAST();
    }

    return Ok();
  }
};

#if !defined(MOZILLA_INTERNAL_API)

inline LauncherResult<HMODULE> GetModuleHandleFromLeafName(
    const UNICODE_STRING& aTarget) {
  auto maybePeb = nt::GetProcessPebPtr(kCurrentProcess);
  if (maybePeb.isErr()) {
    return maybePeb.propagateErr();
  }

  const PPEB peb = reinterpret_cast<PPEB>(maybePeb.unwrap());
  if (!peb->Ldr) {
    return LAUNCHER_ERROR_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
  }

  auto firstItem = &peb->Ldr->InMemoryOrderModuleList;
  for (auto p = firstItem->Flink; p != firstItem; p = p->Flink) {
    const auto currentTableEntry =
        CONTAINING_RECORD(p, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

    UNICODE_STRING leafName;
    nt::GetLeafName(&leafName, &currentTableEntry->FullDllName);

    if (::RtlCompareUnicodeString(&leafName, &aTarget, TRUE) == 0) {
      return reinterpret_cast<HMODULE>(currentTableEntry->DllBase);
    }
  }

  return LAUNCHER_ERROR_FROM_WIN32(ERROR_MOD_NOT_FOUND);
}

/*++

Copyright (c) 2000  Microsoft Corporation

Module Name:

    pushlock.c

Abstract:

    This module houses routines that do push locking.

    Push locks are capable of being acquired in both shared and exclusive mode.

    Properties include:

    They can not be acquired recursively.
    They are small (the size of a pointer) and can be used when embeded in pagable data.
    Acquire and release is done lock free. On lock contention the waiters are chained
    through the lock and local stack space.

    This is the structure of a push lock:

    
    E  == Exclusive bit
    W  == Waiters present
    SC == Share count
    P  == Pointer to wait block
    +----------+---+---+
    |    SC    | E | W | E, W are single bits W == 0
    +----------+---+---+

    +--------------+---+
    |      P       | W | W == 1. Pointer is the address of a chain of stack local wait blocks
    +--------------+---+

    The non-contented acquires and releases are trivial. Interlocked operations make the following
    transformations.

    (SC=0,E=0,W=0) === Exclusive acquire ===> (SC=0,E=1,W=0)
    (SC=n,E=0,W=0) === Shared acquire    ===> (SC=n+1,E=0,W=0)

    (SC=0,E=1,W=0) === Exclusive release ===> (SC=0,E=0,W=0)
    (SC=n,E=0,W=0) === Shared release    ===> (SC=n-1,E=0,W=0) n > 0

    Contention causes the acquiring thread to produce a local stack based wait block and to
    enqueue it to the front of the list.

    (SC=n,E=e,W=0) === Exclusive acquire ===> (P=LWB(SSC=n,E=e),W=1) LWB = local wait block,
                                                                     SSC = Saved share count,
                                                                     n > 0 or e == 1.

    (SC=0,E=1,W=0) === Shared acquire    ===> (P=LWB(SSC=0,E=0),W=1) LWB = local wait block,
                                                                     SSC = Saved share count.

    After contention has causes one or more threads to queue up wait blocks releases are more
    complicated. This following rights are granted to a releasing thread (shared or exclusive).

    1) Shared release threads are allowed to search the wait list until they hit a wait block
       with a non-zero share count (this will be a wait block marked exclusive). This thread is
       allowed to use an interlocked operation to decrement this value. If this thread
       transitioned the value to zero then it obtains the rights of an exclusive release thread

    2) Exclusive threads are allowed to search the wait list until they find a continuous chain
       of shared wait blocks or they find the last wait block is an exclusive waiter. This thread
       may then break the chain at this point or update the header to show a single exclusive
       owner or multiple shared owners. Breaking the list can be done with normal assignment
       but updating the header requires interlocked exchange compare.


Author:

    Neill Clift (NeillC) 30-Sep-2000


Revision History:

--*/

//
// Push lock definitions
//
typedef struct _EX_PUSH_LOCK {

#define EX_PUSH_LOCK_WAITING   0x1
#define EX_PUSH_LOCK_EXCLUSIVE 0x2
#define EX_PUSH_LOCK_SHARE_INC 0x4

    union {
        struct {
            ULONG_PTR Waiting : 1;
            ULONG_PTR Exclusive : 1;
            ULONG_PTR Shared : sizeof (ULONG_PTR) * 8 - 2;
        };
        ULONG_PTR Value;
        PVOID Ptr;
    };
} EX_PUSH_LOCK, *PEX_PUSH_LOCK;

//
// This is a block held on the local stack of the waiting threads.
//

typedef  struct _EX_PUSH_LOCK_WAIT_BLOCK *PEX_PUSH_LOCK_WAIT_BLOCK;

typedef struct _EX_PUSH_LOCK_WAIT_BLOCK {
    HANDLE WakeEvent;
    PEX_PUSH_LOCK_WAIT_BLOCK Next;
    PEX_PUSH_LOCK_WAIT_BLOCK Last;
    PEX_PUSH_LOCK_WAIT_BLOCK Previous;
    ULONG ShareCount;
    BOOLEAN Exclusive;
} EX_PUSH_LOCK_WAIT_BLOCK;

#define NTKERNELAPI inline

#ifndef FASTCALL
#define FASTCALL __fastcall
#endif

#ifdef ASSERT
#undef ASSERT
#endif
#define ASSERT(e)        // get rid of the assert symbol.

#ifdef KeRaiseIrql
#undef KeRaiseIrql
#endif
#define KeRaiseIrql(NewLevel, OldLevel)
#ifdef KeLowerIrql
#undef KeLowerIrql
#endif
#define KeLowerIrql(Level)
//
// Interrupt Request Level (IRQL)
//

typedef UCHAR KIRQL;
typedef KIRQL *PKIRQL;

// Event type
//

typedef enum _EVENT_TYPE {
    NotificationEvent,
    SynchronizationEvent
    } EVENT_TYPE;

extern "C" NTSYSAPI NTSTATUS NTAPI NtCreateEvent(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, EVENT_TYPE, BOOLEAN);
extern "C" NTSYSCALLAPI NTSTATUS NTAPI NtSetEventBoostPriority (IN HANDLE EventHandle);

#pragma hdrstop

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, ExBlockPushLock)
#pragma alloc_text(PAGE, ExfAcquirePushLockExclusive)
#pragma alloc_text(PAGE, ExfAcquirePushLockShared)
#pragma alloc_text(PAGE, ExfUnblockPushLock)
#pragma alloc_text(PAGE, ExAllocateCacheAwarePushLock)
#pragma alloc_text(PAGE, ExFreeCacheAwarePushLock)
#pragma alloc_text(PAGE, ExAcquireCacheAwarePushLockExclusive)
#pragma alloc_text(PAGE, ExReleaseCacheAwarePushLockExclusive)
#endif


NTKERNELAPI
VOID
FASTCALL
ExfAcquirePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock exclusively

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;
    EX_PUSH_LOCK_WAIT_BLOCK WaitBlock;

    OldValue = *PushLock;
    while (1) {
        //
        // If the lock is already held exclusively/shared or there are waiters then
        // we need to wait.
        //
        if (OldValue.Value == 0) {
            NewValue.Value = OldValue.Value + EX_PUSH_LOCK_EXCLUSIVE;
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                break;
            }
        } else {
            NtCreateEvent(&WaitBlock.WakeEvent, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
            WaitBlock.Exclusive = TRUE;
            WaitBlock.Last = NULL;
            WaitBlock.Previous = NULL;
            //
            // Move the sharecount to our wait block if need be.
            //
            if (OldValue.Waiting) {
                WaitBlock.Next = (PEX_PUSH_LOCK_WAIT_BLOCK)
                                     (OldValue.Value - EX_PUSH_LOCK_WAITING);
                WaitBlock.ShareCount = 0;
            } else {
                WaitBlock.Next = NULL;
                WaitBlock.ShareCount = (ULONG) OldValue.Shared;
            }
            NewValue.Ptr = ((PUCHAR) &WaitBlock) + EX_PUSH_LOCK_WAITING;
            ASSERT ((NewValue.Value & EX_PUSH_LOCK_WAITING) != 0);
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                NtWaitForSingleObject(WaitBlock.WakeEvent, FALSE, NULL);
                ASSERT ((WaitBlock.ShareCount == 0) && (WaitBlock.Next == NULL));
                NtClose(WaitBlock.WakeEvent);
                break;
            }
            NtClose(WaitBlock.WakeEvent);

        }
        OldValue = NewValue;
    }
}

NTKERNELAPI
VOID
FASTCALL
ExfAcquirePushLockShared (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock shared

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;
    EX_PUSH_LOCK_WAIT_BLOCK WaitBlock;

    OldValue = *PushLock;
    while (1) {
        //
        // If the lock is already held exclusively or there are waiters then we need to wait
        //
        if (OldValue.Exclusive || OldValue.Waiting) {
            NtCreateEvent(&WaitBlock.WakeEvent, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
            WaitBlock.Exclusive = 0;
            WaitBlock.ShareCount = 0;
            WaitBlock.Last = NULL;
            WaitBlock.Previous = NULL;
            //
            // Chain the next block to us if there is one.
            //
            if (OldValue.Waiting) {
                WaitBlock.Next = (PEX_PUSH_LOCK_WAIT_BLOCK)
                                     (OldValue.Value - EX_PUSH_LOCK_WAITING);
            } else {
                WaitBlock.Next = NULL;
            }
            NewValue.Ptr = ((PUCHAR) &WaitBlock) + EX_PUSH_LOCK_WAITING;
            ASSERT ((NewValue.Value & EX_PUSH_LOCK_WAITING) != 0);
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                NtWaitForSingleObject(WaitBlock.WakeEvent, FALSE, NULL);

                ASSERT (WaitBlock.ShareCount == 0);
                NtClose(WaitBlock.WakeEvent);
                break;
            }
            NtClose(WaitBlock.WakeEvent);

        } else {
            //
            // We only have shared accessors at the moment. We can just update the lock to include this thread.
            //
            NewValue.Value = OldValue.Value + EX_PUSH_LOCK_SHARE_INC;
            ASSERT (!(NewValue.Waiting || NewValue.Exclusive));
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                break;
            }
        }
        OldValue = NewValue;
    }
}


NTKERNELAPI
VOID
FASTCALL
ExfReleasePushLock (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Release a push lock that was acquired exclusively or shared

Arguments:

    PushLock - Push lock to be released

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;
    PEX_PUSH_LOCK_WAIT_BLOCK WaitBlock, NextWaitBlock, ReleaseWaitList, Previous;
    PEX_PUSH_LOCK_WAIT_BLOCK LastWaitBlock, FirstWaitBlock;
    ULONG ShareCount;
    KIRQL OldIrql;

    OldValue = *PushLock;
    while (1) {
        if (!OldValue.Waiting) {
            //
            // Either we hold the lock exclusive or shared but not both.
            //
            ASSERT (OldValue.Exclusive ^ (OldValue.Shared > 0));

            //
            // We must hold the lock exclusive or shared. We make the assuption that
            // the exclusive bit is just below the share count here.
            //
            NewValue.Value = (OldValue.Value - EX_PUSH_LOCK_EXCLUSIVE) &
                             ~EX_PUSH_LOCK_EXCLUSIVE;
            NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                              NewValue.Ptr,
                                                              OldValue.Ptr);
            if (NewValue.Ptr == OldValue.Ptr) {
                break;
            }
            //
            // Either we gained a new waiter or another shared owner arrived or left
            //
            ASSERT (NewValue.Waiting || (NewValue.Shared > 0 && !NewValue.Exclusive));
            OldValue = NewValue;
        } else {
            //
            // There are waiters chained to the lock. We have to release the share count,
            // last exclusive or last chain of shared waiters.
            //
            WaitBlock = (PEX_PUSH_LOCK_WAIT_BLOCK) 
                           (OldValue.Value - EX_PUSH_LOCK_WAITING);

            FirstWaitBlock = WaitBlock;
            ReleaseWaitList = WaitBlock;
            Previous = NULL;
            LastWaitBlock = NULL;
            ShareCount = 0;
            do {

                if (WaitBlock->Last != NULL) {
                    LastWaitBlock = WaitBlock;
                    WaitBlock = WaitBlock->Last;
                    Previous = WaitBlock->Previous;
                    ReleaseWaitList = WaitBlock;
                    ASSERT (WaitBlock->Next == NULL);
                    ASSERT (Previous != NULL);
                    ShareCount = 0;
                }

                if (WaitBlock->Exclusive) {
                    //
                    // This is an exclusive waiter. If this was the first exclusive waited to a shared acquire
                    // then it will have the saved share count. If we acquired the lock shared then the count
                    // must contain a bias for this thread. Release that and if we are not the last shared
                    // accessor then exit. A later shared release thread will wake the exclusive
                    // waiter.
                    //
                    if (WaitBlock->ShareCount != 0) {
                        if (InterlockedDecrement ((PLONG)&WaitBlock->ShareCount) != 0) {
                            return;
                        }
                    }
                    //
                    // Reset count of share acquires waiting.
                    //
                    ShareCount = 0;
                } else {
                    //
                    // This is a shared waiter. Record the number of these to update the head or the
                    // previous exclusive waiter.
                    //
                    ShareCount++;
                }
                NextWaitBlock = WaitBlock->Next;
                if (NextWaitBlock != NULL) {

                    NextWaitBlock->Previous = WaitBlock;

                    if (NextWaitBlock->Exclusive) {
                        //
                        // The next block is exclusive. This may be the entry to free.
                        //
                        Previous = WaitBlock;
                        ReleaseWaitList = NextWaitBlock;
                    } else {
                        //
                        // The next block is shared. If the chain start is exclusive then skip to this one
                        // as the exclusive isn't the thread we will wake up.
                        //
                        if (ReleaseWaitList->Exclusive) {
                            Previous = WaitBlock;
                            ReleaseWaitList = NextWaitBlock;
                        }
                    }
                }

                WaitBlock = NextWaitBlock;
            } while (WaitBlock != NULL);

            //
            // If our release chain is everything then we have to update the header
            //
            if (Previous == NULL) {
                NewValue.Value = 0;
                NewValue.Exclusive = ReleaseWaitList->Exclusive;
                NewValue.Shared = ShareCount;
                ASSERT (((ShareCount > 0) ^ (ReleaseWaitList->Exclusive)) && !NewValue.Waiting);

                NewValue.Ptr = InterlockedCompareExchangePointer (&PushLock->Ptr,
                                                                  NewValue.Ptr,
                                                                  OldValue.Ptr);
                if (NewValue.Ptr != OldValue.Ptr) {
                    //
                    // We are releasing so we could have only gained another waiter
                    //
                    ASSERT (NewValue.Waiting);
                    OldValue = NewValue;
                    continue;
                }
            } else {

                if (LastWaitBlock != NULL) {
                    LastWaitBlock->Last = NULL;
                }
                //
                // Truncate the chain at this position and save the share count for all the shared owners to
                // decrement later.
                //
                Previous->Next = NULL;
                ASSERT (Previous->ShareCount == 0);
                Previous->ShareCount = ShareCount;

                //
                // Add a pointer to make future searches faster
                //
                if (Previous->Exclusive && FirstWaitBlock != Previous) {
                    FirstWaitBlock->Last = Previous;
                    ASSERT (Previous->Previous != NULL);
                }
                //
                // We are either releasing multiple share accessors or a single exclusive
                //
                ASSERT ((ShareCount > 0) ^ ReleaseWaitList->Exclusive);
            }

            //
            // If we are waking more than one thread then raise to DPC level to prevent us
            // getting rescheduled part way through the operation
            //

            OldIrql = DISPATCH_LEVEL;
            if (ShareCount > 1) {
                KeRaiseIrql (DISPATCH_LEVEL, &OldIrql);
            }

            //
            //
            // Release the chain of threads we located.
            //
            do {
                NextWaitBlock = ReleaseWaitList->Next;
                //
                // All the chain should have the same type (Exclusive/Shared).
                //
                ASSERT (NextWaitBlock == NULL || (ReleaseWaitList->Exclusive == NextWaitBlock->Exclusive));
                ASSERT (!ReleaseWaitList->Exclusive || (ReleaseWaitList->ShareCount == 0));
                NtSetEventBoostPriority (ReleaseWaitList->WakeEvent);
                ReleaseWaitList = NextWaitBlock;
            } while (ReleaseWaitList != NULL);

            if (OldIrql != DISPATCH_LEVEL) {
                KeLowerIrql (OldIrql);
            }


            break;
        }
    }
}

// begin_ntosp

VOID
FORCEINLINE
ExAcquirePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock exclusively

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           (PVOID)EX_PUSH_LOCK_EXCLUSIVE,
                                           NULL) != NULL) {
        ExfAcquirePushLockExclusive (PushLock);
    }
}

BOOLEAN
FORCEINLINE
ExTryAcquirePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Try and acquire a push lock exclusively

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    BOOLEAN - TRUE: Acquire was successfull, FALSE: Lock was already acquired

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           (PVOID)EX_PUSH_LOCK_EXCLUSIVE,
                                           NULL) == NULL) {
        return TRUE;
    } else {
        return FALSE;
    }
}

VOID
FORCEINLINE
ExAcquirePushLockShared (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock shared

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;

    OldValue = *PushLock;
    OldValue.Value &= ~(EX_PUSH_LOCK_EXCLUSIVE | EX_PUSH_LOCK_WAITING);
    NewValue.Value = OldValue.Value + EX_PUSH_LOCK_SHARE_INC;
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NewValue.Ptr,
                                           OldValue.Ptr) != OldValue.Ptr) {
        ExfAcquirePushLockShared (PushLock);
    }
}

VOID
FORCEINLINE
ExAcquirePushLockSharedAssumeNoOwner (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Acquire a push lock shared making the assumption that its not currently owned.

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    None

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           (PVOID)EX_PUSH_LOCK_SHARE_INC,
                                           NULL) != NULL) {
        ExfAcquirePushLockShared (PushLock);
    }
}

BOOLEAN
FORCEINLINE
ExTryConvertPushLockSharedToExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Attempts to convert a shared acquire to exclusive. If other sharers or waiters are present
    the function fails.

Arguments:

    PushLock - Push lock to be acquired

Return Value:

    BOOLEAN - TRUE: Conversion worked ok, FALSE: The conversion could not be achieved

--*/
{
    if (InterlockedCompareExchangePointer (&PushLock->Ptr, (PVOID) EX_PUSH_LOCK_EXCLUSIVE,
                                           (PVOID) EX_PUSH_LOCK_SHARE_INC) ==
                                               (PVOID)EX_PUSH_LOCK_SHARE_INC) {
        return TRUE;
    } else {
        return FALSE;
    }
}

VOID
FORCEINLINE
ExReleasePushLock (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Release a push lock that was acquired exclusively or shared

Arguments:

    PushLock - Push lock to be released

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;

    OldValue = *PushLock;
    OldValue.Value &= ~EX_PUSH_LOCK_WAITING;
    NewValue.Value = (OldValue.Value - EX_PUSH_LOCK_EXCLUSIVE) &
                         ~EX_PUSH_LOCK_EXCLUSIVE;
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NewValue.Ptr,
                                           OldValue.Ptr) != OldValue.Ptr) {
        ExfReleasePushLock (PushLock);
    }
}

VOID
FORCEINLINE
ExReleasePushLockExclusive (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Release a push lock that was acquired exclusively

Arguments:

    PushLock - Push lock to be released

Return Value:

    None

--*/
{
    ASSERT (PushLock->Value & (EX_PUSH_LOCK_WAITING|EX_PUSH_LOCK_EXCLUSIVE));

    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NULL,
                                           (PVOID)EX_PUSH_LOCK_EXCLUSIVE) != (PVOID)EX_PUSH_LOCK_EXCLUSIVE) {
        ExfReleasePushLock (PushLock);
    }
}

VOID
FORCEINLINE
ExReleasePushLockShared (
     IN PEX_PUSH_LOCK PushLock
     )
/*++

Routine Description:

    Release a push lock that was acquired shared

Arguments:

    PushLock - Push lock to be released

Return Value:

    None

--*/
{
    EX_PUSH_LOCK OldValue, NewValue;

    OldValue = *PushLock;
    ASSERT (OldValue.Waiting || !OldValue.Exclusive);
    OldValue.Value &= ~EX_PUSH_LOCK_WAITING;
    NewValue.Value = OldValue.Value - EX_PUSH_LOCK_SHARE_INC;
    if (InterlockedCompareExchangePointer (&PushLock->Ptr,
                                           NewValue.Ptr,
                                           OldValue.Ptr) != OldValue.Ptr) {
        ExfReleasePushLock (PushLock);
    }
}

class MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS SRWLock final {
 public:
  constexpr SRWLock() : mLock(SRWLOCK_INIT) {}

  void LockShared() { ExAcquirePushLockShared((PEX_PUSH_LOCK)&mLock); }

  void LockExclusive() { ExAcquirePushLockExclusive((PEX_PUSH_LOCK)&mLock); }

  void UnlockShared() { ExReleasePushLockShared((PEX_PUSH_LOCK)&mLock); }

  void UnlockExclusive() { ExReleasePushLockExclusive((PEX_PUSH_LOCK)&mLock); }

  SRWLock(const SRWLock&) = delete;
  SRWLock(SRWLock&&) = delete;
  SRWLock& operator=(const SRWLock&) = delete;
  SRWLock& operator=(SRWLock&&) = delete;

  SRWLOCK* operator&() { return &mLock; }

 private:
  SRWLOCK mLock;
};

class MOZ_RAII AutoExclusiveLock final {
 public:
  explicit AutoExclusiveLock(SRWLock& aLock) : mLock(aLock) {
    aLock.LockExclusive();
  }

  ~AutoExclusiveLock() { mLock.UnlockExclusive(); }

  AutoExclusiveLock(const AutoExclusiveLock&) = delete;
  AutoExclusiveLock(AutoExclusiveLock&&) = delete;
  AutoExclusiveLock& operator=(const AutoExclusiveLock&) = delete;
  AutoExclusiveLock& operator=(AutoExclusiveLock&&) = delete;

 private:
  SRWLock& mLock;
};

class MOZ_RAII AutoSharedLock final {
 public:
  explicit AutoSharedLock(SRWLock& aLock) : mLock(aLock) { aLock.LockShared(); }

  ~AutoSharedLock() { mLock.UnlockShared(); }

  AutoSharedLock(const AutoSharedLock&) = delete;
  AutoSharedLock(AutoSharedLock&&) = delete;
  AutoSharedLock& operator=(const AutoSharedLock&) = delete;
  AutoSharedLock& operator=(AutoSharedLock&&) = delete;

 private:
  SRWLock& mLock;
};

#endif  // !defined(MOZILLA_INTERNAL_API)

class RtlAllocPolicy {
 public:
  template <typename T>
  T* maybe_pod_malloc(size_t aNumElems) {
    if (aNumElems & mozilla::tl::MulOverflowMask<sizeof(T)>::value) {
      return nullptr;
    }

    return static_cast<T*>(
        ::RtlAllocateHeap(RtlGetProcessHeap(), 0, aNumElems * sizeof(T)));
  }

  template <typename T>
  T* maybe_pod_calloc(size_t aNumElems) {
    if (aNumElems & mozilla::tl::MulOverflowMask<sizeof(T)>::value) {
      return nullptr;
    }

    return static_cast<T*>(::RtlAllocateHeap(
        RtlGetProcessHeap(), HEAP_ZERO_MEMORY, aNumElems * sizeof(T)));
  }

  template <typename T>
  T* maybe_pod_realloc(T* aPtr, size_t aOldSize, size_t aNewSize) {
    if (aNewSize & mozilla::tl::MulOverflowMask<sizeof(T)>::value) {
      return nullptr;
    }

    return static_cast<T*>(::RtlReAllocateHeap(RtlGetProcessHeap(), 0, aPtr,
                                               aNewSize * sizeof(T)));
  }

  template <typename T>
  T* pod_malloc(size_t aNumElems) {
    return maybe_pod_malloc<T>(aNumElems);
  }

  template <typename T>
  T* pod_calloc(size_t aNumElems) {
    return maybe_pod_calloc<T>(aNumElems);
  }

  template <typename T>
  T* pod_realloc(T* aPtr, size_t aOldSize, size_t aNewSize) {
    return maybe_pod_realloc<T>(aPtr, aOldSize, aNewSize);
  }

  template <typename T>
  void free_(T* aPtr, size_t aNumElems = 0) {
    ::RtlFreeHeap(RtlGetProcessHeap(), 0, aPtr);
  }

  void reportAllocOverflow() const {}

  [[nodiscard]] bool checkSimulatedOOM() const { return true; }
};

class AutoMappedView final {
  void* mView;

  void Unmap() {
    if (!mView) {
      return;
    }

#if defined(MOZILLA_INTERNAL_API)
    ::UnmapViewOfFile(mView);
#else
    NTSTATUS status = ::NtUnmapViewOfSection(nt::kCurrentProcess, mView);
    if (!NT_SUCCESS(status)) {
      ::RtlSetLastWin32Error(::RtlNtStatusToDosError(status));
    }
#endif
    mView = nullptr;
  }

 public:
  explicit AutoMappedView(void* aView) : mView(aView) {}

  AutoMappedView(HANDLE aSection, ULONG aProtectionFlags) : mView(nullptr) {
#if defined(MOZILLA_INTERNAL_API)
    mView = ::MapViewOfFile(aSection, aProtectionFlags, 0, 0, 0);
#else
    SIZE_T viewSize = 0;
    NTSTATUS status = ::NtMapViewOfSection(aSection, nt::kCurrentProcess,
                                           &mView, 0, 0, nullptr, &viewSize,
                                           ViewUnmap, 0, aProtectionFlags);
    if (!NT_SUCCESS(status)) {
      ::RtlSetLastWin32Error(::RtlNtStatusToDosError(status));
    }
#endif
  }
  ~AutoMappedView() { Unmap(); }

  // Allow move & Disallow copy
  AutoMappedView(AutoMappedView&& aOther) : mView(aOther.mView) {
    aOther.mView = nullptr;
  }
  AutoMappedView& operator=(AutoMappedView&& aOther) {
    if (this != &aOther) {
      Unmap();
      mView = aOther.mView;
      aOther.mView = nullptr;
    }
    return *this;
  }
  AutoMappedView(const AutoMappedView&) = delete;
  AutoMappedView& operator=(const AutoMappedView&) = delete;

  explicit operator bool() const { return !!mView; }
  template <typename T>
  T* as() {
    return reinterpret_cast<T*>(mView);
  }

  void* release() {
    void* p = mView;
    mView = nullptr;
    return p;
  }
};

#if defined(_M_X64)
// CheckStack ensures that stack memory pages are committed up to a given size
// in bytes from the current stack pointer. It updates the thread stack limit,
// which points to the lowest committed stack address.
MOZ_NEVER_INLINE MOZ_NAKED inline void CheckStack(uint32_t size) {
  asm volatile(
      "mov %ecx, %eax;"
#  if defined(__MINGW32__)
      "jmp ___chkstk_ms;"
#  else
      "jmp __chkstk;"
#  endif  // __MINGW32__
  );
}
#endif  // _M_X64

}  // namespace nt
}  // namespace mozilla

#endif  // mozilla_NativeNt_h

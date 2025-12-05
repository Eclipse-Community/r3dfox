/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <optional>
#include <rpc.h>
#include <string>
#include <windows.h>

static const size_t BUFFER_LEN = MAX_PATH + 1;

/**
 * Create a unique tempfile name in a temp directory appropriate
 * for this user.
 * @return The temp filename, or std::nullopt on failure
 */
std::optional<std::wstring> get_tempfile_name() {
  wchar_t pathBuffer[BUFFER_LEN];
  wchar_t filenameBuffer[BUFFER_LEN];
  UUID uuid;
  DWORD pathLen = 0;
typedef DWORD ( *MYPROC)(DWORD, LPWSTR);
    HINSTANCE hinstLib;
    MYPROC procGetTempPath2W;
    BOOL fRunTimeLinkSuccess = FALSE;

    // Get a handle to the DLL module.

    hinstLib = LoadLibraryW(L"Kernel32.dll");

    // If the handle is valid, try to get the function address.

    if (hinstLib != NULL)
    {
        procGetTempPath2W = (MYPROC) GetProcAddress(hinstLib, "GetTempPath2W");

        // If the function address is valid, call the function.

        if (NULL != procGetTempPath2W)
        {
            fRunTimeLinkSuccess = TRUE;
            pathLen = procGetTempPath2W(BUFFER_LEN, pathBuffer);
        }
        // Free the DLL module.

        FreeLibrary(hinstLib);
    }

    // If unable to call the DLL function, use an alternative.
    if (! fRunTimeLinkSuccess)
        pathLen = GetTempPathW(BUFFER_LEN, pathBuffer);

  if (pathLen > BUFFER_LEN || pathLen == 0) {
    // Error getting path
    return std::nullopt;
  }
  // Use a UUID as a convenient source of random bits
  RPC_STATUS uuidStatus = UuidCreate(&uuid);
  if (uuidStatus != RPC_S_OK && uuidStatus != RPC_S_UUID_LOCAL_ONLY) {
    // Error creating a unique UUID for the filename
    return std::nullopt;
  }

  // Since we're only using the UUID data structure as a source of random bits,
  // rather than as something that we need to serialize and deserialize as a
  // UUID, it's best not to leak the internal implementation details out of the
  // abstraction of the filename.
  // With that in mind, the following code converts the UUID data structure into
  // a sequence of hexadecimal digits.
  ULONG data4msb =
      *((ULONG*)uuid.Data4);  // First 4 bytes of the 8-byte Data4 char array
  ULONG data4lsb =
      *((ULONG*)(uuid.Data4 +
                 4));  // Second 4 bytes of the 8-byte Data4 char array
  if (swprintf_s(filenameBuffer, BUFFER_LEN, L"%sfx%X%X%X%X%X.exe", pathBuffer,
                 uuid.Data1, uuid.Data2, uuid.Data3, data4msb, data4lsb) < 0) {
    return std::nullopt;
  }

  return std::wstring(filenameBuffer);
}

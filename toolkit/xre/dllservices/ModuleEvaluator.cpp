/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "ModuleEvaluator.h"

#include <algorithm>  // For std::find()
#include <type_traits>

#include <windows.h>
#include <shlobj.h>

#include "mozilla/ModuleVersionInfo.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WinDllServices.h"
#include "mozilla/WinHeaderOnlyUtils.h"
#include "nsReadableUtils.h"
#include "nsWindowsHelpers.h"
#include "nsXULAppAPI.h"
#include "prenv.h"

namespace mozilla {

// Fills a Vector with keyboard layout DLLs found in the registry.
// These are leaf names only, not full paths. Here we will convert them to
// lowercase before returning, to facilitate case-insensitive searches.
// On error, this may return partial results.
static Vector<nsString> GetKeyboardLayoutDlls() {
  Vector<nsString> result;

  HKEY rawKey;
  if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
                      0, KEY_ENUMERATE_SUB_KEYS, &rawKey) != ERROR_SUCCESS) {
    return result;
  }
  nsAutoRegKey key(rawKey);

  DWORD iKey = 0;
  wchar_t strTemp[MAX_PATH] = {};
  while (true) {
    DWORD strTempSize = std::size(strTemp);
    if (RegEnumKeyExW(rawKey, iKey, strTemp, &strTempSize, nullptr, nullptr,
                      nullptr, nullptr) != ERROR_SUCCESS) {
      // ERROR_NO_MORE_ITEMS or a real error: bail with what we have.
      return result;
    }
    iKey++;

    strTempSize = sizeof(strTemp);
    if (::RegGetValueW(rawKey, strTemp, L"Layout File", RRF_RT_REG_SZ, nullptr,
                       strTemp, &strTempSize) == ERROR_SUCCESS &&
        strTempSize) {
      nsString ws(strTemp, ((strTempSize + 1) / sizeof(wchar_t)) - 1);
      ToLowerCase(ws);  // To facilitate case-insensitive searches
      (void)result.emplaceBack(std::move(ws));
    }
  }
}

// 
// SHGetKnownFolderPath begin
#include <shellapi.h>
#include <shlobj.h>
#include <guiddef.h>
#include <userenv.h>
// compiler include	Userenv.lib


WCHAR* FOLDERID_Desktop_Path_Pointer = 0;
WCHAR* FOLDERID_InternetFolder_Path_Pointer = 0; // 0x0001 "InternetFolder"
WCHAR* FOLDERID_Programs_Path_Pointer = 0;
WCHAR* FOLDERID_ControlPanelFolder_Path_Pointer = 0;
WCHAR* FOLDERID_PrintersFolder_Path_Pointer = 0;
WCHAR* FOLDERID_LocalDocuments_Path_Pointer = 0;
WCHAR* FOLDERID_Favorites_Path_Pointer = 0;
WCHAR* FOLDERID_Startup_Path_Pointer = 0; // 0x0007 // "Startup"
WCHAR* FOLDERID_Recent_Path_Pointer = 0;
WCHAR* FOLDERID_SendTo_Path_Pointer = 0;
WCHAR* FOLDERID_RecycleBinFolder_Path_Pointer = 0;
WCHAR* FOLDERID_StartMenu_Path_Pointer = 0;
WCHAR* FOLDERID_Documents_Path_Pointer = 0;
WCHAR* FOLDERID_Music_Path_Pointer = 0;
WCHAR* FOLDERID_Videos_Path_Pointer = 0;
WCHAR* FOLDERID_PublicDesktop_Path_Pointer = 0; // 0x0010 // // <user name>\Desktop
WCHAR* FOLDERID_ComputerFolder_Path_Pointer = 0; // 0x0011 // "DriveFolder"
WCHAR* FOLDERID_NetworkFolder_Path_Pointer = 0;
WCHAR* FOLDERID_NetHood_Path_Pointer = 0;
WCHAR* FOLDERID_Fonts_Path_Pointer = 0; // 0x0014 // "Fonts"
WCHAR* FOLDERID_Templates_Path_Pointer = 0;
WCHAR* FOLDERID_CommonStartMenu_Path_Pointer = 0;
WCHAR* FOLDERID_CommonPrograms_Path_Pointer = 0;
WCHAR* FOLDERID_CommonStartup_Path_Pointer = 0;
WCHAR* FOLDERID_AppDataDesktop_Path_Pointer = 0; // 0x0019 // "Common Desktop"
WCHAR* FOLDERID_RoamingAppData_Path_Pointer = 0;
WCHAR* FOLDERID_PrintHood_Path_Pointer = 0;
WCHAR* FOLDERID_LocalAppData_Path_Pointer = 0; // 0x001c // "Local AppData"
WCHAR* FOLDERID_StartMenuAllPrograms_Path_Pointer = 0;
WCHAR* FOLDERID_CommonStartMenuPlaces_Pointer = 0;
WCHAR* FOLDERID_AppDataFavorites_Path_Pointer = 0;
WCHAR* FOLDERID_InternetCache_Path_Pointer = 0; // 0x0020 // "Cache"
WCHAR* FOLDERID_Cookies_Path_Pointer = 0;
WCHAR* FOLDERID_History_Path_Pointer = 0;
WCHAR* FOLDERID_LocalAppDataLow_Path_Pointer = 0;
WCHAR* FOLDERID_Windows_Path_Pointer = 0; // 0x0024 // "Windows"
WCHAR* FOLDERID_System_Path_Pointer = 0;
WCHAR* FOLDERID_ProgramFiles_Path_Pointer = 0;
WCHAR* FOLDERID_Pictures_Path_Pointer = 0;
WCHAR* FOLDERID_Profile_Path_Pointer = 0;
WCHAR* FOLDERID_SystemX86_Path_Pointer = 0;
WCHAR* FOLDERID_ProgramFilesX86_Path_Pointer = 0;
WCHAR* FOLDERID_ProgramFilesCommon_Path_Pointer = 0;
WCHAR* FOLDERID_ProgramFilesCommonX86_Path_Pointer = 0;
WCHAR* FOLDERID_CommonTemplates_Path_Pointer = 0;
WCHAR* FOLDERID_PublicDocuments_Path_Pointer = 0;
WCHAR* FOLDERID_CommonAdminTools_Path_Pointer = 0;
WCHAR* FOLDERID_AdminTools_Path_Pointer = 0; // 0x0030 // "Administrative Tools"
WCHAR* FOLDERID_ConnectionsFolder_Path_Pointer = 0;
WCHAR* FOLDERID_PublicMusic_Path_Pointer = 0;
WCHAR* FOLDERID_PublicPictures_Path_Pointer = 0;
WCHAR* FOLDERID_PublicVideos_Path_Pointer = 0;
WCHAR* FOLDERID_ResourceDir_Path_Pointer = 0;
WCHAR* FOLDERID_LocalizedResourcesDir_Path_Pointer = 0;
WCHAR* FOLDERID_CommonOEMLinks_Path_Pointer = 0;
WCHAR* FOLDERID_CDBurning_Path_Pointer = 0;
WCHAR* FFOLDERID_NetworkFolder_Path_Pointer = 0; // 0x003b // "ComputersNearMe"
// extra misc
WCHAR* FOLDERID_Public_Path_Pointer = 0;
WCHAR* FOLDERID_QuickLaunch_Path_Pointer = 0;
WCHAR* FOLDERID_UserProfiles_Path_Pointer = 0;
WCHAR* FOLDERID_Downloads_Path_Pointer = 0;
// csidl 
int csidl = 0;

void convert_CSIDL_FLAGS(DWORD convert_dwflags)
{
	if (convert_dwflags & KF_FLAG_CREATE)
		csidl |= CSIDL_FLAG_CREATE;

	if (convert_dwflags & KF_FLAG_CREATE)
		csidl |= CSIDL_FLAG_CREATE;

	if (convert_dwflags & KF_FLAG_DONT_VERIFY)
		csidl |= CSIDL_FLAG_DONT_VERIFY;

	if (convert_dwflags & KF_FLAG_NO_ALIAS)
		csidl |= CSIDL_FLAG_NO_ALIAS;

	if (convert_dwflags & KF_FLAG_INIT)
		csidl |= CSIDL_FLAG_PER_USER_INIT;
}


//#pragma comment(linker, "/EXPORT:SHGetKnownFolderPath=_SHGetKnownFolderPathX@16")
//__declspec(noinline)
//extern "C" __declspec(dllexport)
extern "C" __declspec(noinline)
HRESULT
WINAPI
SHGetKnownFolderPathX(
	REFKNOWNFOLDERID rfid,
	DWORD dwflags,
	HANDLE token,
	PWSTR* path
)
{
	wchar_t folder[512 + 1] = { 0 };
	DWORD flags = SHGFP_TYPE_CURRENT;
	DWORD Size = 0;

	csidl = 0; // init always with 0

	if (dwflags & KF_FLAG_DEFAULT_PATH)
		flags = SHGFP_TYPE_DEFAULT;

	if (dwflags & ~(KF_FLAG_CREATE | KF_FLAG_DONT_VERIFY | KF_FLAG_NO_ALIAS | KF_FLAG_INIT))
	{
		return E_INVALIDARG;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Desktop)) // All Users\My Music
	{
		if (FOLDERID_Desktop_Path_Pointer)
		{
			FOLDERID_Desktop_Path_Pointer = nullptr;
		}

		csidl = CSIDL_DESKTOP;  // 0x0000 // <desktop>

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Desktop_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Desktop_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Desktop_Path_Pointer;
		return return_SHGetFolderPathW;

	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_InternetFolder)) // All Users\My Music
	{
		if (FOLDERID_InternetFolder_Path_Pointer)
		{
			FOLDERID_InternetFolder_Path_Pointer = nullptr;
		}

		csidl = CSIDL_INTERNET;// 0x0001 "InternetFolder"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_InternetFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_InternetFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_InternetFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Programs))
	{
		if (FOLDERID_Programs_Path_Pointer)
		{
			FOLDERID_Programs_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PROGRAMS;// 0x0002 // "Programs"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Programs_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Programs_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Programs_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ControlPanelFolder))
	{
		if (FOLDERID_ControlPanelFolder_Path_Pointer)
		{
			FOLDERID_ControlPanelFolder_Path_Pointer = nullptr;
		}

		csidl = CSIDL_CONTROLS; // 0x0003 // "ControlPanelFolder"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ControlPanelFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ControlPanelFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ControlPanelFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_PrintersFolder))
	{
		if (FOLDERID_PrintersFolder_Path_Pointer)
		{
			FOLDERID_PrintersFolder_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PRINTERS;// 0x0004  // "PrintersFolder"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_PrintersFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_PrintersFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_PrintersFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}
	// LLVM 1 times
	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_LocalDocuments))
	{
		if (FOLDERID_LocalDocuments_Path_Pointer)
		{
			FOLDERID_LocalDocuments_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PERSONAL; // 0x0005 // "Personal"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_LocalDocuments_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_LocalDocuments_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_LocalDocuments_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Favorites))
	{
		if (FOLDERID_Favorites_Path_Pointer)
		{
			FOLDERID_Favorites_Path_Pointer = nullptr;
		}

		csidl = CSIDL_FAVORITES; // 0x0006 // "Favorites"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Favorites_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Favorites_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Favorites_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Startup))
	{
		if (FOLDERID_Startup_Path_Pointer)
		{
			FOLDERID_Startup_Path_Pointer = nullptr;
		}

		csidl = CSIDL_STARTUP; // 0x0007 // "Startup"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Startup_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Startup_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Startup_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Recent))
	{
		if (FOLDERID_Recent_Path_Pointer)
		{
			FOLDERID_Recent_Path_Pointer = nullptr;
		}

		csidl = CSIDL_RECENT; // 0x0008 // "Recent"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Recent_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Recent_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Recent_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_SendTo))
	{
		if (FOLDERID_SendTo_Path_Pointer)
		{
			FOLDERID_SendTo_Path_Pointer = nullptr;
		}

		csidl = CSIDL_SENDTO; // 0x0009 // "SendTo

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_SendTo_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_SendTo_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_SendTo_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_RecycleBinFolder))
	{
		if (FOLDERID_RecycleBinFolder_Path_Pointer)
		{
			FOLDERID_RecycleBinFolder_Path_Pointer = nullptr;
		}

		csidl = CSIDL_BITBUCKET; // 0x000a  // "RecycleBinFolder"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_RecycleBinFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_RecycleBinFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_RecycleBinFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_StartMenu))
	{
		if (FOLDERID_StartMenu_Path_Pointer)
		{
			FOLDERID_StartMenu_Path_Pointer = nullptr;
		}

		csidl = CSIDL_STARTMENU; // 0x000a  // "Start Menu"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_StartMenu_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_StartMenu_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_StartMenu_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Documents))
	{
		if (FOLDERID_Documents_Path_Pointer)
		{
			FOLDERID_Documents_Path_Pointer = nullptr;
		}

		csidl = CSIDL_MYDOCUMENTS; // 0x0005 same as "Personal"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Documents_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Documents_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Documents_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Music))
	{
		if (FOLDERID_Music_Path_Pointer)
		{
			FOLDERID_Music_Path_Pointer = nullptr;
		}

		csidl = CSIDL_MYMUSIC; // 0x000d // "My Music" 

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Music_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Music_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Music_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Videos))
	{
		if (FOLDERID_Videos_Path_Pointer)
		{
			FOLDERID_Videos_Path_Pointer = nullptr;
		}

		csidl = CSIDL_MYVIDEO; // 0x000e // "My Video" 

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Videos_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Videos_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Videos_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_PublicDesktop))
	{
		if (FOLDERID_PublicDesktop_Path_Pointer)
		{
			FOLDERID_PublicDesktop_Path_Pointer = nullptr;
		}

		csidl = CSIDL_DESKTOPDIRECTORY; // 0x0010 // // <user name>\Desktop

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_PublicDesktop_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_PublicDesktop_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_PublicDesktop_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ComputerFolder))
	{
		if (FOLDERID_ComputerFolder_Path_Pointer)
		{
			FOLDERID_ComputerFolder_Path_Pointer = nullptr;
		}

		csidl = CSIDL_DRIVES; // 0x0011 // "DriveFolder"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ComputerFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ComputerFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ComputerFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_NetworkFolder))
	{
		if (FOLDERID_NetworkFolder_Path_Pointer)
		{
			FOLDERID_NetworkFolder_Path_Pointer = nullptr;
		}

		csidl = CSIDL_NETWORK; // 0x0012 // "NetworkFolder"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_NetworkFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_NetworkFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_NetworkFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_NetHood))
	{
		if (FOLDERID_NetHood_Path_Pointer)
		{
			FOLDERID_NetHood_Path_Pointer = nullptr;
		}

		csidl = CSIDL_NETHOOD; // 0x0013 // "NetHood"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_NetHood_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_NetHood_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_NetHood_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Fonts))
	{
		if (FOLDERID_Fonts_Path_Pointer)
		{
			FOLDERID_Fonts_Path_Pointer = nullptr;
		}

		csidl = CSIDL_FONTS; // 0x0014 // "Fonts"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Fonts_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Fonts_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Fonts_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Templates))
	{
		if (FOLDERID_Templates_Path_Pointer)
		{
			FOLDERID_Templates_Path_Pointer = nullptr;
		}

		csidl = CSIDL_TEMPLATES; // 0x0015 // "Templates"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Templates_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Templates_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Templates_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CommonStartMenu))
	{
		if (FOLDERID_CommonStartMenu_Path_Pointer)
		{
			FOLDERID_CommonStartMenu_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_STARTMENU; // 0x0016 // "Common Start Menu"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CommonStartMenu_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CommonStartMenu_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CommonStartMenu_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CommonPrograms))
	{
		if (FOLDERID_CommonPrograms_Path_Pointer)
		{
			FOLDERID_CommonPrograms_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_PROGRAMS; // 0x0017 // "Common Programs"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CommonPrograms_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CommonPrograms_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CommonPrograms_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CommonStartup))
	{
		if (FOLDERID_CommonStartup_Path_Pointer)
		{
			FOLDERID_CommonStartup_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_STARTUP; // 0x0018 // "Common Startup"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CommonStartup_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CommonStartup_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CommonStartup_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_AppDataDesktop))
	{
		if (FOLDERID_AppDataDesktop_Path_Pointer)
		{
			FOLDERID_AppDataDesktop_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_DESKTOPDIRECTORY; // 0x0019 // "Common Desktop"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_AppDataDesktop_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_AppDataDesktop_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_AppDataDesktop_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_RoamingAppData))
	{
		if (FOLDERID_RoamingAppData_Path_Pointer)
		{
			FOLDERID_RoamingAppData_Path_Pointer = nullptr;
		}

		csidl = CSIDL_APPDATA; // 0x001a // "AppData"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_RoamingAppData_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_RoamingAppData_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_RoamingAppData_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_PrintHood))
	{
		if (FOLDERID_PrintHood_Path_Pointer)
		{
			FOLDERID_PrintHood_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PRINTHOOD; // 0x001b // "PrintHood"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_PrintHood_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_PrintHood_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_PrintHood_Path_Pointer;
		return return_SHGetFolderPathW;
	}
	// LLVM 2 times
	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_LocalAppData))
	{
		if (FOLDERID_LocalAppData_Path_Pointer)
		{
			FOLDERID_LocalAppData_Path_Pointer = nullptr;
		}

		csidl = CSIDL_LOCAL_APPDATA; // 0x001c // "Local AppData"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_LocalAppData_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_LocalAppData_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_LocalAppData_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_StartMenuAllPrograms))
	{
		if (FOLDERID_StartMenuAllPrograms_Path_Pointer)
		{
			FOLDERID_StartMenuAllPrograms_Path_Pointer = nullptr;
		}

		csidl = CSIDL_ALTSTARTUP; // 0x001d // "AltStartup" // unknown

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_StartMenuAllPrograms_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_StartMenuAllPrograms_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_StartMenuAllPrograms_Path_Pointer;
		return return_SHGetFolderPathW;
	}


	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CommonStartMenuPlaces))
	{
		if (FOLDERID_CommonStartMenuPlaces_Pointer)
		{
			FOLDERID_CommonStartMenuPlaces_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_ALTSTARTUP; // 0x001e // "Common AltStartup" // unknown // comman ) all users
		//The file system directory that corresponds to the nonlocalized Startup program group for all users.

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CommonStartMenuPlaces_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CommonStartMenuPlaces_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CommonStartMenuPlaces_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_AppDataFavorites))
	{
		if (FOLDERID_AppDataFavorites_Path_Pointer)
		{
			FOLDERID_AppDataFavorites_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_FAVORITES; // 0x001f // "Common Favorites"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_AppDataFavorites_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_AppDataFavorites_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_AppDataFavorites_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_InternetCache))
	{
		if (FOLDERID_InternetCache_Path_Pointer)
		{
			FOLDERID_InternetCache_Path_Pointer = nullptr;
		}

		csidl = CSIDL_INTERNET_CACHE; // 0x0020 // "Cache"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_InternetCache_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_InternetCache_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_InternetCache_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Cookies))
	{
		if (FOLDERID_Cookies_Path_Pointer)
		{
			FOLDERID_Cookies_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COOKIES; // 0x0021 // "Cookies"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Cookies_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Cookies_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Cookies_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_History))
	{
		if (FOLDERID_History_Path_Pointer)
		{
			FOLDERID_History_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COOKIES; // 0x0022 // "History"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_History_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_History_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_History_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_LocalAppDataLow))
	{
		if (FOLDERID_LocalAppDataLow_Path_Pointer)
		{
			FOLDERID_LocalAppDataLow_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_APPDATA; // 0x0023 // "Common AppData"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_LocalAppDataLow_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_LocalAppDataLow_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_LocalAppDataLow_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Windows))
	{
		if (FOLDERID_Windows_Path_Pointer)
		{
			FOLDERID_Windows_Path_Pointer = nullptr;
		}

		csidl = CSIDL_WINDOWS; // 0x0024 // "Windows"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Windows_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Windows_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Windows_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_System))
	{
		if (FOLDERID_System_Path_Pointer)
		{
			FOLDERID_System_Path_Pointer = nullptr;
		}

		csidl = CSIDL_SYSTEM; // 0x0025 // "System"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_System_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_System_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_System_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ProgramFiles))
	{
		if (FOLDERID_ProgramFiles_Path_Pointer)
		{
			FOLDERID_ProgramFiles_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PROGRAM_FILES; // 0x0026 // "ProgramFiles"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ProgramFiles_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ProgramFiles_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ProgramFiles_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Pictures))
	{
		if (FOLDERID_Pictures_Path_Pointer)
		{
			FOLDERID_Pictures_Path_Pointer = nullptr;
		}

		csidl = CSIDL_MYPICTURES; // 0x0027 // "My Pictures"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Pictures_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Pictures_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Pictures_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Profile))
	{
		if (FOLDERID_Profile_Path_Pointer)
		{
			FOLDERID_Profile_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PROFILE; // 0x0028 // "Profile"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Profile_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Profile_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Profile_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_SystemX86))
	{
		if (FOLDERID_SystemX86_Path_Pointer)
		{
			FOLDERID_SystemX86_Path_Pointer = nullptr;
		}

		csidl = CSIDL_SYSTEMX86; // 0x0029 // "SystemX86"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_SystemX86_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_SystemX86_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_SystemX86_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ProgramFilesX86))
	{
		if (FOLDERID_ProgramFilesX86_Path_Pointer)
		{
			FOLDERID_ProgramFilesX86_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PROGRAM_FILESX86; // 0x002a // "ProgramFilesX86"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ProgramFilesX86_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ProgramFilesX86_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ProgramFilesX86_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ProgramFilesCommon))
	{
		if (FOLDERID_ProgramFilesCommon_Path_Pointer)
		{
			FOLDERID_ProgramFilesCommon_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PROGRAM_FILES_COMMON; // 0x002b // "CommonProgramFiles"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ProgramFilesCommon_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ProgramFilesCommon_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ProgramFilesCommon_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ProgramFilesCommonX86))
	{
		if (FOLDERID_ProgramFilesCommonX86_Path_Pointer)
		{
			FOLDERID_ProgramFilesCommonX86_Path_Pointer = nullptr;
		}

		csidl = CSIDL_PROGRAM_FILES_COMMONX86; // 0x002c // "CommonProgramFilesX86"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ProgramFilesCommonX86_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ProgramFilesCommonX86_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ProgramFilesCommonX86_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CommonTemplates))
	{
		if (FOLDERID_CommonTemplates_Path_Pointer)
		{
			FOLDERID_CommonTemplates_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_TEMPLATES; // 0x002d // "Common Templates"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CommonTemplates_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CommonTemplates_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CommonTemplates_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_PublicDocuments))
	{
		if (FOLDERID_PublicDocuments_Path_Pointer)
		{
			FOLDERID_PublicDocuments_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_DOCUMENTS; // 0x002e // "Common Documents"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_PublicDocuments_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_PublicDocuments_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_PublicDocuments_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CommonAdminTools))
	{
		if (FOLDERID_CommonAdminTools_Path_Pointer)
		{
			FOLDERID_CommonAdminTools_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_ADMINTOOLS; // 0x002f // "Common Administrative Tools"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CommonAdminTools_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CommonAdminTools_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CommonAdminTools_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_AdminTools))
	{
		if (FOLDERID_AdminTools_Path_Pointer)
		{
			FOLDERID_AdminTools_Path_Pointer = nullptr;
		}

		csidl = CSIDL_ADMINTOOLS; // 0x0030 // "Administrative Tools"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_AdminTools_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_AdminTools_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_AdminTools_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ConnectionsFolder))
	{
		if (FOLDERID_ConnectionsFolder_Path_Pointer)
		{
			FOLDERID_ConnectionsFolder_Path_Pointer = nullptr;
		}

		csidl = CSIDL_CONNECTIONS; // 0x0031 // "ConnectionsFolder"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ConnectionsFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ConnectionsFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ConnectionsFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_PublicMusic))
	{
		if (FOLDERID_PublicMusic_Path_Pointer)
		{
			FOLDERID_PublicMusic_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_MUSIC; // 0x0035 //  "CommonMusic"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_PublicMusic_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_PublicMusic_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_PublicMusic_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_PublicPictures))
	{
		if (FOLDERID_PublicPictures_Path_Pointer)
		{
			FOLDERID_PublicPictures_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_PICTURES; // 0x0036 //  "CommonPictures"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_PublicPictures_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_PublicPictures_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_PublicPictures_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_PublicVideos))
	{
		if (FOLDERID_PublicVideos_Path_Pointer)
		{
			FOLDERID_PublicVideos_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_VIDEO; // 0x0037 //  "CommonVideo"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_PublicVideos_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_PublicVideos_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_PublicVideos_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_ResourceDir))
	{
		FOLDERID_ResourceDir_Path_Pointer = 0;
		if (FOLDERID_ResourceDir_Path_Pointer)
		{
			FOLDERID_ResourceDir_Path_Pointer = nullptr;
		}

		csidl = CSIDL_RESOURCES; // 0x0038 //  "ResourceDir"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_ResourceDir_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_ResourceDir_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_ResourceDir_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_LocalizedResourcesDir))
	{
		if (FOLDERID_LocalizedResourcesDir_Path_Pointer)
		{
			FOLDERID_LocalizedResourcesDir_Path_Pointer = nullptr;
		}

		csidl = CSIDL_RESOURCES_LOCALIZED; // 0x0039 // "LocalizedResourcesDir"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_LocalizedResourcesDir_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_LocalizedResourcesDir_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_LocalizedResourcesDir_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CommonOEMLinks))
	{
		if (FOLDERID_CommonOEMLinks_Path_Pointer)
		{
			FOLDERID_CommonOEMLinks_Path_Pointer = nullptr;
		}

		csidl = CSIDL_COMMON_OEM_LINKS; // 0x003a // "OEM Links"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CommonOEMLinks_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CommonOEMLinks_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CommonOEMLinks_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_CDBurning))
	{
		if (FOLDERID_CDBurning_Path_Pointer)
		{
			FOLDERID_CDBurning_Path_Pointer = nullptr;
		}

		csidl = CSIDL_CDBURN_AREA; // 0x003b // "CD Burning"

		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_CDBurning_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_CDBurning_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_CDBurning_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_NetworkFolder))
	{
		if (FOLDERID_NetworkFolder_Path_Pointer)
		{
			FOLDERID_NetworkFolder_Path_Pointer = nullptr;
		}
		csidl = CSIDL_COMPUTERSNEARME; // 0x003b // "ComputersNearMe"
		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		// function call with flags
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);
		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_NetworkFolder_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_NetworkFolder_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_NetworkFolder_Path_Pointer;
		return return_SHGetFolderPathW;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Public))
	{
		if (FOLDERID_Public_Path_Pointer)
		{
			FOLDERID_Public_Path_Pointer = nullptr;
		}
		Size = sizeof(folder);
		GetAllUsersProfileDirectoryW(folder, &Size);
		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Public_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Public_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Public_Path_Pointer;
		return S_OK;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_QuickLaunch))
	{
		if (FOLDERID_QuickLaunch_Path_Pointer)
		{
			FOLDERID_QuickLaunch_Path_Pointer = nullptr;
		}
		ExpandEnvironmentStringsW(L"%APPDATA%", folder, MAX_PATH);
		lstrcatW(folder, L"\\Microsoft\\Internet Explorer\\Quick");
		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_QuickLaunch_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_QuickLaunch_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_QuickLaunch_Path_Pointer;
		return S_OK;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_UserProfiles))
	{
		if (FOLDERID_UserProfiles_Path_Pointer)
		{
			FOLDERID_UserProfiles_Path_Pointer = nullptr;
		}
		Size = sizeof(folder);
		GetProfilesDirectoryW(folder, (LPDWORD)&Size);
		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_UserProfiles_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_UserProfiles_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_UserProfiles_Path_Pointer;
		return S_OK;
	}

	if (IsEqualGUID((REFGUID)rfid, (REFGUID)FOLDERID_Downloads))
	{
		if (FOLDERID_Downloads_Path_Pointer)
		{
			FOLDERID_Downloads_Path_Pointer = nullptr;
		}
		csidl = CSIDL_PROFILE;
		// change global csidl with flags
		convert_CSIDL_FLAGS(dwflags);
		int return_SHGetFolderPathW = SHGetFolderPathW(NULL, csidl, token, flags, folder);
		lstrcatW(folder, L"\\Downloads");

		unsigned int SHGetKnownFolderPath_Len = lstrlenW(folder);

		FOLDERID_Downloads_Path_Pointer = (WCHAR*)CoTaskMemAlloc((SHGetKnownFolderPath_Len + 1) * sizeof(WCHAR));
		memcpy(FOLDERID_Downloads_Path_Pointer, (void*)folder, ((SHGetKnownFolderPath_Len * 2) + 2));
		*path = (PWSTR)FOLDERID_Downloads_Path_Pointer;
		return return_SHGetFolderPathW;
	}
	//return S_FALSE;
	return S_OK;
}
// SHGetKnownFolderPath end
/* static */
bool ModuleEvaluator::ResolveKnownFolder(REFKNOWNFOLDERID aFolderId,
                                         nsIFile** aOutFile) {
  if (!aOutFile) {
    return false;
  }

  *aOutFile = nullptr;

  // Since we're running off main thread, we can't use NS_GetSpecialDirectory
  PWSTR rawPath = nullptr;
  HRESULT hr =
      SHGetKnownFolderPathX(aFolderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
  if (FAILED(hr)) {
    return false;
  }

  using ShellStringUniquePtr =
      UniquePtr<std::remove_pointer_t<PWSTR>, CoTaskMemFreeDeleter>;

  ShellStringUniquePtr path(rawPath);

  nsresult rv = NS_NewLocalFile(nsDependentString(path.get()), aOutFile);
  return NS_SUCCEEDED(rv);
}

ModuleEvaluator::ModuleEvaluator()
    : mKeyboardLayoutDlls(GetKeyboardLayoutDlls()) {
  MOZ_ASSERT(XRE_IsParentProcess());

#if defined(_M_IX86)
  // We want to resolve to SYSWOW64 when applicable
  REFKNOWNFOLDERID systemFolderId = FOLDERID_SystemX86;
#else
  REFKNOWNFOLDERID systemFolderId = FOLDERID_System;
#endif  // defined(_M_IX86)

  bool resolveOk =
      ResolveKnownFolder(systemFolderId, getter_AddRefs(mSysDirectory));
  MOZ_ASSERT(resolveOk);
  if (!resolveOk) {
    return;
  }

  nsCOMPtr<nsIFile> winSxSDir;
  resolveOk = ResolveKnownFolder(FOLDERID_Windows, getter_AddRefs(winSxSDir));
  MOZ_ASSERT(resolveOk);
  if (!resolveOk) {
    return;
  }

  nsresult rv = winSxSDir->Append(u"WinSxS"_ns);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  mWinSxSDirectory = std::move(winSxSDir);

  nsCOMPtr<nsIFile> exeFile;
  rv = XRE_GetBinaryPath(getter_AddRefs(exeFile));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  rv = exeFile->GetParent(getter_AddRefs(mExeDirectory));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  nsAutoString exePath;
  rv = exeFile->GetPath(exePath);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  ModuleVersionInfo exeVi;
  if (!exeVi.GetFromImage(exePath)) {
    return;
  }

  mExeVersion = Some(ModuleVersion(exeVi.mFileVersion.Version64()));
}

ModuleEvaluator::operator bool() const {
  return mExeVersion.isSome() && mExeDirectory && mSysDirectory &&
         mWinSxSDirectory;
}

Maybe<ModuleTrustFlags> ModuleEvaluator::GetTrust(
    const ModuleRecord& aModuleRecord) const {
  MOZ_ASSERT(XRE_IsParentProcess());

  // We start by checking authenticode signatures, as the presence of any
  // signature will produce an immediate pass/fail.
  if (aModuleRecord.mVendorInfo.isSome() &&
      aModuleRecord.mVendorInfo.ref().mHasNestedMicrosoftSignature) {
    return Some(ModuleTrustFlags::MicrosoftWindowsSignature);
  }
  if (aModuleRecord.mVendorInfo.isSome() &&
      aModuleRecord.mVendorInfo.ref().mSource ==
          VendorInfo::Source::Signature) {
    const nsString& signedBy = aModuleRecord.mVendorInfo.ref().mVendor;

    if (signedBy.EqualsLiteral("Microsoft Windows")) {
      return Some(ModuleTrustFlags::MicrosoftWindowsSignature);
    } else if (signedBy.EqualsLiteral("Microsoft Corporation")) {
      return Some(ModuleTrustFlags::MicrosoftWindowsSignature);
    } else if (signedBy.EqualsLiteral("Mozilla Corporation")) {
      return Some(ModuleTrustFlags::MozillaSignature);
    } else {
      // Being signed by somebody who is neither Microsoft nor us is an
      // automatic and immediate disqualification.
      return Some(ModuleTrustFlags::None);
    }
  }

  const nsCOMPtr<nsIFile>& dllFile = aModuleRecord.mResolvedDosName;
  MOZ_ASSERT(!!dllFile);
  if (!dllFile) {
    return Nothing();
  }

  nsAutoString dllLeafLower;
  if (NS_FAILED(dllFile->GetLeafName(dllLeafLower))) {
    return Nothing();
  }

  ToLowerCase(dllLeafLower);  // To facilitate case-insensitive searching

  // The JIT profiling module doesn't really have any other practical way to
  // match; hard-code it as being trusted.
  if (dllLeafLower.EqualsLiteral("jitpi.dll")) {
    if (PR_GetEnvSecure("JS_LOAD_VTUNE_LIB")) {
      return Some(ModuleTrustFlags::JitPI);
    } else {
      return Some(ModuleTrustFlags::None);
    }
  }

  ModuleTrustFlags result = ModuleTrustFlags::None;

  nsresult rv;
  bool contained;

  // Is the DLL in the system directory?
  rv = mSysDirectory->Contains(dllFile, &contained);
  if (NS_SUCCEEDED(rv) && contained) {
    result |= ModuleTrustFlags::SystemDirectory;
  }

  // Is the DLL in the WinSxS directory? Some Microsoft DLLs (e.g. comctl32) are
  // loaded from here and don't have digital signatures. So while this is not a
  // guarantee of trustworthiness, but is at least as valid as system32.
  rv = mWinSxSDirectory->Contains(dllFile, &contained);
  if (NS_SUCCEEDED(rv) && contained) {
    result |= ModuleTrustFlags::WinSxSDirectory;
  }

  // Is it a keyboard layout DLL?
  if (std::find(mKeyboardLayoutDlls.begin(), mKeyboardLayoutDlls.end(),
                dllLeafLower) != mKeyboardLayoutDlls.end()) {
    result |= ModuleTrustFlags::KeyboardLayout;
    // This doesn't guarantee trustworthiness by itself. Keyboard layouts also
    // must be in the system directory.
  }

  if (aModuleRecord.mVendorInfo.isSome() &&
      aModuleRecord.mVendorInfo.ref().mSource ==
          VendorInfo::Source::VersionInfo) {
    const nsString& companyName = aModuleRecord.mVendorInfo.ref().mVendor;

    if (companyName.EqualsLiteral("Microsoft Corporation")) {
      result |= ModuleTrustFlags::MicrosoftVersion;
    }
  }

  rv = mExeDirectory->Contains(dllFile, &contained);
  if (NS_SUCCEEDED(rv) && contained) {
    result |= ModuleTrustFlags::FirefoxDirectory;

    // If the DLL is in the Firefox directory, does it also share the Firefox
    // version info?
    if (mExeVersion.isSome() && aModuleRecord.mVersion.isSome() &&
        mExeVersion.value() == aModuleRecord.mVersion.value()) {
      result |= ModuleTrustFlags::FirefoxDirectoryAndVersion;
    }
  }

  return Some(result);
}

}  // namespace mozilla

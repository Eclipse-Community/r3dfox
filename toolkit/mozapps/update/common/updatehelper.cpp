/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

// Needed for CreateToolhelp32Snapshot
#include <tlhelp32.h>

#include <stdio.h>
#include <direct.h>
#include "shlobj.h"

// Needed for PathAppendW
#include <shlwapi.h>

#include "updatehelper.h"
#include "updateutils_win.h"

#ifdef MOZ_MAINTENANCE_SERVICE
#  include "mozilla/UniquePtr.h"
#  include "pathhash.h"
#  include "registrycertificates.h"
#  include "uachelper.h"

using mozilla::MakeUnique;
using mozilla::UniquePtr;
#endif

BOOL PathGetSiblingFilePath(LPWSTR destinationBuffer, LPCWSTR siblingFilePath,
                            LPCWSTR newFileName);

/**
 * Obtains the path of a file in the same directory as the specified file.
 *
 * @param  destinationBuffer A buffer of size MAX_PATH + 1 to store the result.
 * @param  siblingFilePath   The path of another file in the same directory
 * @param  newFileName       The filename of another file in the same directory
 * @return TRUE if successful
 */
BOOL PathGetSiblingFilePath(LPWSTR destinationBuffer, LPCWSTR siblingFilePath,
                            LPCWSTR newFileName) {
  if (wcslen(siblingFilePath) > MAX_PATH) {
    return FALSE;
  }

  wcsncpy(destinationBuffer, siblingFilePath, MAX_PATH + 1);
  if (!PathRemoveFileSpecW(destinationBuffer)) {
    return FALSE;
  }

  return PathAppendSafe(destinationBuffer, newFileName);
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
/**
 * Obtains the path of the secure directory used to write the status and log
 * files for updates applied with an elevated updater or an updater that is
 * launched using the maintenance service.
 *
 * Example
 * Destination buffer value:
 *   C:\Program Files (x86)\Mozilla Maintenance Service\UpdateLogs
 *
 * @param  outBuf
 *         A buffer of size MAX_PATH + 1 to store the result.
 * @return TRUE if successful
 */
BOOL GetSecureOutputDirectoryPath(LPWSTR outBuf) {
  PWSTR progFilesX86;
  if (FAILED(SHGetKnownFolderPathX(FOLDERID_ProgramFilesX86, KF_FLAG_CREATE,
                                  nullptr, &progFilesX86))) {
    return FALSE;
  }
  if (wcslen(progFilesX86) > MAX_PATH) {
    CoTaskMemFree(progFilesX86);
    return FALSE;
  }
  wcsncpy(outBuf, progFilesX86, MAX_PATH + 1);
  CoTaskMemFree(progFilesX86);

  if (!PathAppendSafe(outBuf, L"Mozilla Maintenance Service")) {
    return FALSE;
  }

  // Create the Maintenance Service directory in case it doesn't exist.
  if (!CreateDirectoryW(outBuf, nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return FALSE;
  }

  if (!PathAppendSafe(outBuf, L"UpdateLogs")) {
    return FALSE;
  }

  // Create the secure update output directory in case it doesn't exist.
  if (!CreateDirectoryW(outBuf, nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    return FALSE;
  }

  return TRUE;
}

/**
 * Obtains the name of the update output file using the update patch directory
 * path and file extension (must include the '.' separator) passed to this
 * function.
 *
 * Example
 * Patch directory path parameter:
 *   C:\ProgramData\Mozilla\updates\0123456789ABCDEF\updates\0
 * File extension parameter:
 *   .status
 * Destination buffer value:
 *   0123456789ABCDEF.status
 *
 * @param  patchDirPath
 *         The path to the update patch directory.
 * @param  fileExt
 *         The file extension for the file including the '.' separator.
 * @param  outBuf
 *         A buffer of size MAX_PATH + 1 to store the result.
 * @return TRUE if successful
 */
BOOL GetSecureOutputFileName(LPCWSTR patchDirPath, LPCWSTR fileExt,
                             LPWSTR outBuf) {
  size_t fullPathLen = wcslen(patchDirPath);
  if (fullPathLen > MAX_PATH) {
    return FALSE;
  }

  size_t relPathLen = wcslen(PATCH_DIR_PATH);
  if (relPathLen > fullPathLen) {
    return FALSE;
  }

  // The patch directory path must end with updates\0 for updates applied with
  // an elevated updater or an updater that is launched using the maintenance
  // service.
  if (_wcsnicmp(patchDirPath + fullPathLen - relPathLen, PATCH_DIR_PATH,
                relPathLen) != 0) {
    return FALSE;
  }

  wcsncpy(outBuf, patchDirPath, MAX_PATH + 1);
  if (!PathRemoveFileSpecW(outBuf)) {
    return FALSE;
  }

  if (!PathRemoveFileSpecW(outBuf)) {
    return FALSE;
  }

  PathStripPathW(outBuf);

  size_t outBufLen = wcslen(outBuf);
  size_t fileExtLen = wcslen(fileExt);
  if (outBufLen + fileExtLen > MAX_PATH) {
    return FALSE;
  }

  wcsncat(outBuf, fileExt, fileExtLen);

  return TRUE;
}

/**
 * Obtains the full path of the secure update output file using the update patch
 * directory path and file extension (must include the '.' separator) passed to
 * this function.
 *
 * Example
 * Patch directory path parameter:
 *   C:\ProgramData\Mozilla\updates\0123456789ABCDEF\updates\0
 * File extension parameter:
 *   .status
 * Destination buffer value:
 *   C:\Program Files (x86)\Mozilla Maintenance
 *     Service\UpdateLogs\0123456789ABCDEF.status
 *
 * @param  patchDirPath
 *         The path to the update patch directory.
 * @param  fileExt
 *         The file extension for the file including the '.' separator.
 * @param  outBuf
 *         A buffer of size MAX_PATH + 1 to store the result.
 * @return TRUE if successful
 */
BOOL GetSecureOutputFilePath(LPCWSTR patchDirPath, LPCWSTR fileExt,
                             LPWSTR outBuf) {
  if (!GetSecureOutputDirectoryPath(outBuf)) {
    return FALSE;
  }

  WCHAR statusFileName[MAX_PATH + 1] = {L'\0'};
  if (!GetSecureOutputFileName(patchDirPath, fileExt, statusFileName)) {
    return FALSE;
  }

  return PathAppendSafe(outBuf, statusFileName);
}

/**
 * Writes a UUID to the ID file in the secure output directory. This is used by
 * the unelevated updater to determine whether an existing update status file in
 * the secure output directory has been updated.
 *
 * @param  patchDirPath
 *         The path to the update patch directory.
 * @return TRUE if successful
 */
BOOL WriteSecureIDFile(LPCWSTR patchDirPath) {
  WCHAR uuidString[MAX_PATH + 1] = {L'\0'};
  if (!GetUUIDString(uuidString)) {
    return FALSE;
  }

  WCHAR idFilePath[MAX_PATH + 1] = {L'\0'};
  if (!GetSecureOutputFilePath(patchDirPath, L".id", idFilePath)) {
    return FALSE;
  }

  FILE* idFile = _wfopen(idFilePath, L"wb+");
  if (idFile == nullptr) {
    return FALSE;
  }

  if (fprintf(idFile, "%ls\n", uuidString) == -1) {
    fclose(idFile);
    return FALSE;
  }

  fclose(idFile);

  return TRUE;
}

/**
 * Removes the update status and log files from the secure output directory.
 *
 * @param  patchDirPath
 *         The path to the update patch directory.
 */
void RemoveSecureOutputFiles(LPCWSTR patchDirPath) {
  WCHAR filePath[MAX_PATH + 1] = {L'\0'};
  if (GetSecureOutputFilePath(patchDirPath, L".id", filePath)) {
    (void)_wremove(filePath);
  }
  if (GetSecureOutputFilePath(patchDirPath, L".status", filePath)) {
    (void)_wremove(filePath);
  }
  if (GetSecureOutputFilePath(patchDirPath, L".log", filePath)) {
    (void)_wremove(filePath);
  }
}

#ifdef MOZ_MAINTENANCE_SERVICE
/**
 * Starts the upgrade process for update of the service if it is
 * already installed.
 *
 * @param  installDir the installation directory where
 *         maintenanceservice_installer.exe is located.
 * @return TRUE if successful
 */
BOOL StartServiceUpdate(LPCWSTR installDir) {
  // Get a handle to the local computer SCM database
  SC_HANDLE manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!manager) {
    return FALSE;
  }

  // Open the service
  SC_HANDLE svc = OpenServiceW(manager, SVC_NAME, SERVICE_ALL_ACCESS);
  if (!svc) {
    CloseServiceHandle(manager);
    return FALSE;
  }

  // If we reach here, then the service is installed, so
  // proceed with upgrading it.

  CloseServiceHandle(manager);

  // The service exists and we opened it, get the config bytes needed
  DWORD bytesNeeded;
  if (!QueryServiceConfigW(svc, nullptr, 0, &bytesNeeded) &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    CloseServiceHandle(svc);
    return FALSE;
  }

  // Get the service config information, in particular we want the binary
  // path of the service.
  UniquePtr<char[]> serviceConfigBuffer = MakeUnique<char[]>(bytesNeeded);
  if (!QueryServiceConfigW(
          svc,
          reinterpret_cast<QUERY_SERVICE_CONFIGW*>(serviceConfigBuffer.get()),
          bytesNeeded, &bytesNeeded)) {
    CloseServiceHandle(svc);
    return FALSE;
  }

  CloseServiceHandle(svc);

  QUERY_SERVICE_CONFIGW& serviceConfig =
      *reinterpret_cast<QUERY_SERVICE_CONFIGW*>(serviceConfigBuffer.get());

  PathUnquoteSpacesW(serviceConfig.lpBinaryPathName);

  // Obtain the temp path of the maintenance service binary
  WCHAR tmpService[MAX_PATH + 1] = {L'\0'};
  if (!PathGetSiblingFilePath(tmpService, serviceConfig.lpBinaryPathName,
                              L"maintenanceservice_tmp.exe")) {
    return FALSE;
  }

  if (wcslen(installDir) > MAX_PATH) {
    return FALSE;
  }

  // Get the new maintenance service path from the install dir
  WCHAR newMaintServicePath[MAX_PATH + 1] = {L'\0'};
  wcsncpy(newMaintServicePath, installDir, MAX_PATH);
  PathAppendSafe(newMaintServicePath, L"maintenanceservice.exe");

  // Copy the temp file in alongside the maintenace service.
  // This is a requirement for maintenance service upgrades.
  if (!CopyFileW(newMaintServicePath, tmpService, FALSE)) {
    return FALSE;
  }

  // Check that the copied file's certificate matches the expected name and
  // issuer stored in the registry for this installation and that the
  // certificate is trusted by the system's certificate store.
  if (!DoesBinaryMatchAllowedCertificates(installDir, tmpService)) {
    DeleteFileW(tmpService);
    return FALSE;
  }

  // Start the upgrade comparison process
  STARTUPINFOW si = {0};
  si.cb = sizeof(STARTUPINFOW);
  // No particular desktop because no UI
  si.lpDesktop = const_cast<LPWSTR>(L"");  // -Wwritable-strings
  PROCESS_INFORMATION pi = {0};
  WCHAR cmdLine[64] = {'\0'};
  wcsncpy(cmdLine, L"dummyparam.exe upgrade",
          sizeof(cmdLine) / sizeof(cmdLine[0]) - 1);
  BOOL svcUpdateProcessStarted =
      CreateProcessW(tmpService, cmdLine, nullptr, nullptr, FALSE, 0, nullptr,
                     installDir, &si, &pi);
  if (svcUpdateProcessStarted) {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  return svcUpdateProcessStarted;
}

/**
 * Executes a maintenance service command
 *
 * @param  argc    The total number of arguments in argv
 * @param  argv    An array of null terminated strings to pass to the service,
 * @return ERROR_SUCCESS if the service command was started.
 *         Less than 16000, a windows system error code from StartServiceW
 *         More than 20000, 20000 + the last state of the service constant if
 *         the last state is something other than stopped.
 *         17001 if the SCM could not be opened
 *         17002 if the service could not be opened
 */
DWORD
StartServiceCommand(int argc, LPCWSTR* argv) {
  DWORD lastState = WaitForServiceStop(SVC_NAME, 5);
  if (lastState != SERVICE_STOPPED) {
    return 20000 + lastState;
  }

  // Get a handle to the SCM database.
  SC_HANDLE serviceManager = OpenSCManager(
      nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
  if (!serviceManager) {
    return 17001;
  }

  // Get a handle to the service.
  SC_HANDLE service = OpenServiceW(serviceManager, SVC_NAME, SERVICE_START);
  if (!service) {
    CloseServiceHandle(serviceManager);
    return 17002;
  }

  // Wait at most 5 seconds trying to start the service in case of errors
  // like ERROR_SERVICE_DATABASE_LOCKED or ERROR_SERVICE_REQUEST_TIMEOUT.
  const DWORD maxWaitMS = 5000;
  DWORD currentWaitMS = 0;
  DWORD lastError = ERROR_SUCCESS;
  while (currentWaitMS < maxWaitMS) {
    BOOL result = StartServiceW(service, argc, argv);
    if (result) {
      lastError = ERROR_SUCCESS;
      break;
    } else {
      lastError = GetLastError();
    }
    Sleep(100);
    currentWaitMS += 100;
  }
  CloseServiceHandle(service);
  CloseServiceHandle(serviceManager);
  return lastError;
}

/**
 * Launch a service initiated action for a software update with the
 * specified arguments.
 *
 * @param  argc    The total number of arguments in argv
 * @param  argv    An array of null terminated strings to pass to the exePath,
 *                 argv[0] must be the path to the updater.exe
 * @return ERROR_SUCCESS if successful
 */
DWORD
LaunchServiceSoftwareUpdateCommand(int argc, LPCWSTR* argv) {
  // The service command is the same as the updater.exe command line except
  // it has 4 extra args:
  // 0) The name of the service, automatically added by Windows
  // 1) "MozillaMaintenance" (I think this is redundant with 0)
  // 2) The command being executed, which is "software-update"
  // 3) The path to updater.exe (from argv[0])
  LPCWSTR* updaterServiceArgv = new LPCWSTR[argc + 2];
  updaterServiceArgv[0] = L"MozillaMaintenance";
  updaterServiceArgv[1] = L"software-update";

  for (int i = 0; i < argc; ++i) {
    updaterServiceArgv[i + 2] = argv[i];
  }

  // Execute the service command by starting the service with
  // the passed in arguments.
  DWORD ret = StartServiceCommand(argc + 2, updaterServiceArgv);
  delete[] updaterServiceArgv;
  return ret;
}

/**
 * Writes a specific failure code for the update status to a file in the secure
 * output directory. The status file's name without the '.' separator and
 * extension is the same as the update directory name.
 *
 * @param  patchDirPath
 *         The path of the update patch directory.
 * @param  errorCode
 *         Error code to set
 * @return TRUE if successful
 */
BOOL WriteStatusFailure(LPCWSTR patchDirPath, int errorCode) {
  WCHAR statusFilePath[MAX_PATH + 1] = {L'\0'};
  if (!GetSecureOutputFilePath(patchDirPath, L".status", statusFilePath)) {
    return FALSE;
  }

  HANDLE hStatusFile = CreateFileW(statusFilePath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, 0, nullptr);
  if (hStatusFile == INVALID_HANDLE_VALUE) {
    return FALSE;
  }

  char failure[32];
  sprintf(failure, "failed: %d", errorCode);
  DWORD toWrite = strlen(failure);
  DWORD wrote;
  BOOL ok = WriteFile(hStatusFile, failure, toWrite, &wrote, nullptr);
  CloseHandle(hStatusFile);

  if (!ok || wrote != toWrite) {
    return FALSE;
  }

  return TRUE;
}

/**
 * Waits for a service to enter a stopped state.
 * This function does not stop the service, it just blocks until the service
 * is stopped.
 *
 * @param  serviceName     The service to wait for.
 * @param  maxWaitSeconds  The maximum number of seconds to wait
 * @return state of the service after a timeout or when stopped.
 *         A value of 255 is returned for an error. Typical values are:
 *         SERVICE_STOPPED 0x00000001
 *         SERVICE_START_PENDING 0x00000002
 *         SERVICE_STOP_PENDING 0x00000003
 *         SERVICE_RUNNING 0x00000004
 *         SERVICE_CONTINUE_PENDING 0x00000005
 *         SERVICE_PAUSE_PENDING 0x00000006
 *         SERVICE_PAUSED 0x00000007
 *         last status not set 0x000000CF
 *         Could no query status 0x000000DF
 *         Could not open service, access denied 0x000000EB
 *         Could not open service, invalid handle 0x000000EC
 *         Could not open service, invalid name 0x000000ED
 *         Could not open service, does not exist 0x000000EE
 *         Could not open service, other error 0x000000EF
 *         Could not open SCM, access denied 0x000000FD
 *         Could not open SCM, database does not exist 0x000000FE;
 *         Could not open SCM, other error 0x000000FF;
 * Note: The strange choice of error codes above SERVICE_PAUSED are chosen
 * in case Windows comes out with other service stats higher than 7, they
 * would likely call it 8 and above.  JS code that uses this in TestAUSHelper
 * only handles values up to 255 so that's why we don't use GetLastError
 * directly.
 */
DWORD
WaitForServiceStop(LPCWSTR serviceName, DWORD maxWaitSeconds) {
  // 0x000000CF is defined above to be not set
  DWORD lastServiceState = 0x000000CF;

  // Get a handle to the SCM database.
  SC_HANDLE serviceManager = OpenSCManager(
      nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
  if (!serviceManager) {
    DWORD lastError = GetLastError();
    switch (lastError) {
      case ERROR_ACCESS_DENIED:
        return 0x000000FD;
      case ERROR_DATABASE_DOES_NOT_EXIST:
        return 0x000000FE;
      default:
        return 0x000000FF;
    }
  }

  // Get a handle to the service.
  SC_HANDLE service =
      OpenServiceW(serviceManager, serviceName, SERVICE_QUERY_STATUS);
  if (!service) {
    DWORD lastError = GetLastError();
    CloseServiceHandle(serviceManager);
    switch (lastError) {
      case ERROR_ACCESS_DENIED:
        return 0x000000EB;
      case ERROR_INVALID_HANDLE:
        return 0x000000EC;
      case ERROR_INVALID_NAME:
        return 0x000000ED;
      case ERROR_SERVICE_DOES_NOT_EXIST:
        return 0x000000EE;
      default:
        return 0x000000EF;
    }
  }

  DWORD currentWaitMS = 0;
  SERVICE_STATUS_PROCESS ssp;
  ssp.dwCurrentState = lastServiceState;
  while (currentWaitMS < maxWaitSeconds * 1000) {
    DWORD bytesNeeded;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp,
                              sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
      DWORD lastError = GetLastError();
      switch (lastError) {
        case ERROR_INVALID_HANDLE:
          ssp.dwCurrentState = 0x000000D9;
          break;
        case ERROR_ACCESS_DENIED:
          ssp.dwCurrentState = 0x000000DA;
          break;
        case ERROR_INSUFFICIENT_BUFFER:
          ssp.dwCurrentState = 0x000000DB;
          break;
        case ERROR_INVALID_PARAMETER:
          ssp.dwCurrentState = 0x000000DC;
          break;
        case ERROR_INVALID_LEVEL:
          ssp.dwCurrentState = 0x000000DD;
          break;
        case ERROR_SHUTDOWN_IN_PROGRESS:
          ssp.dwCurrentState = 0x000000DE;
          break;
        // These 3 errors can occur when the service is not yet stopped but
        // it is stopping.
        case ERROR_INVALID_SERVICE_CONTROL:
        case ERROR_SERVICE_CANNOT_ACCEPT_CTRL:
        case ERROR_SERVICE_NOT_ACTIVE:
          currentWaitMS += 50;
          Sleep(50);
          continue;
        default:
          ssp.dwCurrentState = 0x000000DF;
      }

      // We couldn't query the status so just break out
      break;
    }

    // The service is already in use.
    if (ssp.dwCurrentState == SERVICE_STOPPED) {
      break;
    }
    currentWaitMS += 50;
    Sleep(50);
  }

  lastServiceState = ssp.dwCurrentState;
  CloseServiceHandle(service);
  CloseServiceHandle(serviceManager);
  return lastServiceState;
}
#endif

/**
 * Determines if there is at least one process running for the specified
 * application. A match will be found across any session for any user.
 *
 * @param process The process to check for existance
 * @return ERROR_NOT_FOUND if the process was not found
 *         ERROR_SUCCESS if the process was found and there were no errors
 *         Other Win32 system error code for other errors
 **/
DWORD
IsProcessRunning(LPCWSTR filename) {
  // Take a snapshot of all processes in the system.
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (INVALID_HANDLE_VALUE == snapshot) {
    return GetLastError();
  }

  PROCESSENTRY32W processEntry;
  processEntry.dwSize = sizeof(PROCESSENTRY32W);
  if (!Process32FirstW(snapshot, &processEntry)) {
    DWORD lastError = GetLastError();
    CloseHandle(snapshot);
    return lastError;
  }

  do {
    if (wcsicmp(filename, processEntry.szExeFile) == 0) {
      CloseHandle(snapshot);
      return ERROR_SUCCESS;
    }
  } while (Process32NextW(snapshot, &processEntry));
  CloseHandle(snapshot);
  return ERROR_NOT_FOUND;
}

/**
 * Waits for the specified application to exit.
 *
 * @param filename   The application to wait for.
 * @param maxSeconds The maximum amount of seconds to wait for all
 *                   instances of the application to exit.
 * @return  ERROR_SUCCESS if no instances of the application exist
 *          WAIT_TIMEOUT if the process is still running after maxSeconds.
 *          Any other Win32 system error code.
 */
DWORD
WaitForProcessExit(LPCWSTR filename, DWORD maxSeconds) {
  DWORD applicationRunningError = WAIT_TIMEOUT;
  for (DWORD i = 0; i < maxSeconds; i++) {
    DWORD applicationRunningError = IsProcessRunning(filename);
    if (ERROR_NOT_FOUND == applicationRunningError) {
      return ERROR_SUCCESS;
    }
    Sleep(1000);
  }

  if (ERROR_SUCCESS == applicationRunningError) {
    return WAIT_TIMEOUT;
  }

  return applicationRunningError;
}

#ifdef MOZ_MAINTENANCE_SERVICE
/**
 *  Determines if the fallback key exists or not
 *
 *  @return TRUE if the fallback key exists and there was no error checking
 */
BOOL DoesFallbackKeyExist() {
  HKEY testOnlyFallbackKey;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TEST_ONLY_FALLBACK_KEY_PATH, 0,
                    KEY_READ | KEY_WOW64_64KEY,
                    &testOnlyFallbackKey) != ERROR_SUCCESS) {
    return FALSE;
  }

  RegCloseKey(testOnlyFallbackKey);
  return TRUE;
}

/**
 * Determines if the file system for the specified file handle is local
 * @param file path to check the filesystem type for, must be at most MAX_PATH
 * @param isLocal out parameter which will hold TRUE if the drive is local
 * @return TRUE if the call succeeded
 */
BOOL IsLocalFile(LPCWSTR file, BOOL& isLocal) {
  WCHAR rootPath[MAX_PATH + 1] = {L'\0'};
  if (wcslen(file) > MAX_PATH) {
    return FALSE;
  }

  wcsncpy(rootPath, file, MAX_PATH);
  PathStripToRootW(rootPath);
  isLocal = GetDriveTypeW(rootPath) == DRIVE_FIXED;
  return TRUE;
}

/**
 * Determines the DWORD value of a registry key value
 *
 * @param key       The base key to where the value name exists
 * @param valueName The name of the value
 * @param retValue  Out parameter which will hold the value
 * @return TRUE on success
 */
static BOOL GetDWORDValue(HKEY key, LPCWSTR valueName, DWORD& retValue) {
  DWORD regDWORDValueSize = sizeof(DWORD);
  LONG retCode =
      RegQueryValueExW(key, valueName, 0, nullptr,
                       reinterpret_cast<LPBYTE>(&retValue), &regDWORDValueSize);
  return ERROR_SUCCESS == retCode;
}

/**
 * Determines if the the system's elevation type allows
 * unprmopted elevation.
 *
 * @param isUnpromptedElevation Out parameter which specifies if unprompted
 *                              elevation is allowed.
 * @return TRUE if the user can actually elevate and the value was obtained
 *         successfully.
 */
BOOL IsUnpromptedElevation(BOOL& isUnpromptedElevation) {
  if (!UACHelper::CanUserElevate()) {
    return FALSE;
  }

  LPCWSTR UACBaseRegKey =
      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
  HKEY baseKey;
  LONG retCode =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE, UACBaseRegKey, 0, KEY_READ, &baseKey);
  if (retCode != ERROR_SUCCESS) {
    return FALSE;
  }

  DWORD consent, secureDesktop;
  BOOL success = GetDWORDValue(baseKey, L"ConsentPromptBehaviorAdmin", consent);
  success = success &&
            GetDWORDValue(baseKey, L"PromptOnSecureDesktop", secureDesktop);

  RegCloseKey(baseKey);
  if (success) {
    isUnpromptedElevation = !consent && !secureDesktop;
  }

  return success;
}
#endif

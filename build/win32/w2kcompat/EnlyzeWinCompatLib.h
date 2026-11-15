//
// EnlyzeWinCompatLib - Let MSVC v141_xp targeted applications run on even older Windows versions
// Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
// SPDX-License-Identifier: MIT
//

// To include EnlyzeWinCompatLib in your application, do the following:
//   1. Add it as a Project Dependency.
//   2. Turn off Visual Studio's default "Inherit from parent or project defaults" option
//      in Project Properties -> Linker -> Input -> Additional Dependencies.
//   3. Include this file in your application.
//
// This ensures that EnlyzeWinCompatLib's compatibility code is linked before the Win32 libraries
// and therefore preferred.

#pragma once

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "odbc32.lib")
#pragma comment(lib, "odbccp32.lib")

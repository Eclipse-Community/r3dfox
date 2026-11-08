//
// EnlyzeWinCompatLib - Let MSVC v141_xp targeted applications run on even older Windows versions
// Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
// SPDX-License-Identifier: MIT
//

#pragma once

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif

#define _WIN32_WINNT _WIN32_WINNT_WIN2K
#include <SDKDDKVer.h>

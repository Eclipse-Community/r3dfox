/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include "DisplayConfigWindows.h"

namespace mozilla {
namespace gfx {

using namespace std;

optional<DisplayConfig> GetDisplayConfig() {
  LONG result;

  UINT32 numPaths;
  UINT32 numModes;
  vector<DISPLAYCONFIG_PATH_INFO> paths;
  vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG (WINAPI * pfnQueryDisplayConfig)(UINT Flags,UINT *pNumPathArrayElements, DISPLAYCONFIG_PATH_INFO *pPathInfoArray,
                                      UINT *pNumModeInfoArrayElements, DISPLAYCONFIG_MODE_INFO *pModeInfoArray,
                                      DISPLAYCONFIG_TOPOLOGY_ID *pCurrentTopologyId) = 0;
    LONG (WINAPI * pfnGetDisplayConfigBufferSizes)(UINT Flags, UINT *pNumPathArrayElements, UINT *pNumModeInfoArrayElements) = 0;
    bool bSupported = false;
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        if (hUser)
        {
                bSupported = true;

                *(uintptr_t *)&pfnQueryDisplayConfig = (uintptr_t)GetProcAddress(hUser, "QueryDisplayConfig");
                bSupported &= !!(pfnQueryDisplayConfig);

                *(uintptr_t *)&pfnGetDisplayConfigBufferSizes = (uintptr_t)GetProcAddress(hUser, "GetDisplayConfigBufferSizes");
                bSupported &= !!(pfnGetDisplayConfigBufferSizes);
        }
    if (!bSupported) {
      return {};
    }
  do {
    result = pfnGetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths,
                                         &numModes);
    if (result != ERROR_SUCCESS) {
      return {};
    }
    // allocate the recommended amount of space
    paths.resize(numPaths);
    modes.resize(numModes);

    result = pfnQueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(),
                                &numModes, modes.data(), NULL);
    // try again if there wasn't enough space
  } while (result == ERROR_INSUFFICIENT_BUFFER);

  if (result != ERROR_SUCCESS) return {};

  // shrink to fit the actual number of modes and paths returned
  modes.resize(numModes);
  paths.resize(numPaths);

  return DisplayConfig{paths, modes};
}

bool HasScaledResolution() {
  auto config = GetDisplayConfig();
  if (config) {
    for (auto& path : config->mPaths) {
      auto& modes = config->mModes;
      int targetModeIndex = path.targetInfo.modeInfoIdx;
      int sourceModeIndex = path.sourceInfo.modeInfoIdx;

      // Check if the source and target resolutions are different
      if ((modes[targetModeIndex]
               .targetMode.targetVideoSignalInfo.activeSize.cx !=
           modes[sourceModeIndex].sourceMode.width) ||
          (modes[targetModeIndex]
               .targetMode.targetVideoSignalInfo.activeSize.cy !=
           modes[sourceModeIndex].sourceMode.height)) {
        return true;
      }
    }
  }
  return false;
}

void GetScaledResolutions(ScaledResolutionSet& aRv) {
  auto config = GetDisplayConfig();
  if (config) {
    for (auto& path : config->mPaths) {
      auto& modes = config->mModes;
      int targetModeIndex = path.targetInfo.modeInfoIdx;
      int sourceModeIndex = path.sourceInfo.modeInfoIdx;

      // Check if the source and target resolutions are different
      IntSize src(modes[sourceModeIndex].sourceMode.width,
                  modes[sourceModeIndex].sourceMode.height);
      IntSize dst(
          modes[targetModeIndex].targetMode.targetVideoSignalInfo.activeSize.cx,
          modes[targetModeIndex]
              .targetMode.targetVideoSignalInfo.activeSize.cy);
      if (src != dst) {
        aRv.AppendElement(std::pair<IntSize, IntSize>{src, dst});
      }
    }
  }
}

}  // namespace gfx
}  // namespace mozilla

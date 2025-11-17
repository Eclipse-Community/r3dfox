#include <windows.h>
#include <iostream>
#include <string>

void usage(int argc, wchar_t** argv) {
  std::wcout << L"Usage:\n"
             << L"  " << argv[0] << L" filename age\n\n"
             << L"    filename\n"
             << L"      The path of the file to change.\n"
             << L"    age\n"
             << L"      The number of seconds in the past to set the file's \n"
             << L"      modification time to.\n\n";
}

static const ULONGLONG FILETIME_TICKS_PER_SECOND = 10'000'000;

static ULARGE_INTEGER secondsAgoTimestamp(ULONGLONG secondsAgo) {
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER systemTime;
  systemTime.LowPart = now.dwLowDateTime;
  systemTime.HighPart = now.dwHighDateTime;
  systemTime.QuadPart -= secondsAgo * FILETIME_TICKS_PER_SECOND;
  return systemTime;
}

int wmain(int argc, wchar_t** argv) {
  HANDLE handle;
  FILE_BASIC_INFO fileBasicInfo{};
  long ageInSeconds;
  if (argc != 3) {
    usage(argc, argv);
    return 1;
  }
  if (!swscanf(argv[2], L"%lu", &ageInSeconds)) {
    std::wcout << "Invalid seconds: " << argv[2] << std::endl;
    return 2;
  }
  if ((handle = CreateFileW(argv[1], GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr)) ==
      INVALID_HANDLE_VALUE) {
    std::wcout << "Error opening file: " << argv[1] << ", " << GetLastError()
               << std::endl;
    return 3;
  }
  if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &fileBasicInfo,
                                    sizeof(fileBasicInfo))) {
    std::wcout << "Error getting file info: " << GetLastError() << std::endl;
    return 4;
  }

  ULARGE_INTEGER desiredTimestamp = secondsAgoTimestamp(ageInSeconds);
  fileBasicInfo.LastWriteTime.QuadPart = desiredTimestamp.QuadPart;

  if (!SetFileInformationByHandle(handle, FileBasicInfo, &fileBasicInfo,
                                  sizeof(fileBasicInfo))) {
    std::wcout << "Error setting file mtime: " << GetLastError() << std::endl;
    return 5;
  }
  if (!CloseHandle(handle)) {
    std::wcout << "Error closing handle: " << GetLastError() << std::endl;
    return 6;
  }
  return 0;
}

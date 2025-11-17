#include <windows.h>
#include <iostream>
#include <string>

// Opens a file and holds it open until the caller sends input.
// Then it closes the file and exits.

void usage(int argc, wchar_t** argv) {
  std::wcout << L"Usage:\n"
             << L"  " << argv[0] << L" filename [sharing]\n\n"
             << L"    filename\n"
             << L"      The path of the file to hold open.\n"
             << L"    sharing\n"
             << L"      File sharing flags. 'r', 'w', and/or 'd' can be\n"
             << L"      specified. Defaults to no sharing.\n\n";
}

int wmain(int argc, wchar_t** argv) {
  DWORD dwShareMode = 0;

  if (argc < 2 || argc > 3) {
    usage(argc, argv);
    exit(1);
  }
  if (argc >= 3) {
    for (wchar_t* curr = argv[2]; *curr; ++curr) {
      switch (*curr) {
        case L'w':
          dwShareMode |= FILE_SHARE_WRITE;
          break;
        case L'r':
          dwShareMode |= FILE_SHARE_READ;
          break;
        case L'd':
          dwShareMode |= FILE_SHARE_DELETE;
          break;
        default:
          usage(argc, argv);
          return 1;
      }
    }
  }
  HANDLE handle = CreateFileW(argv[1], GENERIC_READ, dwShareMode, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    std::wcout << L"failed to open file " << argv[1] << std::endl;
    return 1;
  }
  std::wcout << "Locked" << std::endl;
  (void)std::wcin.get();
  CloseHandle(handle);
  return 0;
}

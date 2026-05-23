/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Win32SerialPlatformService.h"

#include <cfgmgr32.h>
// Including initguid.h needs to come before including devpkey.h, so
// disable clang-format here.

// clang-format off
#include <initguid.h>
#include <devpkey.h>
// clang-format on

#include <ntddser.h>
#include <setupapi.h>

#include "SerialLogging.h"
#include "mozilla/AsyncPlatformPipes.h"
#include "mozilla/ScopeExit.h"
#include "nsString.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

namespace {
constexpr size_t kPropertyBufferSize = 256;
constexpr unsigned int kDeviceChangeDelayMs = 200;
constexpr wchar_t kDevicePathPrefix[] = L"\\\\.\\";
}  // namespace

Win32SerialPlatformService::Win32SerialPlatformService()
    : mIOCapability(IOThread()) {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p] created", this));
  MOZ_ALWAYS_SUCCEEDS(NS_CreateBackgroundTaskQueue(
      "SerialMonitorQueue", getter_AddRefs(mMonitorThread)));
}

nsresult Win32SerialPlatformService::Init() {
  return StartMonitoringDeviceChanges();
}

void Win32SerialPlatformService::Shutdown() {
  AssertIsOnMainThread();
  if (IsShutdown()) {
    return;
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p]::Shutdown", this));
  StopMonitoringDeviceChanges();
  mMonitorThread = nullptr;
  SerialPlatformService::Shutdown();
}

Win32SerialPlatformService::~Win32SerialPlatformService() {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p] destroyed (closing %u ports)", this,
           mOpenPorts.Count()));
  for (auto iter = mOpenPorts.Iter(); !iter.Done(); iter.Next()) {
    HANDLE handle = iter.Data();
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
  mOpenPorts.Clear();
}

namespace {
nsresult EnumeratePortsWin32(SerialPortList& aPorts) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService::EnumeratePorts"));
  aPorts.Clear();

  HDEVINFO deviceInfoSet =
      SetupDiGetClassDevs(&GUID_DEVINTERFACE_COMPORT, nullptr, nullptr,
                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

  if (deviceInfoSet == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService::EnumeratePorts "
             "SetupDiGetClassDevs failed: 0x%08lx",
             error));
    return NS_ERROR_FAILURE;
  }
  auto cleanupDeviceInfoSet =
      MakeScopeExit([&]() { SetupDiDestroyDeviceInfoList(deviceInfoSet); });

  SP_DEVINFO_DATA deviceInfo;
  deviceInfo.cbSize = sizeof(SP_DEVINFO_DATA);

  for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfo); ++i) {
    HKEY hKey = SetupDiOpenDevRegKey(deviceInfoSet, &deviceInfo,
                                     DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);

    if (hKey == INVALID_HANDLE_VALUE) {
      continue;
    }
    wchar_t portName[kPropertyBufferSize];
    {
      auto cleanupRegKey = MakeScopeExit([&]() { RegCloseKey(hKey); });

      DWORD portNameSize = sizeof(portName);
      LONG result =
          RegGetValueW(hKey, nullptr, L"PortName", RRF_RT_REG_SZ, nullptr,
                       reinterpret_cast<LPBYTE>(portName), &portNameSize);
      if (result != ERROR_SUCCESS) {
        continue;
      }
    }

    wchar_t friendlyName[kPropertyBufferSize] = {0};
    DWORD friendlyNameSize = sizeof(friendlyName);
    // The bus reported device description is usually more descriptive than
    // the friendly name (for LEGO Spike, Flipper Zero, etc.)
    auto deviceDescriptionPropKey = DEVPKEY_Device_BusReportedDeviceDesc;
    DEVPROPTYPE unusedPropertyType;
    BOOL succeeded = SetupDiGetDevicePropertyW(
        deviceInfoSet, &deviceInfo, &deviceDescriptionPropKey,
        &unusedPropertyType, reinterpret_cast<PBYTE>(friendlyName),
        friendlyNameSize, nullptr, 0);
    if (!(succeeded && *friendlyName)) {
      deviceDescriptionPropKey = DEVPKEY_Device_FriendlyName;
      succeeded = SetupDiGetDevicePropertyW(
          deviceInfoSet, &deviceInfo, &deviceDescriptionPropKey,
          &unusedPropertyType, reinterpret_cast<PBYTE>(friendlyName),
          friendlyNameSize, nullptr, 0);
    }
    if (!(succeeded && *friendlyName)) {
      wcscpy_s(friendlyName, portName);
    }
    // SPDRP_HARDWAREID is of type REG_MULTI_SZ. We just are interested
    // in the first one, and since each string is null-terminated, we'll
    // just treat it as a regular string.
    wchar_t hardwareId[kPropertyBufferSize] = {0};
    SetupDiGetDeviceRegistryPropertyW(
        deviceInfoSet, &deviceInfo, SPDRP_HARDWAREID, nullptr,
        reinterpret_cast<PBYTE>(hardwareId), sizeof(hardwareId), nullptr);

    IPCSerialPortInfo info;
    info.id() = nsString(portName);
    info.friendlyName() = nsString(friendlyName);

    {
      nsString path(kDevicePathPrefix);
      path.Append(portName);
      info.path() = std::move(path);
    }

    uint16_t vendorId = 0;
    uint16_t productId = 0;
    wchar_t* vidLocation = wcsstr(hardwareId, L"VID_");
    if (vidLocation) {
      swscanf_s(vidLocation + 4, L"%4hx", &vendorId);
    }
    wchar_t* pidLocation = wcsstr(hardwareId, L"PID_");
    if (pidLocation) {
      swscanf_s(pidLocation + 4, L"%4hx", &productId);
    }

    if (vendorId != 0 && productId != 0) {
      info.usbVendorId() = Some(vendorId);
      info.usbProductId() = Some(productId);
    }

    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("Win32SerialPlatformService::EnumeratePorts found port '%s' (%s) "
             "VID:0x%04x PID:0x%04x",
             NS_ConvertUTF16toUTF8(info.id()).get(),
             NS_ConvertUTF16toUTF8(info.friendlyName()).get(), vendorId,
             productId));
    aPorts.AppendElement(info);
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService::EnumeratePorts found %zu ports",
           aPorts.Length()));
  return NS_OK;
}
}  // namespace

nsresult Win32SerialPlatformService::EnumeratePortsImpl(
    SerialPortList& aPorts) {
  return EnumeratePortsWin32(aPorts);
}

HANDLE Win32SerialPlatformService::FindPortHandle(const nsString& aPortId) {
  mIOCapability.AssertOnCurrentThread();
  return mOpenPorts.MaybeGet(aPortId).valueOr(INVALID_HANDLE_VALUE);
}

nsresult Win32SerialPlatformService::ConfigurePort(
    HANDLE aHandle, const IPCSerialOptions& aOptions) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::ConfigurePort (baudRate=%u, "
           "dataBits=%u, stopBits=%u, parity=%u, flowControl=%u)",
           this, aOptions.baudRate(), aOptions.dataBits(), aOptions.stopBits(),
           static_cast<unsigned>(aOptions.parity()),
           static_cast<unsigned>(aOptions.flowControl())));
  mIOCapability.AssertOnCurrentThread();

  DCB dcb = {0};
  dcb.DCBlength = sizeof(DCB);

  if (!GetCommState(aHandle, &dcb)) {
    DWORD error = GetLastError();
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("Win32SerialPlatformService[%p]::ConfigurePort GetCommState failed: "
         "0x%08lx",
         this, error));
    return NS_ERROR_FAILURE;
  }

  // These options are not configurable
  dcb.fBinary = TRUE;
  dcb.fParity = TRUE;
  dcb.fAbortOnError = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;

  dcb.BaudRate = aOptions.baudRate();

  switch (aOptions.dataBits()) {
    case 7:
      dcb.ByteSize = 7;
      break;
    case 8:
      dcb.ByteSize = 8;
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }

  switch (aOptions.stopBits()) {
    case 1:
      dcb.StopBits = ONESTOPBIT;
      break;
    case 2:
      dcb.StopBits = TWOSTOPBITS;
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }

  switch (aOptions.parity()) {
    case ParityType::None:
      dcb.Parity = NOPARITY;
      break;
    case ParityType::Even:
      dcb.Parity = EVENPARITY;
      break;
    case ParityType::Odd:
      dcb.Parity = ODDPARITY;
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }

  switch (aOptions.flowControl()) {
    case FlowControlType::None:
      dcb.fOutxCtsFlow = FALSE;
      dcb.fRtsControl = RTS_CONTROL_ENABLE;
      break;
    case FlowControlType::Hardware:
      dcb.fOutxCtsFlow = TRUE;
      dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }

  if (!SetCommState(aHandle, &dcb)) {
    DWORD error = GetLastError();
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("Win32SerialPlatformService[%p]::ConfigurePort SetCommState failed: "
         "0x%08lx",
         this, error));
    return NS_ERROR_FAILURE;
  }

  COMMTIMEOUTS timeouts = {0};
  // When both ReadIntervalTimeout and ReadTotalTimeoutMultiplier are MAXDWORD,
  // ReadFile returns immediately with buffered data, or waits up to
  // ReadTotalTimeoutConstant ms for the first byte. We use a very large
  // constant (MAXDWORD-1 ~= 49 days) so the overlapped ReadFile effectively
  // waits forever for data, ensuring bytesRead > 0 on every completion.
  // PlatformPipeReader treats 0-byte reads as EOF, so we must never time out
  // with 0 bytes. Using MAXDWORD for all three would mean "return immediately
  // even with 0 bytes" which we must avoid.
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = MAXDWORD - 1;
  // Write timeout as a safety net.
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 5000;

  if (!SetCommTimeouts(aHandle, &timeouts)) {
    DWORD error = GetLastError();
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::ConfigurePort SetCommTimeouts "
             "failed: 0x%08lx",
             this, error));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::ConfigurePort succeeded", this));
  return NS_OK;
}

nsresult Win32SerialPlatformService::OpenImpl(
    const nsString& aPortId, const IPCSerialOptions& aOptions) {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p]::Open port '%s'", this,
           NS_ConvertUTF16toUTF8(aPortId).get()));
  mIOCapability.AssertOnCurrentThread();

  // Validate portId format: must be "COM" followed by one or more digits.
  // This prevents a compromised content process from using a crafted portId
  // to open arbitrary devices in the \\.\ namespace (e.g. PhysicalDrive0).
  if (aPortId.Length() < 4 || !StringBeginsWith(aPortId, u"COM"_ns)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::Open rejected invalid portId "
             "'%s': bad prefix",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_INVALID_ARG;
  }
  for (uint32_t i = 3; i < aPortId.Length(); i++) {
    if (!iswdigit(aPortId.CharAt(i))) {
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("Win32SerialPlatformService[%p]::Open rejected invalid portId "
               "'%s': non-digit character",
               this, NS_ConvertUTF16toUTF8(aPortId).get()));
      return NS_ERROR_INVALID_ARG;
    }
  }

  if (mOpenPorts.Contains(aPortId)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Warning,
            ("Win32SerialPlatformService[%p]::Open port '%s' already open",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_FILE_IS_LOCKED;
  }

  nsString devicePath(kDevicePathPrefix);
  devicePath.Append(aPortId);

  HANDLE handle =
      CreateFileW(devicePath.get(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::Open CreateFileW failed for port "
             "'%s' at path '%s': 0x%08lx",
             this, NS_ConvertUTF16toUTF8(aPortId).get(),
             NS_ConvertUTF16toUTF8(devicePath).get(), error));
    if (error == ERROR_ACCESS_DENIED) {
      return NS_ERROR_FILE_ACCESS_DENIED;
    }
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv = ConfigurePort(handle, aOptions);
  if (NS_FAILED(rv)) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("Win32SerialPlatformService[%p]::Open ConfigurePort failed for port "
         "'%s': 0x%08x",
         this, NS_ConvertUTF16toUTF8(aPortId).get(),
         static_cast<uint32_t>(rv)));
    CloseHandle(handle);
    return rv;
  }

  PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

  mOpenPorts.InsertOrUpdate(aPortId, handle);
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p]::Open succeeded for port '%s'", this,
           NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult Win32SerialPlatformService::CloseImpl(const nsString& aPortId) {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p]::Close port '%s'", this,
           NS_ConvertUTF16toUTF8(aPortId).get()));
  mIOCapability.AssertOnCurrentThread();

  HANDLE handle = FindPortHandle(aPortId);
  if (handle == INVALID_HANDLE_VALUE) {
    MOZ_LOG(gWebSerialLog, LogLevel::Warning,
            ("Win32SerialPlatformService[%p]::Close port '%s' not found", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  mOpenPorts.Remove(aPortId);
  CloseHandle(handle);
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p]::Close succeeded for port '%s'",
           this, NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult Win32SerialPlatformService::WriteImpl(const nsString& aPortId,
                                               Span<const uint8_t> aData) {
  mIOCapability.AssertOnCurrentThread();
  HANDLE handle = FindPortHandle(aPortId);
  if (handle == INVALID_HANDLE_VALUE) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::Write port '%s' not found", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (aData.IsEmpty()) {
    MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
            ("Win32SerialPlatformService[%p]::Write empty data for port '%s'",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_OK;
  }

  MOZ_LOG(
      gWebSerialLog, LogLevel::Verbose,
      ("Win32SerialPlatformService[%p]::Write writing %zu bytes to port '%s'",
       this, aData.Length(), NS_ConvertUTF16toUTF8(aPortId).get()));

  DWORD totalWritten = 0;
  const uint8_t* buffer = aData.Elements();
  DWORD remaining = static_cast<DWORD>(aData.Length());

  auto event = UniqueFileHandle(CreateEvent(nullptr, TRUE, FALSE, nullptr));
  if (!event) {
    return NS_ERROR_FAILURE;
  }
  OVERLAPPED ov = {};
  // Setting the low-order bit of hEvent prevents the I/O completion from
  // being queued to the IOCP. This is necessary because PlatformPipeReader
  // may have registered a DuplicateHandle of this port with the IOCP, and
  // completions from our local OVERLAPPED would corrupt the IOCP handler
  // lookup. GetOverlappedResult still works via the event.
  HANDLE rawEvent = event.get();
  ov.hEvent =
      reinterpret_cast<HANDLE>(reinterpret_cast<uintptr_t>(rawEvent) | 1);

  while (remaining > 0) {
    ResetEvent(event.get());

    if (!WriteFile(handle, buffer + totalWritten, remaining, nullptr, &ov)) {
      DWORD error = GetLastError();
      if (error != ERROR_IO_PENDING) {
        MOZ_LOG(
            gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::Write WriteFile failed for port "
             "'%s': 0x%08lx",
             this, NS_ConvertUTF16toUTF8(aPortId).get(), error));
        return NS_ERROR_FAILURE;
      }
    }
    DWORD bytesWritten = 0;
    if (!GetOverlappedResult(handle, &ov, &bytesWritten, TRUE)) {
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("Win32SerialPlatformService[%p]::Write GetOverlappedResult "
               "failed for port '%s': 0x%08lx",
               this, NS_ConvertUTF16toUTF8(aPortId).get(), GetLastError()));
      return NS_ERROR_FAILURE;
    }

    if (bytesWritten == 0) {
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("Win32SerialPlatformService[%p]::Write WriteFile returned 0 for "
               "port '%s'",
               this, NS_ConvertUTF16toUTF8(aPortId).get()));
      return NS_ERROR_FAILURE;
    }

    totalWritten += bytesWritten;
    remaining -= bytesWritten;

    if (remaining > 0) {
      MOZ_LOG(gWebSerialLog, LogLevel::Verbose,
              ("Win32SerialPlatformService[%p]::Write partial write for port "
               "'%s': %lu bytes, %lu remaining",
               this, NS_ConvertUTF16toUTF8(aPortId).get(), bytesWritten,
               remaining));
    }
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::Write wrote %lu bytes to port '%s'",
           this, totalWritten, NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult Win32SerialPlatformService::DrainImpl(const nsString& aPortId) {
  mIOCapability.AssertOnCurrentThread();
  HANDLE handle = FindPortHandle(aPortId);
  if (handle == INVALID_HANDLE_VALUE) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::Drain port not found: %s", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("Win32SerialPlatformService[%p]::Drain draining transmit buffers for "
       "port '%s'",
       this, NS_ConvertUTF16toUTF8(aPortId).get()));

  if (!FlushFileBuffers(handle)) {
    DWORD error = GetLastError();
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("Win32SerialPlatformService[%p]::Drain FlushFileBuffers failed for "
         "port '%s': error=%lu",
         this, NS_ConvertUTF16toUTF8(aPortId).get(), error));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::Drain successfully drained buffers "
           "for port '%s'",
           this, NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult Win32SerialPlatformService::FlushImpl(const nsString& aPortId,
                                               bool aReceive) {
  mIOCapability.AssertOnCurrentThread();
  HANDLE handle = FindPortHandle(aPortId);
  if (handle == INVALID_HANDLE_VALUE) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::Flush port not found: %s", this,
             NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  DWORD flags = aReceive ? PURGE_RXCLEAR : PURGE_TXCLEAR;
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::Flush discarding %s buffers "
           "for port '%s'",
           this, aReceive ? "receive" : "transmit",
           NS_ConvertUTF16toUTF8(aPortId).get()));

  if (!PurgeComm(handle, flags)) {
    DWORD error = GetLastError();
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::Flush PurgeComm failed for port "
             "'%s': error=%lu",
             this, NS_ConvertUTF16toUTF8(aPortId).get(), error));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::Flush successfully flushed %s "
           "buffers for port '%s'",
           this, aReceive ? "receive" : "transmit",
           NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult Win32SerialPlatformService::SetSignalsImpl(
    const nsString& aPortId, const IPCSerialOutputSignals& aSignals) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::SetSignals for port '%s' (DTR=%s, "
           "RTS=%s, Break=%s)",
           this, NS_ConvertUTF16toUTF8(aPortId).get(),
           aSignals.dataTerminalReady().isSome()
               ? (aSignals.dataTerminalReady().value() ? "true" : "false")
               : "unset",
           aSignals.requestToSend().isSome()
               ? (aSignals.requestToSend().value() ? "true" : "false")
               : "unset",
           aSignals.breakSignal().isSome()
               ? (aSignals.breakSignal().value() ? "true" : "false")
               : "unset"));
  mIOCapability.AssertOnCurrentThread();

  HANDLE handle = FindPortHandle(aPortId);
  if (handle == INVALID_HANDLE_VALUE) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::SetSignals port '%s' not found",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (aSignals.dataTerminalReady().isSome()) {
    if (!EscapeCommFunction(
            handle, aSignals.dataTerminalReady().value() ? SETDTR : CLRDTR)) {
      DWORD error = GetLastError();
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("Win32SerialPlatformService[%p]::SetSignals EscapeCommFunction DTR "
           "failed for port '%s': 0x%08lx",
           this, NS_ConvertUTF16toUTF8(aPortId).get(), error));
      return NS_ERROR_FAILURE;
    }
  }

  if (aSignals.requestToSend().isSome()) {
    if (!EscapeCommFunction(
            handle, aSignals.requestToSend().value() ? SETRTS : CLRRTS)) {
      DWORD error = GetLastError();
      MOZ_LOG(
          gWebSerialLog, LogLevel::Error,
          ("Win32SerialPlatformService[%p]::SetSignals EscapeCommFunction RTS "
           "failed for port '%s': 0x%08lx",
           this, NS_ConvertUTF16toUTF8(aPortId).get(), error));
      return NS_ERROR_FAILURE;
    }
  }

  if (aSignals.breakSignal().isSome()) {
    if (!EscapeCommFunction(
            handle, aSignals.breakSignal().value() ? SETBREAK : CLRBREAK)) {
      DWORD error = GetLastError();
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("Win32SerialPlatformService[%p]::SetSignals EscapeCommFunction "
               "Break failed for port '%s': 0x%08lx",
               this, NS_ConvertUTF16toUTF8(aPortId).get(), error));
      return NS_ERROR_FAILURE;
    }
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::SetSignals succeeded for port '%s'",
           this, NS_ConvertUTF16toUTF8(aPortId).get()));
  return NS_OK;
}

nsresult Win32SerialPlatformService::GetSignalsImpl(
    const nsString& aPortId, IPCSerialInputSignals& aSignals) {
  mIOCapability.AssertOnCurrentThread();
  HANDLE handle = FindPortHandle(aPortId);
  if (handle == INVALID_HANDLE_VALUE) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::GetSignals port '%s' not found",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  DWORD status = 0;
  if (!GetCommModemStatus(handle, &status)) {
    DWORD error = GetLastError();
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("Win32SerialPlatformService[%p]::GetSignals GetCommModemStatus failed "
         "for port '%s': 0x%08lx",
         this, NS_ConvertUTF16toUTF8(aPortId).get(), error));
    return NS_ERROR_FAILURE;
  }

  aSignals = IPCSerialInputSignals{
      (status & MS_RLSD_ON) != 0,  // dataCarrierDetect (DCD)
      (status & MS_CTS_ON) != 0,   // clearToSend (CTS)
      (status & MS_RING_ON) != 0,  // ringIndicator (RI)
      (status & MS_DSR_ON) != 0    // dataSetReady (DSR)
  };

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::GetSignals for port '%s': DCD=%s, "
           "CTS=%s, RI=%s, DSR=%s",
           this, NS_ConvertUTF16toUTF8(aPortId).get(),
           aSignals.dataCarrierDetect() ? "true" : "false",
           aSignals.clearToSend() ? "true" : "false",
           aSignals.ringIndicator() ? "true" : "false",
           aSignals.dataSetReady() ? "true" : "false"));

  return NS_OK;
}

nsresult Win32SerialPlatformService::GetReadStreamImpl(
    const nsString& aPortId, uint32_t aBufferSize,
    nsIAsyncInputStream** aStream) {
  mIOCapability.AssertOnCurrentThread();
  HANDLE handle = FindPortHandle(aPortId);
  if (handle == INVALID_HANDLE_VALUE) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("Win32SerialPlatformService[%p]::GetReadStream port '%s' not found",
         this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_NOT_AVAILABLE;
  }
  UniqueFileHandle readHandle = DuplicateFileHandle(handle);
  if (!readHandle) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::GetReadStream DuplicateHandle "
             "failed for port '%s'",
             this, NS_ConvertUTF16toUTF8(aPortId).get()));
    return NS_ERROR_FAILURE;
  }
  RefPtr<PlatformPipeReader> reader =
      MakeRefPtr<PlatformPipeReader>(std::move(readHandle), aBufferSize);
  reader.forget(aStream);
  return NS_OK;
}

nsresult Win32SerialPlatformService::StartMonitoringDeviceChanges() {
  if (mMonitoring) {
    return NS_OK;
  }
  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("Win32SerialPlatformService[%p]::StartMonitoringDeviceChanges", this));

  CM_NOTIFY_FILTER filter = {};
  filter.cbSize = sizeof(CM_NOTIFY_FILTER);
  filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
  filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_COMPORT;

  CONFIGRET cr=CR_SUCCESS;
typedef CONFIGRET (WINAPI *MYPROC)(PCM_NOTIFY_FILTER   pFilter,
  PVOID               pContext,
  PCM_NOTIFY_CALLBACK pCallback,
  PHCMNOTIFICATION    pNotifyContext); 
    HINSTANCE hinstLib; 
    MYPROC ProcAdd; 
    BOOL fRunTimeLinkSuccess = FALSE; 
 
    // Get a handle to the DLL module.
 
    hinstLib = LoadLibraryW(L"CfgMgr32.dll"); 
 
    // If the handle is valid, try to get the function address.
 
    if (hinstLib != NULL) 
    { 
        ProcAdd = (MYPROC) GetProcAddress(hinstLib, "CM_Register_Notification"); 
 
        // If the function address is valid, call the function.
 
        if (NULL != ProcAdd) 
        {
            fRunTimeLinkSuccess = TRUE;
  cr = (ProcAdd)(
      &filter, this, DeviceNotificationCallback, &mDeviceNotification);
        }
        // Free the DLL module.
 
        FreeLibrary(hinstLib); 
    } 

    // If unable to call the DLL function, use an alternative.
  if (! fRunTimeLinkSuccess||cr != CR_SUCCESS) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::StartMonitoringDeviceChanges "
             "CM_Register_Notification failed: 0x%08lx",
             this, cr));
    return NS_ERROR_FAILURE;
  }

  mMonitoring = true;

  auto cachedPortList = mCachedPortList.Lock();
  nsresult rv = EnumeratePortsWin32(*cachedPortList);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::StartMonitoringDeviceChanges "
             "EnumeratePorts failed: 0x%08x",
             this, static_cast<unsigned>(rv)));
    return rv;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("Win32SerialPlatformService[%p]::StartMonitoringDeviceChanges "
           "succeeded, "
           "monitoring %zu ports",
           this, cachedPortList->Length()));
  return NS_OK;
}

void Win32SerialPlatformService::StopMonitoringDeviceChanges() {
  AssertIsOnMainThread();
  if (!mMonitoring) {
    return;
  }

  MOZ_LOG(
      gWebSerialLog, LogLevel::Debug,
      ("Win32SerialPlatformService[%p]::StopMonitoringDeviceChanges", this));

  if (mDeviceNotification) {
typedef CONFIGRET (WINAPI *MYPROC)(HCMNOTIFICATION ); 
 
    HINSTANCE hinstLib; 
    MYPROC ProcAdd; 
 
    // Get a handle to the DLL module.
 
    hinstLib = LoadLibraryW(L"MyPuts.dll"); 
 
    // If the handle is valid, try to get the function address.
 
    if (hinstLib != NULL) 
    { 
        ProcAdd = (MYPROC) GetProcAddress(hinstLib, "CM_Unregister_Notification"); 
 
        // If the function address is valid, call the function.
 
        if (NULL != ProcAdd) 
        {
    (ProcAdd)(mDeviceNotification);
        }
        // Free the DLL module.
 
        FreeLibrary(hinstLib); 
    } 
    mDeviceNotification = nullptr;
  }

  mMonitoring = false;
}

DWORD CALLBACK Win32SerialPlatformService::DeviceNotificationCallback(
    HCMNOTIFICATION hNotify, PVOID Context, CM_NOTIFY_ACTION Action,
    PCM_NOTIFY_EVENT_DATA EventData, DWORD EventDataSize) {
  RefPtr<SerialPlatformService> service = SerialPlatformService::GetInstance();
  if (!service) {
    return ERROR_SUCCESS;
  }
  auto* winService = static_cast<Win32SerialPlatformService*>(service.get());

  if (!(Action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL ||
        Action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)) {
    return ERROR_SUCCESS;
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::DeviceNotificationCallback "
           "action=%d",
           winService, Action));

  // Only schedule a check if one isn't already pending
  bool expected = false;
  if (winService->mCheckPending.compareExchange(expected, true)) {
    // Dispatch to the monitor thread (separate from the I/O queue so device
    // change detection isn't blocked by hanging serial I/O calls).
    nsresult rv = winService->mMonitorThread->DelayedDispatch(
        NS_NewRunnableFunction(
            "Win32SerialPlatformService::CheckForDeviceChanges",
            [self = RefPtr{winService}]() {
              self->mCheckPending = false;
              self->CheckForDeviceChanges();
            }),
        kDeviceChangeDelayMs);
    if (NS_FAILED(rv)) {
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("Win32SerialPlatformService[%p]::"
               "DeviceNotificationCallback DelayedDispatch failed: 0x%08x",
               winService, static_cast<uint32_t>(rv)));
      winService->mCheckPending = false;
    } else {
      MOZ_LOG(gWebSerialLog, LogLevel::Debug,
              ("Win32SerialPlatformService[%p]::"
               "DeviceNotificationCallback scheduled CheckForDeviceChanges",
               winService));
    }
  } else {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("Win32SerialPlatformService[%p]::"
             "DeviceNotificationCallback check already pending, skipping",
             winService));
  }

  return ERROR_SUCCESS;
}

void Win32SerialPlatformService::CheckForDeviceChanges() {
  if (IsShutdown()) {
    return;
  }
  MOZ_ASSERT(mMonitorThread->IsOnCurrentThread());
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("Win32SerialPlatformService[%p]::CheckForDeviceChanges", this));

  SerialPortList newPortList;
  nsresult rv = EnumeratePortsWin32(newPortList);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("Win32SerialPlatformService[%p]::CheckForDeviceChanges "
             "EnumeratePorts failed: 0x%08x",
             this, static_cast<unsigned>(rv)));
    return;
  }

  auto cachedPortList = mCachedPortList.Lock();

  // Find newly connected ports
  for (const auto& newPort : newPortList) {
    bool found = false;
    for (const auto& oldPort : *cachedPortList) {
      if (oldPort.id() == newPort.id()) {
        found = true;
        break;
      }
    }
    if (!found) {
      MOZ_LOG(gWebSerialLog, LogLevel::Info,
              ("Win32SerialPlatformService[%p]::CheckForDeviceChanges port "
               "connected: '%s'",
               this, NS_ConvertUTF16toUTF8(newPort.id()).get()));
      NotifyPortConnected(newPort);
    }
  }

  // Find disconnected ports
  for (const auto& oldPort : *cachedPortList) {
    bool found = false;
    for (const auto& newPort : newPortList) {
      if (oldPort.id() == newPort.id()) {
        found = true;
        break;
      }
    }
    if (!found) {
      MOZ_LOG(gWebSerialLog, LogLevel::Info,
              ("Win32SerialPlatformService[%p]::CheckForDeviceChanges port "
               "disconnected: '%s'",
               this, NS_ConvertUTF16toUTF8(oldPort.id()).get()));
      // Since we're just using synchonous I/O for now, if the OS detects
      // a device gets disconnected any pending writes will immediately
      // fail, so we don't need to do any sort of cancelling here.
      NotifyPortDisconnected(oldPort.id());
    }
  }

  *cachedPortList = std::move(newPortList);
}

}  // namespace mozilla::dom

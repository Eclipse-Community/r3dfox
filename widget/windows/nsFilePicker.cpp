/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFilePicker.h"

#include <cderr.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sysinfoapi.h>
#include <winerror.h>
#include <winuser.h>
#include <utility>

#include "ContentAnalysis.h"
#include "mozilla/Assertions.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/Components.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/Logging.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/widget/WinMessages.h"
#include "nsArrayEnumerator.h"
#include "nsCRT.h"
#include "nsEnumeratorUtils.h"
#include "nsHashPropertyBag.h"
#include "nsIContentAnalysis.h"
#include "nsIFile.h"
#include "nsISimpleEnumerator.h"
#include "nsCExternalHandlerService.h"
#include "nsIExternalHelperAppService.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsToolkit.h"
#include "nsWindow.h"
#include "WinUtils.h"

#include "mozilla/widget/filedialog/WinFileDialogCommands.h"
#include "mozilla/widget/filedialog/WinFileDialogParent.h"

using mozilla::IsVistaOrLater;
using mozilla::MakeUnique;
using mozilla::LogLevel;
using mozilla::UniquePtr;

using namespace mozilla::widget;

template <typename Res>
using FDPromise = filedialog::Promise<Res>;

constinit UniquePtr<char16_t[], nsFilePicker::FreeDeleter>
    nsFilePicker::sLastUsedUnicodeDirectory;

static const wchar_t kDialogPtrProp[] = L"DialogPtrProperty";
static const DWORD kDialogTimerID = 9999;

#define FILE_BUFFER_SIZE 4096

static const unsigned long kDialogTimerTimeout = 300;

#define MAX_EXTENSION_LENGTH 10

///////////////////////////////////////////////////////////////////////////////
// Helper classes

// Manages the current working path.
class AutoRestoreWorkingPath {
 public:
  AutoRestoreWorkingPath() {
    DWORD bufferLength = GetCurrentDirectoryW(0, nullptr);
    if (!bufferLength) {
      return;
    }

    mWorkingPath = MakeUnique<wchar_t[]>(bufferLength);
    DWORD pathLength = GetCurrentDirectoryW(bufferLength, mWorkingPath.get());
    if (!pathLength || pathLength >= bufferLength) {
      mWorkingPath = nullptr;
    }
  }

  ~AutoRestoreWorkingPath() {
    if (HasWorkingPath()) {
      ::SetCurrentDirectoryW(mWorkingPath.get());
    }
  }

  inline bool HasWorkingPath() const { return mWorkingPath != nullptr; }

 private:
  UniquePtr<wchar_t[]> mWorkingPath;
};

// Manages matching PickerOpen/PickerClosed calls on the parent widget.
class AutoWidgetPickerState {
  static RefPtr<nsWindow> GetWindowForWidget(nsIWidget* aWidget) {
    MOZ_ASSERT(NS_IsMainThread());
    if (!aWidget) {
      return nullptr;
    }
    HWND hwnd = (HWND)aWidget->GetNativeData(NS_NATIVE_WINDOW);
    return RefPtr(WinUtils::GetNSWindowPtr(hwnd));
  }

 public:
  explicit AutoWidgetPickerState(nsIWidget* aWidget)
      : mWindow(GetWindowForWidget(aWidget)) {
    MOZ_ASSERT(mWindow);
    if (mWindow) {
      mWindow->PickerOpen();
    }
  }
  ~AutoWidgetPickerState() {
    // may be null if moved-from
    if (mWindow) {
      mWindow->PickerClosed();
    }
  }

  AutoWidgetPickerState(AutoWidgetPickerState const&) = delete;
  AutoWidgetPickerState(AutoWidgetPickerState&& that) noexcept = default;

 private:
  RefPtr<nsWindow> mWindow;
};

///////////////////////////////////////////////////////////////////////////////
// nsIFilePicker

nsFilePicker::nsFilePicker() : mDlgWnd(nullptr) {}

NS_IMPL_ISUPPORTS(nsFilePicker, nsIFilePicker)

NS_IMETHODIMP nsFilePicker::Init(
    mozilla::dom::BrowsingContext* aBrowsingContext, const nsAString& aTitle,
    nsIFilePicker::Mode aMode, nsISupports* aGlobal) {
  // Don't attempt to open a real file-picker in headless mode.
  if (gfxPlatform::IsHeadless()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return nsBaseFilePicker::Init(aBrowsingContext, aTitle, aMode, aGlobal);
}

/*
 * XP picker callbacks
 */

// Show - Display the file dialog
int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam,
                                LPARAM lpData) {
  if (uMsg == BFFM_INITIALIZED) {
    char16_t* filePath = (char16_t*)lpData;
    if (filePath)
      ::SendMessageW(hwnd, BFFM_SETSELECTIONW,
                     TRUE /* true because lpData is a path string */, lpData);
  }
  return 0;
}

static void EnsureWindowVisible(HWND hwnd) {
  // Obtain the monitor which has the largest area of intersection
  // with the window, or nullptr if there is no intersection.
  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
  if (!monitor) {
    // The window is not visible, we should reposition it to the same place as
    // its parent
    HWND parentHwnd = GetParent(hwnd);
    RECT parentRect;
    GetWindowRect(parentHwnd, &parentRect);
    SetWindowPos(hwnd, nullptr, parentRect.left, parentRect.top, 0, 0,
                 SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
  }
}

// Callback hook which will ensure that the window is visible. Currently
// only in use on os <= XP.
UINT_PTR CALLBACK nsFilePicker::FilePickerHook(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_NOTIFY: {
      LPOFNOTIFYW lpofn = (LPOFNOTIFYW)lParam;
      if (!lpofn || !lpofn->lpOFN) {
        return 0;
      }

      if (CDN_INITDONE == lpofn->hdr.code) {
        // The Window will be automatically moved to the last position after
        // CDN_INITDONE.  We post a message to ensure the window will be visible
        // so it will be done after the automatic last position window move.
        PostMessage(hwnd, MOZ_WM_ENSUREVISIBLE, 0, 0);
      }
    } break;
    case MOZ_WM_ENSUREVISIBLE:
      EnsureWindowVisible(GetParent(hwnd));
      break;
    case WM_INITDIALOG: {
      OPENFILENAMEW* pofn = reinterpret_cast<OPENFILENAMEW*>(lParam);
      SetProp(hwnd, kDialogPtrProp, (HANDLE)pofn->lCustData);
      nsFilePicker* picker = reinterpret_cast<nsFilePicker*>(pofn->lCustData);
      if (picker) {
        picker->SetDialogHandle(hwnd);
        SetTimer(hwnd, kDialogTimerID, kDialogTimerTimeout, nullptr);
      }
    } break;
    case WM_TIMER: {
      // Check to see if our parent has been torn down, if so, we close too.
      if (wParam == kDialogTimerID) {
        nsFilePicker* picker =
            reinterpret_cast<nsFilePicker*>(GetProp(hwnd, kDialogPtrProp));
        if (picker && picker->ClosePickerIfNeeded(true)) {
          KillTimer(hwnd, kDialogTimerID);
        }
      }
    } break;
  }
  return 0;
}

// Callback hook which will dynamically allocate a buffer large enough
// for the file picker dialog.  Currently only in use on  os <= XP.
UINT_PTR CALLBACK nsFilePicker::MultiFilePickerHook(HWND hwnd, UINT msg,
                                                    WPARAM wParam,
                                                    LPARAM lParam) {
  switch (msg) {
    case WM_INITDIALOG: {
      // Finds the child drop down of a File Picker dialog and sets the
      // maximum amount of text it can hold when typed in manually.
      // A wParam of 0 mean 0x7FFFFFFE characters.
      HWND comboBox =
          FindWindowEx(GetParent(hwnd), nullptr, L"ComboBoxEx32", nullptr);
      if (comboBox) SendMessage(comboBox, CB_LIMITTEXT, 0, 0);
      // Store our nsFilePicker ptr for future use
      OPENFILENAMEW* pofn = reinterpret_cast<OPENFILENAMEW*>(lParam);
      SetProp(hwnd, kDialogPtrProp, (HANDLE)pofn->lCustData);
      nsFilePicker* picker =
          reinterpret_cast<nsFilePicker*>(pofn->lCustData);
      if (picker) {
        picker->SetDialogHandle(hwnd);
        SetTimer(hwnd, kDialogTimerID, kDialogTimerTimeout, nullptr);
      }
    } break;
    case WM_NOTIFY: {
      LPOFNOTIFYW lpofn = (LPOFNOTIFYW)lParam;
      if (!lpofn || !lpofn->lpOFN) {
        return 0;
      }
      // CDN_SELCHANGE is sent when the selection in the list box of the file
      // selection dialog changes
      if (lpofn->hdr.code == CDN_SELCHANGE) {
        HWND parentHWND = GetParent(hwnd);

        // Get the required size for the selected files buffer
        UINT newBufLength = 0;
        int requiredBufLength =
            CommDlg_OpenSave_GetSpecW(parentHWND, nullptr, 0);
        if (requiredBufLength >= 0)
          newBufLength += requiredBufLength;
        else
          newBufLength += MAX_PATH;

        // If the user selects multiple files, the buffer contains the
        // current directory followed by the file names of the selected
        // files. So make room for the directory path.  If the user
        // selects a single file, it is no harm to add extra space.
        requiredBufLength =
            CommDlg_OpenSave_GetFolderPathW(parentHWND, nullptr, 0);
        if (requiredBufLength >= 0)
          newBufLength += requiredBufLength;
        else
          newBufLength += MAX_PATH;

        // Check if lpstrFile and nMaxFile are large enough
        if (newBufLength > lpofn->lpOFN->nMaxFile) {
          if (lpofn->lpOFN->lpstrFile) delete[] lpofn->lpOFN->lpstrFile;

          // We allocate FILE_BUFFER_SIZE more bytes than is needed so that
          // if the user selects a file and holds down shift and down to
          // select additional items, we will not continuously reallocate
          newBufLength += FILE_BUFFER_SIZE;

          wchar_t* filesBuffer = new wchar_t[newBufLength];
          ZeroMemory(filesBuffer, newBufLength * sizeof(wchar_t));

          lpofn->lpOFN->lpstrFile = filesBuffer;
          lpofn->lpOFN->nMaxFile = newBufLength;
        }
      }
    } break;
    case WM_TIMER: {
      // Check to see if our parent has been torn down, if so, we close too.
      if (wParam == kDialogTimerID) {
        nsFilePicker* picker =
            reinterpret_cast<nsFilePicker*>(GetProp(hwnd, kDialogPtrProp));
        if (picker && picker->ClosePickerIfNeeded(true)) {
          KillTimer(hwnd, kDialogTimerID);
        }
      }
    } break;
  }

  return FilePickerHook(hwnd, msg, wParam, lParam);
}

/*
 * Close on parent close logic
 */

bool nsFilePicker::ClosePickerIfNeeded(bool aIsXPDialog) {
  if (!mParentWidget || !mDlgWnd) return false;

  nsWindow* win = static_cast<nsWindow*>(mParentWidget.get());
  // Note, the xp callbacks hand us an inner window, so we have to step up
  // one to get the actual dialog.
  HWND dlgWnd;
  if (aIsXPDialog)
    dlgWnd = GetParent(mDlgWnd);
  else
    dlgWnd = mDlgWnd;
  if (IsWindow(dlgWnd) && IsWindowVisible(dlgWnd) && win->DestroyCalled()) {
    wchar_t className[64];
    // Make sure we have the right window
    if (GetClassNameW(dlgWnd, className,std::size(className)) &&
        !wcscmp(className, L"#32770") && DestroyWindow(dlgWnd)) {
      mDlgWnd = nullptr;
      return true;
    }
  }
  return false;
}

void nsFilePicker::SetDialogHandle(HWND aWnd) {
  if (!aWnd || mDlgWnd) return;
  mDlgWnd = aWnd;
}

namespace mozilla::detail {
using Error = mozilla::widget::filedialog::Error;

// Boilerplate for remotely showing a file dialog.
template <typename ActionType,
          typename ReturnType = typename decltype(std::declval<ActionType>()(
              nullptr))::element_type::ResolveValueType>
static auto ShowRemote(ActionType&& action) -> RefPtr<FDPromise<ReturnType>> {
  using RetPromise = FDPromise<ReturnType>;

// "function-local" #define
#define FAIL(where_, why_)                                             \
  return RetPromise::CreateAndReject(MOZ_FD_LOCAL_ERROR(where_, why_), \
                                     __PRETTY_FUNCTION__)

  auto mgr = mozilla::ipc::UtilityProcessManager::GetSingleton();
  if (!mgr) {
    MOZ_ASSERT(false);
    FAIL("ShowRemote: UtilityProcessManager::GetSingleton", E_POINTER);
  }

  auto wfda = mgr->CreateWinFileDialogActor();
  if (!wfda) {
    FAIL("ShowRemote: invocation of CreateWinFileDialogActor", E_POINTER);
  }

  using mozilla::widget::filedialog::sLogFileDialog;

  return wfda->Then(
      mozilla::GetMainThreadSerialEventTarget(),
      "nsFilePicker ShowRemote acquire",
      [action = std::forward<ActionType>(action)](
          filedialog::ProcessProxy p) -> RefPtr<RetPromise> {
        MOZ_LOG(sLogFileDialog, LogLevel::Info,
                ("nsFilePicker ShowRemote first callback: p = [%p]", p.get()));

        // false positive: not actually redundant
        // NOLINTNEXTLINE(readability-redundant-smartptr-get)
        auto promise = action(p.get());
        return promise->Map(
            mozilla::GetMainThreadSerialEventTarget(), __func__,
            [p = std::move(p)](typename RetPromise::ResolveValueType&& val) {
              // explicitly retain the ProcessProxy until at least this point
              return std::move(val);
            });
      },
      [](mozilla::ipc::LaunchError const& error) {
        MOZ_LOG(sLogFileDialog, LogLevel::Error,
                ("could not acquire WinFileDialog: %s:%zu",
                 error.FunctionName().get(), size_t(error.ErrorCode())));
        return RetPromise::CreateAndReject(Error::From(error),
                                           "nsFilePicker::ShowRemote");
      });

#undef FAIL
}

namespace {

static RefPtr<FDPromise<Maybe<filedialog::Results>>> ShowFilePickerRemote(
    HWND parent, filedialog::FileDialogType type,
    nsTArray<filedialog::Command> const& commands, bool needsInputProtection) {
  using mozilla::widget::filedialog::sLogFileDialog;
  return mozilla::detail::ShowRemote([parent, type, needsInputProtection,
                                      commands = commands.Clone()](
                                         filedialog::WinFileDialogParent* p) {
    MOZ_LOG(sLogFileDialog, LogLevel::Info,
            ("%s: p = [%p]", __PRETTY_FUNCTION__, p));
    return p->ShowFileDialogImpl(parent, type, commands, needsInputProtection);
  });
}

static RefPtr<FDPromise<Maybe<nsString>>> ShowFolderPickerRemote(
    HWND parent, nsTArray<filedialog::Command> const& commands,
    bool needsInputProtection) {
  using mozilla::widget::filedialog::sLogFileDialog;
  return mozilla::detail::ShowRemote(
      [parent, needsInputProtection,
       commands = commands.Clone()](filedialog::WinFileDialogParent* p) {
        MOZ_LOG(sLogFileDialog, LogLevel::Info,
                ("%s: p = [%p]", __PRETTY_FUNCTION__, p));
        return p->ShowFolderDialogImpl(parent, commands, needsInputProtection);
      });
}

static RefPtr<FDPromise<Maybe<filedialog::Results>>> ShowFilePickerLocal(
    HWND parent, filedialog::FileDialogType type,
    nsTArray<filedialog::Command> const& commands, bool needsInputProtection) {
  return filedialog::SpawnFilePicker(parent, type, commands.Clone(),
                                     needsInputProtection);
}

static RefPtr<FDPromise<Maybe<nsString>>> ShowFolderPickerLocal(
    HWND parent, nsTArray<filedialog::Command> const& commands,
    bool needsInputProtection) {
  return filedialog::SpawnFolderPicker(parent, commands.Clone(),
                                       needsInputProtection);
}

}  // namespace

// fd_async
//
// Wrapper-namespace for the AsyncExecute() and AsyncAll() functions.
namespace fd_async {

// Implementation details of, specifically, the AsyncExecute() and AsyncAll()
// functions.
namespace details {
// Helper for generically copying ordinary types and nsTArray (which lacks a
// copy constructor) in the same breath.
template <typename T>
static T Copy(T const& val) {
  return val;
}
template <typename T>
static nsTArray<T> Copy(nsTArray<T> const& arr) {
  return arr.Clone();
}

// The possible execution strategies of AsyncExecute.
enum Strategy {
  // Always and only open the dialog in-process. This is effectively the
  // only behavior in older versions of Gecko.
  LocalOnly,

  // Always and only open the dialog out-of-process.
  RemoteOnly,

  // Open the dialog out-of-process. If that fails in any way, try to recover by
  // opening it in-process.
  RemoteWithFallback,

  // Try to open the dialog out-of-process. If and only if the process can't
  // even be created, fall back to in-process.
  //
  // This heuristic is crafted to avoid the out-of-process file-dialog causing
  // user-experience regressions compared to the previous "LocalOnly" behavior:
  //  * If the file-dialog actually crashes, then it would have brought down the
  //    entire browser. In this case just surfacing an error is a strict
  //    improvement.
  //  * If the utility process simply fails to start, there's usually nothing
  //    preventing the dialog from being opened in-process instead. Producing an
  //    error would be a degradation.
  FallbackUnlessCrash,
};

// Decode the relevant preference to determine the desired execution-
// strategy.
static Strategy GetStrategy() {
  int32_t const pref =
      mozilla::StaticPrefs::widget_windows_utility_process_file_picker();
  switch (pref) {
    case -1:
      return LocalOnly;
    case 3:
      return FallbackUnlessCrash;
    case 2:
      return RemoteOnly;
    case 1:
      return RemoteWithFallback;

    default:
      // by default, fall back to local only on non-crash failures
      return FallbackUnlessCrash;
  }
};

template <typename T>
class AsyncAllIterator final {
 public:
  NS_INLINE_DECL_REFCOUNTING(AsyncAllIterator)
  AsyncAllIterator(
      nsTArray<T> aItems,
      std::function<
          RefPtr<mozilla::MozPromise<bool, nsresult, true>>(const T& item)>
          aPredicate,
      RefPtr<mozilla::MozPromise<bool, nsresult, true>::Private> aPromise)
      : mItems(std::move(aItems)),
        mNextIndex(0),
        mPredicate(std::move(aPredicate)),
        mPromise(std::move(aPromise)) {}

  void StartIterating() { ContinueIterating(); }

 private:
  ~AsyncAllIterator() = default;
  void ContinueIterating() {
    if (mNextIndex >= mItems.Length()) {
      mPromise->Resolve(true, __func__);
      return;
    }
    mPredicate(mItems.ElementAt(mNextIndex))
        ->Then(
            mozilla::GetMainThreadSerialEventTarget(), __func__,
            [self = RefPtr{this}](bool aResult) {
              if (!aResult) {
                self->mPromise->Resolve(false, __func__);
                return;
              }
              ++self->mNextIndex;
              self->ContinueIterating();
            },
            [self = RefPtr{this}](nsresult aError) {
              self->mPromise->Reject(aError, __func__);
            });
  }
  nsTArray<T> mItems;
  uint32_t mNextIndex;
  std::function<RefPtr<mozilla::MozPromise<bool, nsresult, true>>(
      const T& item)>
      mPredicate;
  RefPtr<mozilla::MozPromise<bool, nsresult, true>::Private> mPromise;
};

/* N.B.: L and R stand for Local and Remote, not just Left and Right */
template <typename FnL, typename FnR, typename... Args>
struct AsyncExecuteInfo {
  template <typename T>
  using DestructurePromise = widget::filedialog::detail::DestructurePromise<T>;

  using Unit = ::mozilla::Ok;

  using RetL = std::invoke_result_t<FnL, Args...>;
  using RetR = std::invoke_result_t<FnR, Args...>;

  using InfoL = DestructurePromise<RetL>;
  using InfoR = DestructurePromise<RetR>;

  MOZ_ASSERT_SAME_TYPE(
      typename InfoL::ResolveT, typename InfoR::ResolveT,
      "local and remote promises must have identical resolve-types");

  // At present, the local and remote promises have the same type, but this
  // isn't logically necessary. (In particular, a future refactor may remove the
  // redundant `.kind` from the local promises' return types.)
  MOZ_ASSERT_SAME_TYPE(typename InfoL::RejectT, filedialog::Error,
                       "local promise must reject with a filedialog::Error");

  MOZ_ASSERT_SAME_TYPE(typename InfoR::RejectT, filedialog::Error,
                       "remote promise must reject with a filedialog::Error");

  using ResolveT = typename InfoL::ResolveT;
  using PromiseT = MozPromise<ResolveT, filedialog::Error, true>;

  using RetT = RefPtr<PromiseT>;
};

}  // namespace details

// Invoke either or both of a promise-returning "do locally" and "do remotely"
// function with the provided arguments, depending on the relevant preference's
// value and on whether or not the remote version fails (returns a rejection-
// promise).
//
// Both provided functions must return a `RefPtr<filedialog::MozPromise<T>>`. As
// `AsyncExecute` reports failures itself, its rejection-type is `()`.
template <typename Fn1, typename Fn2, typename... Args>
static auto AsyncExecute(Fn1 local, Fn2 remote, Args const&... args) ->
    typename details::AsyncExecuteInfo<Fn1, Fn2, Args...>::RetT {
  using namespace details;
  using Info = AsyncExecuteInfo<Fn1, Fn2, Args...>;

  using ResolveT = typename Info::ResolveT;
  using PromiseT = typename Info::PromiseT;
  using LPromiseT = typename Info::InfoL::Promise;
  using RPromiseT = typename Info::InfoR::Promise;

  constexpr static char kFunctionName[] = "LocalAndOrRemote::AsyncExecute";

  bool (*useLocalFallback)(Error const& err) = [](Error const& err) {
    MOZ_ASSERT_UNREACHABLE("useLocalFallback not set?!");
    return true;
  };

  switch (GetStrategy()) {
    case LocalOnly: {
      return local(args...)->MapErr(
          NS_GetCurrentThread(), __func__, [](Error const& err) {
            MOZ_ASSERT(err.kind == Error::LocalError);
            MOZ_LOG(filedialog::sLogFileDialog, LogLevel::Info,
                    ("local file-dialog failed: where=%s, why=%08" PRIX32,
                     err.where.c_str(), err.why));
            return err;
          });
    }

    case RemoteOnly:
      useLocalFallback = [](Error const&) { return false; };
      break;

    case RemoteWithFallback:
      useLocalFallback = [](Error const&) { return true; };
      break;

    case FallbackUnlessCrash:
      useLocalFallback = [](Error const& err) {
        // All remote crashes are reported as IPCError. The converse isn't
        // necessarily true in theory, but (per telemetry) appears to be true in
        // practice.
        return err.kind != Error::IPCError;
      };
      break;
  }

  return remote(args...)->Then(
      NS_GetCurrentThread(), kFunctionName,
      [](typename RPromiseT::ResolveValueType result) -> RefPtr<PromiseT> {
        // success; stop here
        return PromiseT::CreateAndResolve(std::move(result), kFunctionName);
      },
      // initialized lambda pack captures are C++20 (clang 9, gcc 9);
      // `make_tuple` is just a C++17 workaround
      [=, tuple = std::make_tuple(Copy(args)...)](
          typename RPromiseT::RejectValueType err) mutable -> RefPtr<PromiseT> {
        // failure; record time

        // should we fall back to a local implementation?
        if (!useLocalFallback(err)) {
          // if not, log this failure immediately...
          MOZ_LOG(filedialog::sLogFileDialog, LogLevel::Info,
                  ("remote file-dialog failed: kind=%s, where=%s, "
                   "why=%08" PRIX32,
                   Error::KindName(err.kind), err.where.c_str(), err.why));
          // ... and stop here
          return PromiseT::CreateAndReject(err, kFunctionName);
        }

        // otherwise, retry locally
        auto p0 = std::apply(local, std::move(tuple));
        return p0->Then(
            NS_GetCurrentThread(), kFunctionName,
            [](typename LPromiseT::ResolveOrRejectValue&& val)
                -> RefPtr<PromiseT> {
              using V = typename PromiseT::ResolveOrRejectValue;
              return PromiseT::CreateAndResolveOrReject(
                  val.IsResolve()
                      ? V::MakeResolve(std::move(val).ResolveValue())
                      : V::MakeReject(val.RejectValue()),
                  kFunctionName);
            });
      });
}

}  // namespace fd_async

using fd_async::AsyncExecute;

}  // namespace mozilla::detail

// Open the older XP style folder picker dialog. We end up in this call
// on XP systems or when the modern dialog cannot be initialized.
bool nsFilePicker::ShowXPFolderPicker(const nsString& aInitialDir) {
  bool result = false;

  auto dirBuffer = MakeUnique<wchar_t[]>(FILE_BUFFER_SIZE);
  wcsncpy(dirBuffer.get(), aInitialDir.get(), FILE_BUFFER_SIZE);
  dirBuffer[FILE_BUFFER_SIZE - 1] = '\0';

  ScopedRtlShimWindow shim(mParentWidget.get());
  AutoWidgetPickerState awps(mParentWidget);

  BROWSEINFOW browserInfo = {0};
  browserInfo.pidlRoot = nullptr;
  browserInfo.pszDisplayName = dirBuffer.get();
  browserInfo.lpszTitle = mTitle.get();
  browserInfo.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
  browserInfo.hwndOwner = shim.get();
  browserInfo.iImage = 0;
  browserInfo.lParam = reinterpret_cast<LPARAM>(this);

  if (!aInitialDir.IsEmpty()) {
    // the dialog is modal so that |initialDir.get()| will be valid in
    // BrowserCallbackProc. Thus, we don't need to clone it.
    browserInfo.lParam = (LPARAM)aInitialDir.get();
    browserInfo.lpfn = &BrowseCallbackProc;
  } else {
    browserInfo.lParam = 0;
    browserInfo.lpfn = nullptr;
  }

  LPITEMIDLIST list = ::SHBrowseForFolderW(&browserInfo);
  if (list) {
    result = ::SHGetPathFromIDListW(list, dirBuffer.get());
    if (result)
      mUnicodeFile.Assign(static_cast<const wchar_t*>(dirBuffer.get()));
    // free PIDL
    CoTaskMemFree(list);
  }

  return result;
}

/*
 * Folder picker invocation
 */

/*
 * Show a folder picker.
 *
 * @param aInitialDir   The initial directory. The last-used directory will be
 *                      used if left blank.
 * @return  A promise which:
 *          - resolves to true if a file was selected successfully (in which
 *            case mUnicodeFile will be updated);
 *          - resolves to false if the dialog was cancelled by the user;
 *          - is rejected with the associated HRESULT if some error occurred.
 */
RefPtr<mozilla::MozPromise<bool, nsFilePicker::Error, true>>
nsFilePicker::ShowFolderPicker(const nsString& aInitialDir) {
  using Promise = mozilla::MozPromise<bool, Error, true>;
  constexpr static auto Ok = [](bool val) {
    return Promise::CreateAndResolve(val, "nsFilePicker::ShowFolderPicker");
  };

  if (!IsVistaOrLater()) {
    return Ok(ShowXPFolderPicker(aInitialDir));
  }

  namespace fd = ::mozilla::widget::filedialog;
  nsTArray<fd::Command> commands = {
      fd::SetOptions(FOS_PICKFOLDERS),
      fd::SetTitle(mTitle),
  };

  if (!mOkButtonLabel.IsEmpty()) {
    commands.AppendElement(fd::SetOkButtonLabel(mOkButtonLabel));
  }

  if (!aInitialDir.IsEmpty()) {
    commands.AppendElement(fd::SetFolder(aInitialDir));
  }

  ScopedRtlShimWindow shim(mParentWidget.get());
  AutoWidgetPickerState awps(mParentWidget);

  return mozilla::detail::AsyncExecute(&mozilla::detail::ShowFolderPickerLocal,
                                       &mozilla::detail::ShowFolderPickerRemote,
                                       shim.get(), commands,
                                       IsContentInitiated())
      ->Then(NS_GetCurrentThread(), __PRETTY_FUNCTION__,
             [self = RefPtr(this), shim = std::move(shim),
              awps = std::move(awps)](Maybe<nsString> val) {
               if (val) {
                 self->mUnicodeFile = val.extract();
                 return Ok(true);
               }
               return Ok(false);
             },
             [self = RefPtr(this), initialDir = nsString(aInitialDir)](Error const&) {
               return Ok(self->ShowXPFolderPicker(initialDir));
             });
}

/*
 * File open and save picker invocation
 */

/* static */
bool nsFilePicker::GetFileNameWrapper(OPENFILENAMEW* ofn, PickerType aType) {
  MOZ_SEH_TRY {
    if (aType == PICKER_TYPE_OPEN)
      return ::GetOpenFileNameW(ofn);
    else if (aType == PICKER_TYPE_SAVE)
      return ::GetSaveFileNameW(ofn);
  }
  MOZ_SEH_EXCEPT(true) {
    NS_ERROR(
        "nsFilePicker GetFileName win32 call generated an exception! This is "
        "bad!");
  }
  return false;
}

bool nsFilePicker::FilePickerWrapper(OPENFILENAMEW* ofn, PickerType aType) {
  if (!ofn) return false;
  AutoWidgetPickerState awps(mParentWidget);
  mDlgWnd = nullptr;
  bool result = GetFileNameWrapper(ofn, aType);
  mDlgWnd = nullptr;
  return result;
}

bool nsFilePicker::ShowXPFilePicker(const nsString& aInitialDir) {
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  nsString filterBuffer = mFilterListXP;

  auto fileBuffer = MakeUnique<wchar_t[]>(FILE_BUFFER_SIZE);
  wcsncpy(fileBuffer.get(), mDefaultFilePath.get(), FILE_BUFFER_SIZE);
  fileBuffer[FILE_BUFFER_SIZE - 1] = '\0';  // null terminate if copy truncated

  if (!aInitialDir.IsEmpty()) {
    ofn.lpstrInitialDir = aInitialDir.get();
  }

  ScopedRtlShimWindow shim(mParentWidget.get());

  ofn.lpstrTitle = (LPCWSTR)mTitle.get();
  ofn.lpstrFilter = (LPCWSTR)filterBuffer.get();
  ofn.nFilterIndex = mSelectedType;
  ofn.lpstrFile = fileBuffer.get();
  ofn.nMaxFile = FILE_BUFFER_SIZE;
  ofn.hwndOwner = shim.get();
  ofn.lCustData = reinterpret_cast<LPARAM>(this);
  ofn.Flags = OFN_SHAREAWARE | OFN_LONGNAMES | OFN_OVERWRITEPROMPT |
              OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_ENABLESIZING |
              OFN_EXPLORER;

  // Windows Vista and up won't allow you to use the new looking dialogs with
  // a hook procedure.  The hook procedure fixes a problem on XP dialogs for
  // file picker visibility.  Vista and up automatically ensures the file
  // picker is always visible.
  if (!IsVistaOrLater()) {
    ofn.lpfnHook = FilePickerHook;
    ofn.Flags |= OFN_ENABLEHOOK;
  }

  // Handle add to recent docs settings
  if (IsPrivacyModeEnabled() || !mAddToRecentDocs) {
    ofn.Flags |= OFN_DONTADDTORECENT;
  }

  static const wchar_t htmExt[] = L"html";

  if (!mDefaultExtension.IsEmpty()) {
    ofn.lpstrDefExt = mDefaultExtension.get();
  } else if (IsDefaultPathHtml()) {
    // Get file extension from suggested filename to detect if we are
    // saving an html file.
    // This is supposed to append ".htm" if user doesn't supply an
    // extension but the behavior is sort of weird:
    // - Often appends ".html" even if you have an extension
    // - It obeys your extension if you put quotes around name
    ofn.lpstrDefExt = htmExt;
  }

  // When possible, instead of using OFN_NOCHANGEDIR to ensure the current
  // working directory will not change from this call, we will retrieve the
  // current working directory before the call and restore it after the
  // call.  This flag causes problems on Windows XP for paths that are
  // selected like C:test.txt where the user is currently at C:\somepath
  // In which case expected result should be C:\somepath\test.txt
  AutoRestoreWorkingPath restoreWorkingPath;
  // If we can't get the current working directory, the best case is to
  // use the OFN_NOCHANGEDIR flag
  if (!restoreWorkingPath.HasWorkingPath()) {
    ofn.Flags |= OFN_NOCHANGEDIR;
  }

  bool result = false;

  switch (mMode) {
    case modeOpen:
      // FILE MUST EXIST!
      ofn.Flags |= OFN_FILEMUSTEXIST;
      result = FilePickerWrapper(&ofn, PICKER_TYPE_OPEN);
      break;

    case modeOpenMultiple:
      ofn.Flags |= OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;

      // The hook set here ensures that the buffer returned will always be
      // large enough to hold all selected files.  The hook may modify the
      // value of ofn.lpstrFile and deallocate the old buffer that it pointed
      // to (fileBuffer). The hook assumes that the passed in value is heap
      // allocated and that the returned value should be freed by the caller.
      // If the hook changes the buffer, it will deallocate the old buffer.
      // This fix would be nice to have in Vista and up, but it would force
      // the file picker to use the old style dialogs because hooks are not
      // allowed in the new file picker UI.  We need to eventually move to
      // the new Common File Dialogs for Vista and up.
      if (!IsVistaOrLater()) {
        ofn.lpfnHook = MultiFilePickerHook;
        fileBuffer.release();
        result = FilePickerWrapper(&ofn, PICKER_TYPE_OPEN);
        fileBuffer.reset(ofn.lpstrFile);
      } else {
        result = FilePickerWrapper(&ofn, PICKER_TYPE_OPEN);
        if (!result && CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
          UINT newBufLength = *reinterpret_cast<WORD*>(fileBuffer.get());
          if (newBufLength > ofn.nMaxFile) {
            fileBuffer = MakeUnique<wchar_t[]>(newBufLength);
            ZeroMemory(fileBuffer.get(), newBufLength * sizeof(wchar_t));
            ofn.lpstrFile = fileBuffer.get();
            ofn.nMaxFile = newBufLength;
            result = FilePickerWrapper(&ofn, PICKER_TYPE_OPEN);
          }
        }
      }
      break;

    case modeSave: {
      ofn.Flags |= OFN_NOREADONLYRETURN;

      // Don't follow shortcuts when saving a shortcut, this can be used
      // to trick users (bug 271732)
      if (IsDefaultPathLink()) ofn.Flags |= OFN_NODEREFERENCELINKS;

      result = FilePickerWrapper(&ofn, PICKER_TYPE_SAVE);
      if (!result) {
        // Error, find out what kind.
        if (CommDlgExtendedError() == FNERR_INVALIDFILENAME) {
          // Probably the default file name is too long or contains illegal
          // characters. Try again, without a starting file name.
          ofn.lpstrFile[0] = L'\0';
          result = FilePickerWrapper(&ofn, PICKER_TYPE_SAVE);
        }
      }
    } break;

    default:
      // NS_NOTREACHED("unsupported file picker mode");
      return false;
  }

  if (!result) return false;

  // Remember what filter type the user selected
  mSelectedType = (int16_t)ofn.nFilterIndex;

  // Single file selection, we're done
  if (mMode != modeOpenMultiple) {
    GetQualifiedPath(fileBuffer.get(), mUnicodeFile);
    return true;
  }

  // Set user-selected location of file or directory.  From msdn's "Open and
  // Save As Dialog Boxes" section:
  // If you specify OFN_EXPLORER, the directory and file name strings are '\0'
  // separated, with an extra '\0' character after the last file name. This
  // format enables the Explorer-style dialog boxes to return long file names
  // that include spaces.
  wchar_t* current = fileBuffer.get();

  nsAutoString dirName(current);
  // Sometimes dirName contains a trailing slash and sometimes it doesn't:
  if (current[dirName.Length() - 1] != '\\') dirName.Append((char16_t)'\\');

  while (current && *current && *(current + wcslen(current) + 1)) {
    current = current + wcslen(current) + 1;

    // Only prepend the directory if the path specified is a relative path
    nsAutoString path;
    if (PathIsRelativeW(current)) {
      path = dirName + nsDependentString(current);
    } else {
      path = current;
    }

    nsAutoString canonicalizedPath;
    GetQualifiedPath(path.get(), canonicalizedPath);
    nsCOMPtr<nsIFile> file;
    if (NS_FAILED(NS_NewLocalFile(canonicalizedPath, getter_AddRefs(file))) ||
        !mFiles.AppendObject(file))
      return false;
  }

  // Handle the case where the user selected just one file. From msdn: If you
  // specify OFN_ALLOWMULTISELECT and the user selects only one file the
  // lpstrFile string does not have a separator between the path and file name.
  if (current && *current && (current == fileBuffer.get())) {
    nsAutoString canonicalizedPath;
    GetQualifiedPath(current, canonicalizedPath);
    nsCOMPtr<nsIFile> file;
    if (NS_FAILED(NS_NewLocalFile(canonicalizedPath, getter_AddRefs(file))) ||
        !mFiles.AppendObject(file))
      return false;
  }

  return true;
}

/*
 * Show a file picker.
 *
 * @param aInitialDir   The initial directory. The last-used directory will be
 *                      used if left blank.
 * @return  A promise which:
 *          - resolves to true if one or more files were selected successfully
 *            (in which case mUnicodeFile and/or mFiles will be updated);
 *          - resolves to false if the dialog was cancelled by the user;
 *          - is rejected with the associated HRESULT if some error occurred.
 */
RefPtr<mozilla::MozPromise<bool, nsFilePicker::Error, true>>
nsFilePicker::ShowFilePicker(const nsString& aInitialDir) {
  AUTO_PROFILER_LABEL("nsFilePicker::ShowFilePicker", OTHER);

  using Promise = mozilla::MozPromise<bool, Error, true>;
  constexpr static auto Ok = [](bool val) {
    return Promise::CreateAndResolve(val, "nsFilePicker::ShowFilePicker");
  };
  constexpr static auto NotOk = [](Error error) -> RefPtr<Promise> {
    return Promise::CreateAndReject(std::move(error),
                                    "nsFilePicker::ShowFilePicker");
  };

  if (!IsVistaOrLater()) {
    return Ok(ShowXPFilePicker(aInitialDir));
  }

  namespace fd = ::mozilla::widget::filedialog;
  nsTArray<fd::Command> commands;
  // options
  {
    FILEOPENDIALOGOPTIONS fos = 0;

    // FOS_OVERWRITEPROMPT: always confirm on overwrite in Save dialogs
    // FOS_FORCEFILESYSTEM: provide only filesystem-objects, not more exotic
    //    entities like libraries
    fos |= FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM;

    // Handle add to recent docs settings
    if (IsPrivacyModeEnabled() || !mAddToRecentDocs) {
      fos |= FOS_DONTADDTORECENT;
    }

    // mode specification
    switch (mMode) {
      case modeOpenMultiple:
        fos |= FOS_ALLOWMULTISELECT;
        [[fallthrough]];

      case modeOpen:
        fos |= FOS_FILEMUSTEXIST;
        switch (mozilla::StaticPrefs::
                    widget_windows_follow_shortcuts_on_file_open()) {
          case 1:
            break;
          default:
            fos |= FOS_NODEREFERENCELINKS;
        }
        break;

      case modeSave:
        fos |= FOS_NOREADONLYRETURN;
        // Don't follow shortcuts when saving a shortcut; this can be used to
        // trick users (bug 271732). _Do_ follow shortcuts when not saving a
        // shortcut (bug 283730).
        if (IsDefaultPathLink()) {
          fos |= FOS_NODEREFERENCELINKS;
        }
        break;

      case modeGetFolder:
        MOZ_ASSERT(false, "file-picker opened in directory-picker mode");
        return NotOk(MOZ_FD_LOCAL_ERROR(
            "file-picker opened in directory-picker mode", E_INVALIDARG));
    }

    commands.AppendElement(fd::SetOptions(fos));
  }

  // initial strings

  // title
  commands.AppendElement(fd::SetTitle(mTitle));

  // default filename
  if (!mDefaultFilename.IsEmpty()) {
    // Prevent the shell from expanding environment variables by removing the %
    // characters that are used to delimit them.
    //
    // Note that we do _not_ need to preserve this sanitization for the fallback
    // case where the file dialog fails. Variable-expansion only occurs in the
    // file dialog specifically, and not when creating a file directly via other
    // means.
    nsAutoString sanitizedFilename(mDefaultFilename);
    sanitizedFilename.ReplaceChar('%', '_');

    commands.AppendElement(fd::SetFileName(sanitizedFilename));
  }

  // default extension to append to new files
  if (!mDefaultExtension.IsEmpty()) {
    // We don't want environment variables expanded in the extension either.
    nsAutoString sanitizedExtension(mDefaultExtension);
    sanitizedExtension.ReplaceChar('%', '_');

    commands.AppendElement(fd::SetDefaultExtension(sanitizedExtension));
  } else if (IsDefaultPathHtml()) {
    commands.AppendElement(fd::SetDefaultExtension(u"html"_ns));
  }

  // initial location
  if (!aInitialDir.IsEmpty()) {
    commands.AppendElement(fd::SetFolder(aInitialDir));
  }

  // filter types and the default index
  if (!mFilterList.IsEmpty()) {
    nsTArray<fd::ComDlgFilterSpec> fileTypes;
    for (auto const& filter : mFilterList) {
      fileTypes.EmplaceBack(filter.title, filter.filter);
    }
    commands.AppendElement(fd::SetFileTypes(std::move(fileTypes)));
    commands.AppendElement(fd::SetFileTypeIndex(mSelectedType));
  }

  ScopedRtlShimWindow shim(mParentWidget.get());
  AutoWidgetPickerState awps(mParentWidget);

  mozilla::BackgroundHangMonitor().NotifyWait();
  auto type = mMode == modeSave ? FileDialogType::Save : FileDialogType::Open;

  auto promise = mozilla::detail::AsyncExecute(
      &mozilla::detail::ShowFilePickerLocal,
      &mozilla::detail::ShowFilePickerRemote, shim.get(), type, commands,
      IsContentInitiated());

  return promise->Then(
      mozilla::GetMainThreadSerialEventTarget(), __PRETTY_FUNCTION__,
      [self = RefPtr(this), mode = mMode, shim = std::move(shim),
       awps = std::move(awps)](Maybe<Results> res_opt) {
        if (!res_opt) {
          return Ok(false);  // operation cancelled by user
        }
        auto result = res_opt.extract();

        // Remember what filter type the user selected
        self->mSelectedType = int32_t(result.selectedFileTypeIndex());

        auto const& paths = result.paths();

        // single selection
        if (mode != modeOpenMultiple) {
          if (!paths.IsEmpty()) {
            MOZ_ASSERT(paths.Length() == 1);
            self->mUnicodeFile = paths[0];
            return Ok(true);
          }
          return Ok(false);
        }

        // multiple selection
        for (auto const& str : paths) {
          nsCOMPtr<nsIFile> file;
          if (NS_SUCCEEDED(NS_NewLocalFile(str, getter_AddRefs(file)))) {
            self->mFiles.AppendObject(file);
          }
        }

        return Ok(true);
      },
      [self = RefPtr(this), initialDir = nsString(aInitialDir)](Error const&) {
        return Ok(self->ShowXPFilePicker(initialDir));
      });
}

void nsFilePicker::ClearFiles() {
  mUnicodeFile.Truncate();
  mFiles.Clear();
}

RefPtr<nsFilePicker::ContentAnalysisResponse>
nsFilePicker::CheckContentAnalysisService() {
  nsresult rv;
  nsCOMPtr<nsIContentAnalysis> contentAnalysis =
      mozilla::components::nsIContentAnalysis::Service(&rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsFilePicker::ContentAnalysisResponse::CreateAndReject(rv, __func__);
  }
  bool contentAnalysisIsActive = false;
  rv = contentAnalysis->GetIsActive(&contentAnalysisIsActive);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsFilePicker::ContentAnalysisResponse::CreateAndReject(rv, __func__);
  }
  if (!contentAnalysisIsActive ||
      !mozilla::StaticPrefs::
          browser_contentanalysis_interception_point_file_upload_enabled()) {
    nsCOMArray<nsIFile> files;
    if (mMode == modeGetFolder || !mUnicodeFile.IsEmpty()) {
      RefPtr<nsIFile> folderOrFile;
      nsresult rv = GetFile(getter_AddRefs(folderOrFile));
      if (NS_WARN_IF(NS_FAILED(rv) || !folderOrFile)) {
        return nsFilePicker::ContentAnalysisResponse::CreateAndReject(rv,
                                                                      __func__);
      }
      files.AppendElement(folderOrFile);
    } else {
      // multiple selections
      files.AppendElements(mFiles);
    }
    return nsFilePicker::ContentAnalysisResponse::CreateAndResolve(
        std::move(files), __func__);
  }

  // Entries may be files or folders.  Folder contents will be recursively
  // checked.
  nsCOMArray<nsIFile> files;
  if (mMode == modeGetFolder || !mUnicodeFile.IsEmpty()) {
    nsCOMPtr<nsIFile> folderOrFile;
    nsresult rv = GetFile(getter_AddRefs(folderOrFile));
    if (NS_WARN_IF(NS_FAILED(rv) || !folderOrFile)) {
      return nsFilePicker::ContentAnalysisResponse::CreateAndReject(rv,
                                                                    __func__);
    }
    files.AppendElement(folderOrFile);
  } else {
    // multiple selections
    files.AppendElements(mFiles);
  }
  MOZ_ASSERT(!files.IsEmpty());
  auto* windowGlobal = mBrowsingContext->Canonical()->GetCurrentWindowGlobal();
  NS_ENSURE_TRUE(
      windowGlobal,
      nsFilePicker::ContentAnalysisResponse::CreateAndReject(rv, __func__));
  // This will check all of the files even if BLOCK responses are received for
  // some of them, and the promise will resolve with the files that are ALLOWed.
  return mozilla::contentanalysis::ContentAnalysis::CheckUploadsInBatchMode(
      std::move(files), /* aAutoAcknowledge */ true, windowGlobal,
      nsIContentAnalysisRequest::Reason::eFilePickerDialog);
};

///////////////////////////////////////////////////////////////////////////////
// nsIFilePicker impl.

nsresult nsFilePicker::Open(nsIFilePickerShownCallback* aCallback) {
  NS_ENSURE_ARG_POINTER(aCallback);

  if (MaybeBlockFilePicker(aCallback)) {
    return NS_OK;
  }

  // Don't attempt to open a real file-picker in headless mode.
  if (gfxPlatform::IsHeadless()) {
    return nsresult::NS_ERROR_NOT_AVAILABLE;
  }

  nsAutoString initialDir;
  if (mDisplayDirectory) {
    mDisplayDirectory->GetPath(initialDir);
  }

  // If no display directory, re-use the last one.
  if (initialDir.IsEmpty()) {
    // Allocate copy of last used dir.
    initialDir = sLastUsedUnicodeDirectory.get();
  }

  // Clear previous file selections
  ClearFiles();

  auto promise = mMode == modeGetFolder ? ShowFolderPicker(initialDir)
                                        : ShowFilePicker(initialDir);

  promise->Then(
      mozilla::GetMainThreadSerialEventTarget(), __PRETTY_FUNCTION__,
      [self = RefPtr(this),
       callback = RefPtr(aCallback)](bool selectionMade) -> void {
        if (!selectionMade) {
          callback->Done(ResultCode::returnCancel);
          return;
        }

        self->RememberLastUsedDirectory();

        nsIFilePicker::ResultCode retValue = ResultCode::returnOK;

        if (self->mMode == modeSave) {
          // Windows does not return resultReplace; we must check whether the
          // file already exists.
          nsCOMPtr<nsIFile> file;
          nsresult rv =
              NS_NewLocalFile(self->mUnicodeFile, getter_AddRefs(file));

          bool flag = false;
          if (NS_SUCCEEDED(rv) && NS_SUCCEEDED(file->Exists(&flag)) && flag) {
            retValue = ResultCode::returnReplace;
          }
        }

        if (self->mBrowsingContext && !self->mBrowsingContext->IsChrome() &&
            self->mMode != modeSave && retValue != ResultCode::returnCancel) {
          self->CheckContentAnalysisService()->Then(
              mozilla::GetMainThreadSerialEventTarget(), __func__,
              [retValue, callback,
               self = RefPtr{self}](nsCOMArray<nsIFile> aAllowedFiles) {
                if (aAllowedFiles.IsEmpty()) {
                  self->ClearFiles();
                  callback->Done(ResultCode::returnCancel);
                }
                if (self->mMode == modeGetFolder ||
                    !self->mUnicodeFile.IsEmpty()) {
                  //  Callers will use mUnicodeFile, so just call the callback
                  callback->Done(retValue);
                  return;
                }
                // Only the files in aAllowedFiles should be returned, so
                // swap these into mFiles.
                self->ClearFiles();
                aAllowedFiles.SwapElements(self->mFiles);
                callback->Done(retValue);
              },
              [callback, self = RefPtr{self}](nsresult aError) {
                self->ClearFiles();
                callback->Done(ResultCode::returnCancel);
              });
          return;
        }

        callback->Done(retValue);
      },
      [callback = RefPtr(aCallback), self = RefPtr{this}](Error const& err) {
        // The file-dialog process (probably) crashed. Report this fact to the
        // user, and try to recover with a fallback rather than discarding the
        // file.
        //
        // (Note that at this point, logging of the crash -- and possibly also a
        // telemetry ping -- has already occurred.)
        ResultCode resultCode = ResultCode::returnCancel;

        // This does not describe the original error, just the error when trying
        // to select a fallback location -- no such attempt means no such error.
        FallbackResult fallback{nullptr};

        if (self->mMode == Mode::modeSave) {
          fallback = self->ComputeFallbackSavePath();
          // don't set sLastUsedUnicodeDirectory here: the user didn't
          // actually select anything
        }

        self->SendFailureNotification(resultCode, err, std::move(fallback));
        callback->Done(resultCode);
      });

  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetFile(nsIFile** aFile) {
  NS_ENSURE_ARG_POINTER(aFile);
  *aFile = nullptr;

  if (mUnicodeFile.IsEmpty()) {
    return NS_OK;
  }

  nsCOMPtr<nsIFile> file;
  nsresult rv = NS_NewLocalFile(mUnicodeFile, getter_AddRefs(file));
  if (NS_FAILED(rv)) {
    return rv;
  }

  file.forget(aFile);
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetFileURL(nsIURI** aFileURL) {
  *aFileURL = nullptr;
  nsCOMPtr<nsIFile> file;
  nsresult rv = GetFile(getter_AddRefs(file));
  if (!file) {
    return rv;
  }

  return NS_NewFileURI(aFileURL, file);
}

NS_IMETHODIMP
nsFilePicker::GetFiles(nsISimpleEnumerator** aFiles) {
  NS_ENSURE_ARG_POINTER(aFiles);
  return NS_NewArrayEnumerator(aFiles, mFiles, NS_GET_IID(nsIFile));
}

// Get the file + path
NS_IMETHODIMP
nsBaseWinFilePicker::SetDefaultString(const nsAString& aString) {
  mDefaultFilePath = aString;

  // First, make sure the file name is not too long.
  int32_t nameLength;
  int32_t nameIndex = mDefaultFilePath.RFind(u"\\");
  if (nameIndex == kNotFound) {
    nameIndex = 0;
  } else {
    nameIndex++;
  }
  nameLength = mDefaultFilePath.Length() - nameIndex;
  mDefaultFilename.Assign(Substring(mDefaultFilePath, nameIndex));

  if (nameLength > MAX_PATH) {
    int32_t extIndex = mDefaultFilePath.RFind(u".");
    if (extIndex == kNotFound) {
      extIndex = mDefaultFilePath.Length();
    }

    // Let's try to shave the needed characters from the name part.
    int32_t charsToRemove = nameLength - MAX_PATH;
    if (extIndex - nameIndex >= charsToRemove) {
      mDefaultFilePath.Cut(extIndex - charsToRemove, charsToRemove);
    }
  }

  // Then, we need to replace illegal characters. At this stage, we cannot
  // replace the backslash as the string might represent a file path.
  mDefaultFilePath.ReplaceChar(u"" FILE_ILLEGAL_CHARACTERS, u'-');
  mDefaultFilename.ReplaceChar(u"" FILE_ILLEGAL_CHARACTERS, u'-');

  return NS_OK;
}

NS_IMETHODIMP
nsBaseWinFilePicker::GetDefaultString(nsAString& aString) {
  return NS_ERROR_FAILURE;
}

// The default extension to use for files
NS_IMETHODIMP
nsBaseWinFilePicker::GetDefaultExtension(nsAString& aExtension) {
  aExtension = mDefaultExtension;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseWinFilePicker::SetDefaultExtension(const nsAString& aExtension) {
  mDefaultExtension = aExtension;
  return NS_OK;
}

// Set the filter index
NS_IMETHODIMP
nsFilePicker::GetFilterIndex(int32_t* aFilterIndex) {
  // Windows' filter index is 1-based, we use a 0-based system.
  *aFilterIndex = mSelectedType - 1;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::SetFilterIndex(int32_t aFilterIndex) {
  // Windows' filter index is 1-based, we use a 0-based system.
  mSelectedType = aFilterIndex + 1;
  return NS_OK;
}

void nsFilePicker::InitNative(nsIWidget* aParent, const nsAString& aTitle) {
  mParentWidget = aParent;
  mTitle.Assign(aTitle);
}

void nsFilePicker::GetQualifiedPath(const wchar_t* aInPath,
                                    nsString& aOutPath) {
  // Prefer a qualified path over a non qualified path.
  // Things like c:file.txt would be accepted in Win XP but would later
  // fail to open from the download manager.
  wchar_t qualifiedFileBuffer[MAX_PATH];
  if (PathSearchAndQualifyW(aInPath, qualifiedFileBuffer, MAX_PATH)) {
    aOutPath.Assign(qualifiedFileBuffer);
  } else {
    aOutPath.Assign(aInPath);
  }
}

void nsFilePicker::AppendXPFilter(const nsAString& aTitle,
                                  const nsAString& aFilter) {
  mFilterListXP.Append(aTitle);
  mFilterListXP.Append(char16_t('\0'));
  mFilterListXP.Append(aFilter);
  mFilterListXP.Append(char16_t('\0'));
}

NS_IMETHODIMP
nsFilePicker::AppendFilter(const nsAString& aTitle, const nsAString& aFilter) {
  nsString sanitizedFilter(aFilter);
  sanitizedFilter.ReplaceChar('%', '_');

  if (sanitizedFilter == u"..apps"_ns) {
    sanitizedFilter = u"*.exe;*.com"_ns;
  } else {
    sanitizedFilter.StripWhitespace();
    if (sanitizedFilter == u"*"_ns) {
      sanitizedFilter = u"*.*"_ns;
    }
  }
  mFilterList.AppendElement(
      Filter{.title = nsString(aTitle), .filter = sanitizedFilter});

  AppendXPFilter(aTitle, sanitizedFilter);
  return NS_OK;
}

void nsFilePicker::RememberLastUsedDirectory() {
  if (IsPrivacyModeEnabled()) {
    // Don't remember the directory if private browsing was in effect
    return;
  }

  nsCOMPtr<nsIFile> file;
  if (NS_FAILED(NS_NewLocalFile(mUnicodeFile, getter_AddRefs(file)))) {
    NS_WARNING("RememberLastUsedDirectory failed to init file path.");
    return;
  }

  nsCOMPtr<nsIFile> dir;
  nsAutoString newDir;
  if (NS_FAILED(file->GetParent(getter_AddRefs(dir))) ||
      !(mDisplayDirectory = dir) ||
      NS_FAILED(mDisplayDirectory->GetPath(newDir)) || newDir.IsEmpty()) {
    NS_WARNING("RememberLastUsedDirectory failed to get parent directory.");
    return;
  }

  sLastUsedUnicodeDirectory.reset(ToNewUnicode(newDir));
}

bool nsFilePicker::IsPrivacyModeEnabled() {
  return mBrowsingContext && mBrowsingContext->UsePrivateBrowsing();
}

bool nsFilePicker::IsDefaultPathLink() {
  NS_ConvertUTF16toUTF8 ext(mDefaultFilePath);
  ext.Trim(" .", false, true);  // watch out for trailing space and dots
  ToLowerCase(ext);
  return StringEndsWith(ext, ".lnk"_ns) || StringEndsWith(ext, ".pif"_ns) ||
         StringEndsWith(ext, ".url"_ns);
}

bool nsFilePicker::IsDefaultPathHtml() {
  int32_t extIndex = mDefaultFilePath.RFind(u".");
  if (extIndex >= 0) {
    nsAutoString ext;
    mDefaultFilePath.Right(ext, mDefaultFilePath.Length() - extIndex);
    if (ext.LowerCaseEqualsLiteral(".htm") ||
        ext.LowerCaseEqualsLiteral(".html") ||
        ext.LowerCaseEqualsLiteral(".shtml")) {
      return true;
    }
  }
  return false;
}

auto nsFilePicker::ComputeFallbackSavePath() const -> FallbackResult {
  using mozilla::Err;

  // we shouldn't even be here if we're not trying to save
  if (mMode != Mode::modeSave) {
    return Err(NS_ERROR_FAILURE);
  }

  // get a fallback download-location
  RefPtr<nsIFile> location;
  {
    // try to query the helper service for the preferred downloads directory
    nsresult rv;
    nsCOMPtr<nsIExternalHelperAppService> svc =
        do_GetService(NS_EXTERNALHELPERAPPSERVICE_CONTRACTID, &rv);
    MOZ_TRY(rv);

    MOZ_TRY(svc->GetPreferredDownloadsDirectory(getter_AddRefs(location)));
  }
  MOZ_ASSERT(location);

  constexpr static const auto EndsWithExtension =
      [](nsAString const& path, nsAString const& extension) -> bool {
    size_t const len = path.Length();
    size_t const extLen = extension.Length();
    if (extLen + 2 > len) {
      // `path` is too short and can't possibly end with `extension`. (Note that
      // we consider, _e.g._, ".jpg" not to end with the extension "jpg".)
      return false;
    }
    if (path[len - extLen - 1] == L'.' &&
        StringTail(path, extLen) == extension) {
      return true;
    }
    return false;
  };

  nsString filename(mDefaultFilename);
  if (!mDefaultExtension.IsEmpty() &&
      !EndsWithExtension(filename, mDefaultExtension)) {
    filename.AppendLiteral(".");
    filename.Append(mDefaultExtension);
  }

  MOZ_TRY(location->Append(filename));
  MOZ_TRY(location->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600));
  return location;
}

void nsFilePicker::SendFailureNotification(nsFilePicker::ResultCode aResult,
                                           Error error,
                                           FallbackResult aFallback) const {
  if (MOZ_LOG_TEST(filedialog::sLogFileDialog, LogLevel::Info)) {
    nsString msg;
    if (aFallback.isOk()) {
      nsString path;
      aFallback.inspect()->GetPath(path);
      msg = u"path: "_ns;
      msg.Append(path);
    } else {
      msg.AppendPrintf("err: 0x%08" PRIX32, (uint32_t)aFallback.inspectErr());
    }
    MOZ_LOG(filedialog::sLogFileDialog, LogLevel::Info,
            ("SendCrashNotification: %" PRIX16 ", %ls", aResult,
             static_cast<wchar_t const*>(msg.get())));
  }

  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  if (!obsSvc) {
    return;  // normal during XPCOM shutdown
  }

  auto props = mozilla::MakeRefPtr<nsHashPropertyBag>();
  props->SetPropertyAsInterface(u"ctx"_ns, mBrowsingContext);
  props->SetPropertyAsUint32(u"mode"_ns, mMode);
  if (aFallback.isOk()) {
    props->SetPropertyAsInterface(u"file"_ns, aFallback.unwrap().get());
  } else {
    props->SetPropertyAsUint32(u"file-error"_ns,
                               (uint32_t)aFallback.unwrapErr());
  }

  props->SetPropertyAsBool(u"crash"_ns, error.kind == Error::IPCError);

  nsIPropertyBag2* const iface = props;
  obsSvc->NotifyObservers(iface, "file-picker-crashed", nullptr);
}

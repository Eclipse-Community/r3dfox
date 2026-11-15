//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// global_state.cpp : Implements functions for querying the thread-local GL and EGL state.

#include "libGLESv2/global_state.h"

#include "common/debug.h"
#include "common/platform.h"
#include "common/system_utils.h"
#include "libANGLE/ErrorStrings.h"
#include "libANGLE/Thread.h"
#include "libGLESv2/resource.h"

#include <atomic>
#if defined(ANGLE_PLATFORM_APPLE)
#    include <dispatch/dispatch.h>
#endif
namespace egl
{
namespace
{
ANGLE_REQUIRE_CONSTANT_INIT std::atomic<angle::GlobalMutex *> g_Mutex{};
static_assert(std::is_trivially_destructible<decltype(g_Mutex)>::value,
              "global mutex is not trivially destructible");
ANGLE_REQUIRE_CONSTANT_INIT std::atomic<angle::GlobalMutex *> g_SurfaceMutex{};
static_assert(std::is_trivially_destructible<decltype(g_SurfaceMutex)>::value,
              "global mutex is not trivially destructible");

ANGLE_REQUIRE_CONSTANT_INIT gl::Context *g_LastContext(nullptr);
static_assert(std::is_trivially_destructible<decltype(g_LastContext)>::value,
              "global last context is not trivially destructible");

void SetContextToAndroidOpenGLTLSSlot(gl::Context *value)
{
#if defined(ANGLE_USE_ANDROID_TLS_SLOT)
    if (angle::gUseAndroidOpenGLTlsSlot)
    {
        ANGLE_ANDROID_GET_GL_TLS()[angle::kAndroidOpenGLTlsSlot] = static_cast<void *>(value);
    }
#endif
}

// Called only on Android platform
[[maybe_unused]] void ThreadCleanupCallback(void *ptr)
{
    ANGLE_SCOPED_GLOBAL_LOCK();
    angle::PthreadKeyDestructorCallback(ptr);
}

Thread *AllocateCurrentThread()
{
    Thread *thread;
    {
        // Global thread intentionally leaked
        ANGLE_SCOPED_DISABLE_LSAN();
        thread = new Thread();
#if defined(ANGLE_PLATFORM_APPLE)
        SetCurrentThreadTLS(thread);
#else
        gCurrentThread = thread;
#endif
    }

    // Initialize fast TLS slot
    SetContextToAndroidOpenGLTLSSlot(nullptr);

#if defined(ANGLE_PLATFORM_APPLE)
    gl::SetCurrentValidContextTLS(nullptr);
#else
    gl::gCurrentValidContext = nullptr;
#endif

#if defined(ANGLE_PLATFORM_ANDROID)
    static pthread_once_t keyOnce          = PTHREAD_ONCE_INIT;
    static TLSIndex gThreadCleanupTLSIndex = TLS_INVALID_INDEX;

    // Create thread cleanup TLS slot
    auto CreateThreadCleanupTLSIndex = []() {
        gThreadCleanupTLSIndex = CreateTLSIndex(ThreadCleanupCallback);
    };
    pthread_once(&keyOnce, CreateThreadCleanupTLSIndex);
    ASSERT(gThreadCleanupTLSIndex != TLS_INVALID_INDEX);

    // Initialize thread cleanup TLS slot
    SetTLSValue(gThreadCleanupTLSIndex, thread);
#endif  // ANGLE_PLATFORM_ANDROID

    ASSERT(thread);
    return thread;
}

void AllocateGlobalMutex(std::atomic<angle::GlobalMutex *> &mutex)
{
    if (mutex == nullptr)
    {
        std::unique_ptr<angle::GlobalMutex> newMutex(new angle::GlobalMutex());
        angle::GlobalMutex *expected = nullptr;
        if (mutex.compare_exchange_strong(expected, newMutex.get()))
        {
            newMutex.release();
        }
    }
}

void AllocateMutex()
{
    AllocateGlobalMutex(g_Mutex);
}

void AllocateSurfaceMutex()
{
    AllocateGlobalMutex(g_SurfaceMutex);
}

}  // anonymous namespace

#if defined(ANGLE_PLATFORM_APPLE)
// TODO(angleproject:6479): Due to a bug in Apple's dyld loader, `thread_local` will cause
// excessive memory use. Temporarily avoid it by using pthread's thread
// local storage instead.
// https://bugs.webkit.org/show_bug.cgi?id=228240

static TLSIndex GetCurrentThreadTLSIndex()
{
    static TLSIndex CurrentThreadIndex = TLS_INVALID_INDEX;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      ASSERT(CurrentThreadIndex == TLS_INVALID_INDEX);
      CurrentThreadIndex = CreateTLSIndex(nullptr);
    });
    return CurrentThreadIndex;
}
Thread *GetCurrentThreadTLS()
{
    TLSIndex CurrentThreadIndex = GetCurrentThreadTLSIndex();
    ASSERT(CurrentThreadIndex != TLS_INVALID_INDEX);
    return static_cast<Thread *>(GetTLSValue(CurrentThreadIndex));
}
void SetCurrentThreadTLS(Thread *thread)
{
    TLSIndex CurrentThreadIndex = GetCurrentThreadTLSIndex();
    ASSERT(CurrentThreadIndex != TLS_INVALID_INDEX);
    SetTLSValue(CurrentThreadIndex, thread);
}
#else
thread_local Thread *gCurrentThread = nullptr;
#endif

angle::GlobalMutex &GetGlobalMutex()
{
    AllocateMutex();
    return *g_Mutex;
}

angle::GlobalMutex &GetGlobalSurfaceMutex()
{
    AllocateSurfaceMutex();
    return *g_SurfaceMutex;
}

gl::Context *GetGlobalLastContext()
{
    return g_LastContext;
}

void SetGlobalLastContext(gl::Context *context)
{
    g_LastContext = context;
}

// This function causes an MSAN false positive, which is muted. See https://crbug.com/1211047
// It also causes a flaky false positive in TSAN. http://crbug.com/1223970
ANGLE_NO_SANITIZE_MEMORY ANGLE_NO_SANITIZE_THREAD Thread *GetCurrentThread()
{
#if defined(ANGLE_PLATFORM_APPLE)
    Thread *current = GetCurrentThreadTLS();
#else
    Thread *current = gCurrentThread;
#endif
    return (current ? current : AllocateCurrentThread());
}

void SetContextCurrent(Thread *thread, gl::Context *context)
{
#if defined(ANGLE_PLATFORM_APPLE)
    Thread *currentThread = GetCurrentThreadTLS();
#else
    Thread *currentThread = gCurrentThread;
#endif
    ASSERT(currentThread);
    currentThread->setCurrent(context);
    SetContextToAndroidOpenGLTLSSlot(context);

#if defined(ANGLE_PLATFORM_APPLE)
    gl::SetCurrentValidContextTLS(context);
#else
    gl::gCurrentValidContext = context;
#endif

#if defined(ANGLE_FORCE_CONTEXT_CHECK_EVERY_CALL)
    DirtyContextIfNeeded(context);
#endif
}

ScopedSyncCurrentContextFromThread::ScopedSyncCurrentContextFromThread(egl::Thread *thread)
    : mThread(thread)
{
    ASSERT(mThread);
}

ScopedSyncCurrentContextFromThread::~ScopedSyncCurrentContextFromThread()
{
    SetContextCurrent(mThread, mThread->getContext());
}

}  // namespace egl

namespace gl
{
void GenerateContextLostErrorOnContext(Context *context)
{
    if (context && context->isContextLost())
    {
        context->validationError(angle::EntryPoint::GLInvalid, GL_CONTEXT_LOST, err::kContextLost);
    }
}

void GenerateContextLostErrorOnCurrentGlobalContext()
{
    // If the client starts issuing GL calls before ANGLE has had a chance to initialize,
    // GenerateContextLostErrorOnCurrentGlobalContext can be called before AllocateCurrentThread has
    // had a chance to run. Calling GetCurrentThread() ensures that TLS thread state is set up.
    egl::GetCurrentThread();

    GenerateContextLostErrorOnContext(GetGlobalContext());
}
}  // namespace gl

#if defined(ANGLE_PLATFORM_WINDOWS) && !defined(ANGLE_STATIC)
namespace egl
{

namespace
{

void DeallocateGlobalMutex(std::atomic<angle::GlobalMutex *> &mutex)
{
    angle::GlobalMutex *toDelete = mutex.exchange(nullptr);
    if (!mutex)
        return;
    {
        // Wait for toDelete to become released by other threads before deleting.
        std::lock_guard<angle::GlobalMutex> lock(*toDelete);
    }
    SafeDelete(toDelete);
}

void DeallocateCurrentThread()
{
    SafeDelete(gCurrentThread);
}

void DeallocateMutex()
{
    DeallocateGlobalMutex(g_Mutex);
}

void DeallocateSurfaceMutex()
{
    DeallocateGlobalMutex(g_SurfaceMutex);
}

bool InitializeProcess()
{
    EnsureDebugAllocated();
    AllocateMutex();
    return AllocateCurrentThread() != nullptr;
}

void TerminateProcess()
{
    DeallocateDebug();
    DeallocateSurfaceMutex();
    DeallocateMutex();
    DeallocateCurrentThread();
}

}  // anonymous namespace

}  // namespace egl

namespace
{
// The following WaitForDebugger code is based on SwiftShader. See:
// https://cs.chromium.org/chromium/src/third_party/swiftshader/src/Vulkan/main.cpp
#    if defined(ANGLE_ENABLE_ASSERTS) && !defined(ANGLE_ENABLE_WINDOWS_UWP)
INT_PTR CALLBACK DebuggerWaitDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT rect;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            ::GetWindowRect(GetDesktopWindow(), &rect);
            ::SetWindowPos(hwnd, HWND_TOP, rect.right / 2, rect.bottom / 2, 0, 0, SWP_NOSIZE);
            ::SetTimer(hwnd, 1, 100, NULL);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL)
            {
                ::EndDialog(hwnd, 0);
            }
            break;
        case WM_TIMER:
            if (angle::IsDebuggerAttached())
            {
                ::EndDialog(hwnd, 0);
            }
    }

    return FALSE;
}

void WaitForDebugger(HINSTANCE instance)
{
    if (angle::IsDebuggerAttached())
        return;

    HRSRC dialog = ::FindResourceA(instance, MAKEINTRESOURCEA(IDD_DIALOG1), MAKEINTRESOURCEA(5));
    if (!dialog)
    {
        printf("Error finding wait for debugger dialog. Error %lu.\n", ::GetLastError());
        return;
    }

    DLGTEMPLATE *dialogTemplate = reinterpret_cast<DLGTEMPLATE *>(::LoadResource(instance, dialog));
    ::DialogBoxIndirectA(instance, dialogTemplate, NULL, DebuggerWaitDialogProc);
}
#    else
void WaitForDebugger(HINSTANCE instance) {}
#    endif  // defined(ANGLE_ENABLE_ASSERTS) && !defined(ANGLE_ENABLE_WINDOWS_UWP)
}  // namespace

#pragma comment(lib, "ntdll.lib")

#ifdef __cplusplus
extern "C" {
#endif

//
// Logical Data Type - These are 32-bit logical values.
//

typedef ULONG LOGICAL;
typedef ULONG *PLOGICAL;

// begin_ntndis begin_windbgkd
//
// NTSTATUS
//

typedef LONG NTSTATUS;
/*lint -save -e624 */  // Don't complain about different typedefs.
typedef NTSTATUS *PNTSTATUS;
/*lint -restore */  // Resume checking for different typedefs.

//
//  Status values are 32 bit values layed out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-------------------------+-------------------------------+
//  |Sev|C|       Facility          |               Code            |
//  +---+-+-------------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//

//
// Generic test for success on any status value (non-negative numbers
// indicate success).
//

#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

//
// Unicode strings are counted 16-bit character strings. If they are
// NULL terminated, Length does not include trailing NULL.
//

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
#ifdef MIDL_PASS
    [size_is(MaximumLength / 2), length_is((Length) / 2) ] USHORT * Buffer;
#else // MIDL_PASS
    PWSTR  Buffer;
#endif // MIDL_PASS
} UNICODE_STRING;
typedef UNICODE_STRING *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;
#define UNICODE_NULL ((WCHAR)0) // winnt

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;        // Points to type SECURITY_DESCRIPTOR
    PVOID SecurityQualityOfService;  // Points to type SECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES;
typedef OBJECT_ATTRIBUTES *POBJECT_ATTRIBUTES;
typedef CONST OBJECT_ATTRIBUTES *PCOBJECT_ATTRIBUTES;

//++
//
// VOID
// InitializeObjectAttributes(
//     OUT POBJECT_ATTRIBUTES p,
//     IN PUNICODE_STRING n,
//     IN ULONG a,
//     IN HANDLE r,
//     IN PSECURITY_DESCRIPTOR s
//     )
//
//--

#define InitializeObjectAttributes( p, n, a, r, s ) { \
    (p)->Length = sizeof( OBJECT_ATTRIBUTES );          \
    (p)->RootDirectory = r;                             \
    (p)->Attributes = a;                                \
    (p)->ObjectName = n;                                \
    (p)->SecurityDescriptor = s;                        \
    (p)->SecurityQualityOfService = NULL;               \
    }

//
// The success status codes 0 - 63 are reserved for wait completion status.
// FacilityCodes 0x5 - 0xF have been allocated by various drivers.
//
#define STATUS_SUCCESS                          ((NTSTATUS)0x00000000L) // ntsubauth

//
// MessageId: STATUS_INVALID_INFO_CLASS
//
// MessageText:
//
//  {Invalid Parameter}
//  The specified information class is not a valid information class for the specified object.
//
#define STATUS_INVALID_INFO_CLASS        ((NTSTATUS)0xC0000003L)    // ntsubauth

//
// MessageId: STATUS_INFO_LENGTH_MISMATCH
//
// MessageText:
//
//  The specified information record length does not match the length required for the specified information class.
//
#define STATUS_INFO_LENGTH_MISMATCH      ((NTSTATUS)0xC0000004L)

//
// Thread priority
//

typedef LONG KPRIORITY;

struct _TEB *
NtCurrentTeb(void);

//
// Loader Data Table. Used to track DLLs loaded into an
// image.
//

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    union {
        LIST_ENTRY HashLinks;
        struct {
            PVOID SectionPointer;
            ULONG CheckSum;
        };
    };
    union {
        struct {
            ULONG TimeDateStamp;
        };
        struct {
            PVOID LoadedImports;
        };
    };
    struct _ACTIVATION_CONTEXT * EntryPointActivationContext;
    
    PVOID PatchInformation; 
    
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

//
// Process Environment Block
//
#ifdef _MAC
#pragma warning( disable : 4121)
#endif

typedef struct _PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    HANDLE SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID EntryInProgress;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

#ifdef _MAC
#pragma warning( default : 4121 )
#endif
//
// Handle tag bits for Peb Stdio File Handles
//

#define PEB_STDIO_HANDLE_NATIVE     0
#define PEB_STDIO_HANDLE_SUBSYS     1
#define PEB_STDIO_HANDLE_PM         2
#define PEB_STDIO_HANDLE_RESERVED   3

#define GDI_HANDLE_BUFFER_SIZE32  34
#define GDI_HANDLE_BUFFER_SIZE64  60

#if !defined(_IA64_) && !defined(_AMD64_)
#define GDI_HANDLE_BUFFER_SIZE      GDI_HANDLE_BUFFER_SIZE32
#else
#define GDI_HANDLE_BUFFER_SIZE      GDI_HANDLE_BUFFER_SIZE64
#endif

typedef ULONG GDI_HANDLE_BUFFER32[GDI_HANDLE_BUFFER_SIZE32];
typedef ULONG GDI_HANDLE_BUFFER64[GDI_HANDLE_BUFFER_SIZE64];
typedef ULONG GDI_HANDLE_BUFFER  [GDI_HANDLE_BUFFER_SIZE  ];

#define FOREGROUND_BASE_PRIORITY  9
#define NORMAL_BASE_PRIORITY      8

typedef struct _PEB_FREE_BLOCK {
    struct _PEB_FREE_BLOCK *Next;
    ULONG Size;
} PEB_FREE_BLOCK, *PPEB_FREE_BLOCK;

// begin_ntddk begin_wdm begin_nthal begin_ntifs
//
// ClientId
//

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID;
typedef CLIENT_ID *PCLIENT_ID;

// end_ntddk end_wdm end_nthal end_ntifs

#if !defined(CLIENT_ID64_DEFINED)

typedef struct _CLIENT_ID64 {
    ULONGLONG  UniqueProcess;
    ULONGLONG  UniqueThread;
} CLIENT_ID64;

typedef CLIENT_ID64 *PCLIENT_ID64;

#define CLIENT_ID64_DEFINED

#endif

#define FLS_MAXIMUM_AVAILABLE 128   // winnt
#define TLS_MINIMUM_AVAILABLE 64    // winnt
#define TLS_EXPANSION_SLOTS   1024

typedef
VOID
(*PPS_POST_PROCESS_INIT_ROUTINE) (
    VOID
    );

//
// Gdi command batching
//

#define GDI_BATCH_BUFFER_SIZE 310

typedef struct _GDI_TEB_BATCH {
    ULONG    Offset;
    ULONG_PTR HDC;
    ULONG    Buffer[GDI_BATCH_BUFFER_SIZE];
} GDI_TEB_BATCH,*PGDI_TEB_BATCH;


//
// Wx86 thread state information
//

typedef struct _Wx86ThreadState {
    PULONG  CallBx86Eip;
    PVOID   DeallocationCpu;
    BOOLEAN UseKnownWx86Dll;
    char    OleStubInvoked;
} WX86THREAD, *PWX86THREAD;

//
//  TEB - The thread environment block
//

#define STATIC_UNICODE_BUFFER_LENGTH 261
#define WIN32_CLIENT_INFO_LENGTH 62

#define WIN32_CLIENT_INFO_SPIN_COUNT 1

typedef PVOID* PPVOID;

#define PEBTEB_PRIVATE_PASTE(x,y)       x##y
#define PEBTEB_PASTE(x,y)               PEBTEB_PRIVATE_PASTE(x,y)

//
// Declare and use regular native types.
//
#define PEBTEB_POINTER(x) x
#define PEBTEB_STRUCT(x)  x

/* for searching
typedef struct _PEB
typedef struct _PEB32
typedef struct _PEB64
*/
typedef struct PEBTEB_STRUCT(_PEB) {
    BOOLEAN InheritedAddressSpace;      // These four fields cannot change unless the
    BOOLEAN ReadImageFileExecOptions;   //
    BOOLEAN BeingDebugged;              //
    BOOLEAN SpareBool;                  //
    PEBTEB_POINTER(HANDLE) Mutant;      // INITIAL_PEB structure is also updated.

    PEBTEB_POINTER(PVOID) ImageBaseAddress;
    PEBTEB_POINTER(PPEB_LDR_DATA) Ldr;
    PEBTEB_POINTER(struct _RTL_USER_PROCESS_PARAMETERS*) ProcessParameters;
    PEBTEB_POINTER(PVOID) SubSystemData;
    PEBTEB_POINTER(PVOID) ProcessHeap;
    PEBTEB_POINTER(struct _RTL_CRITICAL_SECTION*) FastPebLock;
    PEBTEB_POINTER(PVOID) SparePtr1;
    PEBTEB_POINTER(PVOID) SparePtr2;
    ULONG EnvironmentUpdateCount;
    PEBTEB_POINTER(PVOID) KernelCallbackTable;
    ULONG SystemReserved[1];

    struct {
        ULONG ExecuteOptions : 2;
        ULONG SpareBits : 30;
    };    


    PEBTEB_POINTER(PPEB_FREE_BLOCK) FreeList;
    ULONG TlsExpansionCounter;
    PEBTEB_POINTER(PVOID) TlsBitmap;
    ULONG TlsBitmapBits[2];         // TLS_MINIMUM_AVAILABLE bits
    PEBTEB_POINTER(PVOID) ReadOnlySharedMemoryBase;
    PEBTEB_POINTER(PVOID) ReadOnlySharedMemoryHeap;
    PEBTEB_POINTER(PPVOID) ReadOnlyStaticServerData;
    PEBTEB_POINTER(PVOID) AnsiCodePageData;
    PEBTEB_POINTER(PVOID) OemCodePageData;
    PEBTEB_POINTER(PVOID) UnicodeCaseTableData;

    //
    // Useful information for LdrpInitialize
    ULONG NumberOfProcessors;
    ULONG NtGlobalFlag;

    //
    // Passed up from MmCreatePeb from Session Manager registry key
    //

    LARGE_INTEGER CriticalSectionTimeout;
    PEBTEB_POINTER(SIZE_T) HeapSegmentReserve;
    PEBTEB_POINTER(SIZE_T) HeapSegmentCommit;
    PEBTEB_POINTER(SIZE_T) HeapDeCommitTotalFreeThreshold;
    PEBTEB_POINTER(SIZE_T) HeapDeCommitFreeBlockThreshold;

    //
    // Where heap manager keeps track of all heaps created for a process
    // Fields initialized by MmCreatePeb.  ProcessHeaps is initialized
    // to point to the first free byte after the PEB and MaximumNumberOfHeaps
    // is computed from the page size used to hold the PEB, less the fixed
    // size of this data structure.
    //

    ULONG NumberOfHeaps;
    ULONG MaximumNumberOfHeaps;
    PEBTEB_POINTER(PPVOID) ProcessHeaps;

    //
    //
    PEBTEB_POINTER(PVOID) GdiSharedHandleTable;
    PEBTEB_POINTER(PVOID) ProcessStarterHelper;
    ULONG GdiDCAttributeList;
    PEBTEB_POINTER(struct _RTL_CRITICAL_SECTION*) LoaderLock;

    //
    // Following fields filled in by MmCreatePeb from system values and/or
    // image header.
    //

    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    USHORT OSBuildNumber;
    USHORT OSCSDVersion;
    ULONG OSPlatformId;
    ULONG ImageSubsystem;
    ULONG ImageSubsystemMajorVersion;
    ULONG ImageSubsystemMinorVersion;
    PEBTEB_POINTER(ULONG_PTR) ImageProcessAffinityMask;
    PEBTEB_STRUCT(GDI_HANDLE_BUFFER) GdiHandleBuffer;
    PEBTEB_POINTER(PPS_POST_PROCESS_INIT_ROUTINE) PostProcessInitRoutine;

    PEBTEB_POINTER(PVOID) TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32];   // TLS_EXPANSION_SLOTS bits

    //
    // Id of the Hydra session in which this process is running
    //
    ULONG SessionId;

    //
    // Filled in by LdrpInstallAppcompatBackend
    //
    ULARGE_INTEGER AppCompatFlags;

    //
    // ntuser appcompat flags
    //
    ULARGE_INTEGER AppCompatFlagsUser;

    //
    // Filled in by LdrpInstallAppcompatBackend
    //
    PEBTEB_POINTER(PVOID) pShimData;

    //
    // Filled in by LdrQueryImageFileExecutionOptions
    //
    PEBTEB_POINTER(PVOID) AppCompatInfo;

    //
    // Used by GetVersionExW as the szCSDVersion string
    //
    PEBTEB_STRUCT(UNICODE_STRING) CSDVersion;

    //
    // Fusion stuff
    //
    PEBTEB_POINTER(const struct _ACTIVATION_CONTEXT_DATA *) ActivationContextData;
    PEBTEB_POINTER(struct _ASSEMBLY_STORAGE_MAP *) ProcessAssemblyStorageMap;
    PEBTEB_POINTER(const struct _ACTIVATION_CONTEXT_DATA *) SystemDefaultActivationContextData;
    PEBTEB_POINTER(struct _ASSEMBLY_STORAGE_MAP *) SystemAssemblyStorageMap;
    
    //
    // Enforced minimum initial commit stack
    //
    PEBTEB_POINTER(SIZE_T) MinimumStackCommit;

    //
    // Fiber local storage.
    //

    PEBTEB_POINTER(PPVOID) FlsCallback;
    PEBTEB_STRUCT(LIST_ENTRY) FlsListHead;
    PEBTEB_POINTER(PVOID) FlsBitmap;
    ULONG FlsBitmapBits[FLS_MAXIMUM_AVAILABLE / (sizeof(ULONG) * 8)];
    ULONG FlsHighIndex;
} PEBTEB_STRUCT(PEB), * PEBTEB_STRUCT(PPEB);

//
//  Fusion/sxs thread state information
//

#define ACTIVATION_CONTEXT_STACK_FLAG_QUERIES_DISABLED (0x00000001)

typedef struct PEBTEB_STRUCT(_ACTIVATION_CONTEXT_STACK) {
    ULONG Flags;
    ULONG NextCookieSequenceNumber;
    PEBTEB_POINTER(struct _RTL_ACTIVATION_CONTEXT_STACK_FRAME *) ActiveFrame;
    PEBTEB_STRUCT(LIST_ENTRY) FrameListCache;
} PEBTEB_STRUCT(ACTIVATION_CONTEXT_STACK), * PEBTEB_STRUCT(PACTIVATION_CONTEXT_STACK);

typedef const PEBTEB_STRUCT(ACTIVATION_CONTEXT_STACK) * PEBTEB_STRUCT(PCACTIVATION_CONTEXT_STACK);

#define TEB_ACTIVE_FRAME_CONTEXT_FLAG_EXTENDED (0x00000001)

typedef struct PEBTEB_STRUCT(_TEB_ACTIVE_FRAME_CONTEXT) {
    ULONG Flags;
    PEBTEB_POINTER(PCSTR) FrameName;
} PEBTEB_STRUCT(TEB_ACTIVE_FRAME_CONTEXT), *PEBTEB_STRUCT(PTEB_ACTIVE_FRAME_CONTEXT);

typedef const PEBTEB_STRUCT(TEB_ACTIVE_FRAME_CONTEXT) *PEBTEB_STRUCT(PCTEB_ACTIVE_FRAME_CONTEXT);

typedef struct PEBTEB_STRUCT(_TEB_ACTIVE_FRAME_CONTEXT_EX) {
    PEBTEB_STRUCT(TEB_ACTIVE_FRAME_CONTEXT) BasicContext;
    PEBTEB_POINTER(PCSTR) SourceLocation; // e.g. "Z:\foo\bar\baz.c"
} PEBTEB_STRUCT(TEB_ACTIVE_FRAME_CONTEXT_EX), *PEBTEB_STRUCT(PTEB_ACTIVE_FRAME_CONTEXT_EX);

typedef const PEBTEB_STRUCT(TEB_ACTIVE_FRAME_CONTEXT_EX) *PEBTEB_STRUCT(PCTEB_ACTIVE_FRAME_CONTEXT_EX);

#define TEB_ACTIVE_FRAME_FLAG_EXTENDED (0x00000001)

typedef struct PEBTEB_STRUCT(_TEB_ACTIVE_FRAME) {
    ULONG Flags;
    PEBTEB_POINTER(struct _TEB_ACTIVE_FRAME*) Previous;
    PEBTEB_POINTER(PCTEB_ACTIVE_FRAME_CONTEXT) Context;
} PEBTEB_STRUCT(TEB_ACTIVE_FRAME), *PEBTEB_STRUCT(PTEB_ACTIVE_FRAME);

typedef const PEBTEB_STRUCT(TEB_ACTIVE_FRAME) *PEBTEB_STRUCT(PCTEB_ACTIVE_FRAME);

typedef struct PEBTEB_STRUCT(_TEB_ACTIVE_FRAME_EX) {
    PEBTEB_STRUCT(TEB_ACTIVE_FRAME) BasicFrame;
    PEBTEB_POINTER(PVOID) ExtensionIdentifier; // use address of your DLL Main or something unique to your mapping in the address space
} PEBTEB_STRUCT(TEB_ACTIVE_FRAME_EX), *PEBTEB_STRUCT(PTEB_ACTIVE_FRAME_EX);

typedef const PEBTEB_STRUCT(TEB_ACTIVE_FRAME_EX) *PEBTEB_STRUCT(PCTEB_ACTIVE_FRAME_EX);

/* for searching
typedef struct _TEB
typedef struct _TEB32
typedef struct _TEB64
*/
typedef struct PEBTEB_STRUCT(_TEB) {
    PEBTEB_STRUCT(NT_TIB) NtTib;
    PEBTEB_POINTER(PVOID) EnvironmentPointer;
    PEBTEB_STRUCT(CLIENT_ID) ClientId;
    PEBTEB_POINTER(PVOID) ActiveRpcHandle;
    PEBTEB_POINTER(PVOID) ThreadLocalStoragePointer;
    PEBTEB_POINTER(PPEB) ProcessEnvironmentBlock;
    ULONG LastErrorValue;
    ULONG CountOfOwnedCriticalSections;
    PEBTEB_POINTER(PVOID) CsrClientThread;
    PEBTEB_POINTER(PVOID) Win32ThreadInfo;          // PtiCurrent
    ULONG User32Reserved[26];       // user32.dll items
    ULONG UserReserved[5];          // Winsrv SwitchStack
    PEBTEB_POINTER(PVOID) WOW32Reserved;            // used by WOW
    LCID CurrentLocale;
    ULONG FpSoftwareStatusRegister; // offset known by outsiders!
    PEBTEB_POINTER(PVOID) SystemReserved1[54];      // Used by FP emulator
    NTSTATUS ExceptionCode;         // for RaiseUserException
    PEBTEB_STRUCT(ACTIVATION_CONTEXT_STACK) ActivationContextStack;   // Fusion activation stack
    // sizeof(PEBTEB_POINTER(PVOID)) is a way to express processor-dependence, more generally than #ifdef _WIN64
    UCHAR SpareBytes1[48 - sizeof(PEBTEB_POINTER(PVOID)) - sizeof(PEBTEB_STRUCT(ACTIVATION_CONTEXT_STACK))];
    PEBTEB_STRUCT(GDI_TEB_BATCH) GdiTebBatch;      // Gdi batching
    PEBTEB_STRUCT(CLIENT_ID) RealClientId;
    PEBTEB_POINTER(HANDLE) GdiCachedProcessHandle;
    ULONG GdiClientPID;
    ULONG GdiClientTID;
    PEBTEB_POINTER(PVOID) GdiThreadLocalInfo;
    PEBTEB_POINTER(ULONG_PTR) Win32ClientInfo[WIN32_CLIENT_INFO_LENGTH]; // User32 Client Info
    PEBTEB_POINTER(PVOID) glDispatchTable[233];     // OpenGL
    PEBTEB_POINTER(ULONG_PTR) glReserved1[29];      // OpenGL
    PEBTEB_POINTER(PVOID) glReserved2;              // OpenGL
    PEBTEB_POINTER(PVOID) glSectionInfo;            // OpenGL
    PEBTEB_POINTER(PVOID) glSection;                // OpenGL
    PEBTEB_POINTER(PVOID) glTable;                  // OpenGL
    PEBTEB_POINTER(PVOID) glCurrentRC;              // OpenGL
    PEBTEB_POINTER(PVOID) glContext;                // OpenGL
    ULONG LastStatusValue;
    PEBTEB_STRUCT(UNICODE_STRING) StaticUnicodeString;
    WCHAR StaticUnicodeBuffer[STATIC_UNICODE_BUFFER_LENGTH];
    PEBTEB_POINTER(PVOID) DeallocationStack;
    PEBTEB_POINTER(PVOID) TlsSlots[TLS_MINIMUM_AVAILABLE];
    PEBTEB_STRUCT(LIST_ENTRY) TlsLinks;
    PEBTEB_POINTER(PVOID) Vdm;
    PEBTEB_POINTER(PVOID) ReservedForNtRpc;
    PEBTEB_POINTER(PVOID) DbgSsReserved[2];
    ULONG HardErrorMode;
    PEBTEB_POINTER(PVOID) Instrumentation[16];
    PEBTEB_POINTER(PVOID) WinSockData;              // WinSock
    ULONG GdiBatchCount;
    BOOLEAN InDbgPrint;
    BOOLEAN FreeStackOnTermination;
    BOOLEAN HasFiberData;
    BOOLEAN IdealProcessor;
    ULONG Spare3;
    PEBTEB_POINTER(PVOID) ReservedForPerf;
    PEBTEB_POINTER(PVOID) ReservedForOle;
    ULONG WaitingOnLoaderLock;
    PEBTEB_STRUCT(WX86THREAD) Wx86Thread;
    PEBTEB_POINTER(PPVOID) TlsExpansionSlots;
#if (defined(_WIN64) && !defined(PEBTEB_BITS)) \
    || ((defined(_WIN64) || defined(_X86_)) && defined(PEBTEB_BITS) && PEBTEB_BITS == 64)
    //
    // These are in native Win64 TEB, Win64 TEB64, and x86 TEB64.
    //
    PEBTEB_POINTER(PVOID) DeallocationBStore;
    PEBTEB_POINTER(PVOID) BStoreLimit;
#endif    
    LCID ImpersonationLocale;       // Current locale of impersonated user
    ULONG IsImpersonating;          // Thread impersonation status
    PEBTEB_POINTER(PVOID) NlsCache;                 // NLS thread cache
    PEBTEB_POINTER(PVOID) pShimData;                // Per thread data used in the shim
    ULONG HeapVirtualAffinity;
    PEBTEB_POINTER(HANDLE) CurrentTransactionHandle;// reserved for TxF transaction context
    PEBTEB_POINTER(PTEB_ACTIVE_FRAME) ActiveFrame;
    PEBTEB_POINTER(PVOID) FlsData;
    
} PEBTEB_STRUCT(TEB), *PEBTEB_STRUCT(PTEB);

#undef PEBTEB_POINTER
#undef PEBTEB_STRUCT
#undef PEBTEB_PRIVATE_PASTE
#undef PEBTEB_PASTE
//
// Thread Information Classes
//

typedef enum _THREADINFOCLASS {
    ThreadBasicInformation,
    ThreadTimes,
    ThreadPriority,
    ThreadBasePriority,
    ThreadAffinityMask,
    ThreadImpersonationToken,
    ThreadDescriptorTableEntry,
    ThreadEnableAlignmentFaultFixup,
    ThreadEventPair_Reusable,
    ThreadQuerySetWin32StartAddress,
    ThreadZeroTlsCell,
    ThreadPerformanceCount,
    ThreadAmILastThread,
    ThreadIdealProcessor,
    ThreadPriorityBoost,
    ThreadSetTlsArrayAddress,
    ThreadIsIoPending,
    ThreadHideFromDebugger,
    ThreadBreakOnTermination,
    MaxThreadInfoClass
    } THREADINFOCLASS;
//
// Basic Thread Information
//  NtQueryInformationThread using ThreadBasicInfo
//

typedef struct _THREAD_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PTEB TebBaseAddress;
    CLIENT_ID ClientId;
    ULONG_PTR AffinityMask;
    KPRIORITY Priority;
    LONG BasePriority;
} THREAD_BASIC_INFORMATION;
typedef THREAD_BASIC_INFORMATION *PTHREAD_BASIC_INFORMATION;

#define NtCurrentPeb() (NtCurrentTeb()->ProcessEnvironmentBlock)

NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenThread (
    OUT PHANDLE ThreadHandle,
    IN ACCESS_MASK DesiredAccess,
    IN POBJECT_ATTRIBUTES ObjectAttributes,
    IN PCLIENT_ID ClientId OPTIONAL
    );

NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryInformationThread(
    IN HANDLE ThreadHandle,
    IN THREADINFOCLASS ThreadInformationClass,
    OUT PVOID ThreadInformation,
    IN ULONG ThreadInformationLength,
    OUT PULONG ReturnLength OPTIONAL
    );

//
// System Information Classes.
//

typedef enum _SYSTEM_INFORMATION_CLASS {
    SystemBasicInformation,
    SystemProcessorInformation,             // obsolete...delete
    SystemPerformanceInformation,
    SystemTimeOfDayInformation,
    SystemPathInformation,
    SystemProcessInformation,
    SystemCallCountInformation,
    SystemDeviceInformation,
    SystemProcessorPerformanceInformation,
    SystemFlagsInformation,
    SystemCallTimeInformation,
    SystemModuleInformation,
    SystemLocksInformation,
    SystemStackTraceInformation,
    SystemPagedPoolInformation,
    SystemNonPagedPoolInformation,
    SystemHandleInformation,
    SystemObjectInformation,
    SystemPageFileInformation,
    SystemVdmInstemulInformation,
    SystemVdmBopInformation,
    SystemFileCacheInformation,
    SystemPoolTagInformation,
    SystemInterruptInformation,
    SystemDpcBehaviorInformation,
    SystemFullMemoryInformation,
    SystemLoadGdiDriverInformation,
    SystemUnloadGdiDriverInformation,
    SystemTimeAdjustmentInformation,
    SystemSummaryMemoryInformation,
    SystemMirrorMemoryInformation,
    SystemPerformanceTraceInformation,
    SystemObsolete0,
    SystemExceptionInformation,
    SystemCrashDumpStateInformation,
    SystemKernelDebuggerInformation,
    SystemContextSwitchInformation,
    SystemRegistryQuotaInformation,
    SystemExtendServiceTableInformation,
    SystemPrioritySeperation,
    SystemVerifierAddDriverInformation,
    SystemVerifierRemoveDriverInformation,
    SystemProcessorIdleInformation,
    SystemLegacyDriverInformation,
    SystemCurrentTimeZoneInformation,
    SystemLookasideInformation,
    SystemTimeSlipNotification,
    SystemSessionCreate,
    SystemSessionDetach,
    SystemSessionInformation,
    SystemRangeStartInformation,
    SystemVerifierInformation,
    SystemVerifierThunkExtend,
    SystemSessionProcessInformation,
    SystemLoadGdiDriverInSystemSpace,
    SystemNumaProcessorMap,
    SystemPrefetcherInformation,
    SystemExtendedProcessInformation,
    SystemRecommendedSharedDataAlignment,
    SystemComPlusPackage,
    SystemNumaAvailableMemory,
    SystemProcessorPowerInformation,
    SystemEmulationBasicInformation,
    SystemEmulationProcessorInformation,
    SystemExtendedHandleInformation,
    SystemLostDelayedWriteInformation,
    SystemBigPoolInformation,
    SystemSessionPoolTagInformation,
    SystemSessionMappedViewInformation,
    SystemHotpatchInformation,
    SystemObjectSecurityMode,
    SystemWatchdogTimerHandler,
    SystemWatchdogTimerInformation,
    SystemLogicalProcessorInformation,
    MaxSystemInfoClass  // MaxSystemInfoClass should always be the last enum
} SYSTEM_INFORMATION_CLASS;

typedef struct _SYSTEM_PROCESS_INFORMATION {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER SpareLi1;
    LARGE_INTEGER SpareLi2;
    LARGE_INTEGER SpareLi3;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR PageDirectoryBase;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
} SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;

typedef struct _SYSTEM_THREAD_INFORMATION {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG WaitTime;
    PVOID StartAddress;
    CLIENT_ID ClientId;
    KPRIORITY Priority;
    LONG BasePriority;
    ULONG ContextSwitches;
    ULONG ThreadState;
    ULONG WaitReason;
} SYSTEM_THREAD_INFORMATION, *PSYSTEM_THREAD_INFORMATION;

NTSYSCALLAPI
NTSTATUS
NTAPI
NtQuerySystemInformation (
    IN SYSTEM_INFORMATION_CLASS SystemInformationClass,
    OUT PVOID SystemInformation,
    IN ULONG SystemInformationLength,
    OUT PULONG ReturnLength OPTIONAL
    );

#ifndef InitializeListHead


//
//  Doubly-linked list manipulation routines.  Implemented as macros
//  but logically these are procedures.
//

//
//  VOID
//  InitializeListHead(
//      PLIST_ENTRY ListHead
//      );
//

#define InitializeListHead(ListHead) (\
    (ListHead)->Flink = (ListHead)->Blink = (ListHead))

//
//  BOOLEAN
//  IsListEmpty(
//      PLIST_ENTRY ListHead
//      );
//

#define IsListEmpty(ListHead) \
    ((ListHead)->Flink == (ListHead))

//
//  PLIST_ENTRY
//  RemoveHeadList(
//      PLIST_ENTRY ListHead
//      );
//

#define RemoveHeadList(ListHead) \
    (ListHead)->Flink;\
    {RemoveEntryList((ListHead)->Flink)}

//
//  PLIST_ENTRY
//  RemoveTailList(
//      PLIST_ENTRY ListHead
//      );
//

#define RemoveTailList(ListHead) \
    (ListHead)->Blink;\
    {RemoveEntryList((ListHead)->Blink)}

//
//  VOID
//  RemoveEntryList(
//      PLIST_ENTRY Entry
//      );
//

#define RemoveEntryList(Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_Flink;\
    _EX_Flink = (Entry)->Flink;\
    _EX_Blink = (Entry)->Blink;\
    _EX_Blink->Flink = _EX_Flink;\
    _EX_Flink->Blink = _EX_Blink;\
    }

//
//  VOID
//  InsertTailList(
//      PLIST_ENTRY ListHead,
//      PLIST_ENTRY Entry
//      );
//

#define InsertTailList(ListHead,Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Blink = _EX_ListHead->Blink;\
    (Entry)->Flink = _EX_ListHead;\
    (Entry)->Blink = _EX_Blink;\
    _EX_Blink->Flink = (Entry);\
    _EX_ListHead->Blink = (Entry);\
    }

//
//  VOID
//  InsertHeadList(
//      PLIST_ENTRY ListHead,
//      PLIST_ENTRY Entry
//      );
//

#define InsertHeadList(ListHead,Entry) {\
    PLIST_ENTRY _EX_Flink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Flink = _EX_ListHead->Flink;\
    (Entry)->Flink = _EX_Flink;\
    (Entry)->Blink = _EX_ListHead;\
    _EX_Flink->Blink = (Entry);\
    _EX_ListHead->Flink = (Entry);\
    }

//
//
//  PSINGLE_LIST_ENTRY
//  PopEntryList(
//      PSINGLE_LIST_ENTRY ListHead
//      );
//

#define PopEntryList(ListHead) \
    (ListHead)->Next;\
    {\
        PSINGLE_LIST_ENTRY FirstEntry;\
        FirstEntry = (ListHead)->Next;\
        if (FirstEntry != NULL) {     \
            (ListHead)->Next = FirstEntry->Next;\
        }                             \
    }


//
//  VOID
//  PushEntryList(
//      PSINGLE_LIST_ENTRY ListHead,
//      PSINGLE_LIST_ENTRY Entry
//      );
//

#define PushEntryList(ListHead,Entry) \
    (Entry)->Next = (ListHead)->Next; \
    (ListHead)->Next = (Entry)


#endif
NTSYSAPI
PVOID
NTAPI
RtlAllocateHeap(
    IN PVOID HeapHandle,
    IN ULONG Flags,
    IN SIZE_T Size
    );

NTSYSAPI
BOOLEAN
NTAPI
RtlFreeHeap(
    IN PVOID HeapHandle,
    IN ULONG Flags,
    IN PVOID BaseAddress
    );

NTSYSAPI
PVOID
NTAPI
RtlImageDirectoryEntryToData(
    PVOID BaseOfImage,
    BOOLEAN MappedAsImage,
    USHORT DirectoryEntry,
    PULONG Size
    );

#define HEAP_MAKE_TAG_FLAGS( b, o ) ((ULONG)((b) + ((o) << 18)))  // winnt
#define RtlProcessHeap() (NtCurrentPeb()->ProcessHeap)

#define RTL_HEAP_MAKE_TAG HEAP_MAKE_TAG_FLAGS

#define LdrpCallInitRoutine(InitRoutine, DllHandle, Reason, Context)    \
    (InitRoutine)((DllHandle), (Reason), (Context))

#ifdef __cplusplus
}
#endif

ULONG LdrpNumberOfTlsEntries;

NTSTATUS
LdrpInitializeTls (
    VOID
    )
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Head,Next;
    PIMAGE_TLS_DIRECTORY TlsImage;
    ULONG TlsSize;

    //
    // Walk through the loaded modules and look for TLS. If we find TLS,
    // lock in the module and add to the TLS chain.
    //

    Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
    Next = Head->Flink;

    while (Next != Head) {

        Entry = CONTAINING_RECORD(Next, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        Next = Next->Flink;

        TlsImage = (PIMAGE_TLS_DIRECTORY)RtlImageDirectoryEntryToData(
                           Entry->DllBase,
                           TRUE,
                           IMAGE_DIRECTORY_ENTRY_TLS,
                           &TlsSize);

        if (TlsImage &&
            TlsImage->AddressOfIndex
           ) {
            LdrpNumberOfTlsEntries = ( (*(PLONG)TlsImage->AddressOfIndex + 1 > LdrpNumberOfTlsEntries) ? (*(PLONG)TlsImage->AddressOfIndex + 1) : LdrpNumberOfTlsEntries ) ;
        }
    }

    //
    // We now have walked through all static DLLs and know
    // all DLLs that reference thread local storage. Now we
    // just have to allocate the thread local storage for the current
    // thread and for all subsequent threads.
    //

//  return LdrpAllocateTls ();
    return STATUS_SUCCESS;
}


NTSTATUS
LdrpAllocateTls (
    IN PVOID DllBase,
    IN PTEB Teb
    )
{
    PVOID *TlsVector;
    HANDLE ProcessHeap;
    PIMAGE_TLS_DIRECTORY TlsImage;
    ULONG TlsSize;

    //
    // Allocate the array of thread local storage pointers
    //

        TlsImage = (PIMAGE_TLS_DIRECTORY)RtlImageDirectoryEntryToData(
                           DllBase,
                           TRUE,
                           IMAGE_DIRECTORY_ENTRY_TLS,
                           &TlsSize);


        if (!Teb)Teb = NtCurrentTeb();
        ProcessHeap = Teb->ProcessEnvironmentBlock->ProcessHeap;

        TlsVector = (PVOID *)RtlAllocateHeap(ProcessHeap,0,sizeof(PVOID)*LdrpNumberOfTlsEntries);

        if (!TlsVector) {
            return STATUS_NO_MEMORY;
        }
        //
        // NOTICE-2002/03/14-ELi
        // Zero out the new array of pointers, LdrpFreeTls frees the pointers
        // if the pointers are non-NULL
        //
        RtlZeroMemory( TlsVector, sizeof(PVOID)*LdrpNumberOfTlsEntries );

            TlsVector[*(PLONG)TlsImage->AddressOfIndex] = RtlAllocateHeap(
                                                        ProcessHeap,
                                                        0,
                                                        TlsImage->EndAddressOfRawData - TlsImage->StartAddressOfRawData
                                                        );
            if (!TlsVector[*(PLONG)TlsImage->AddressOfIndex] ) {
                return STATUS_NO_MEMORY;
            }

            //
            // Do the TLS Callouts
            //

            RtlCopyMemory (
                TlsVector[*(PLONG)TlsImage->AddressOfIndex],
                (PVOID)TlsImage->StartAddressOfRawData,
                TlsImage->EndAddressOfRawData - TlsImage->StartAddressOfRawData
            );
            RtlCopyMemory(
                TlsVector,
                (PVOID)Teb->ThreadLocalStoragePointer,
                sizeof(PVOID) * (LdrpNumberOfTlsEntries - 1)
                );

        Teb->ThreadLocalStoragePointer = TlsVector;
    return STATUS_SUCCESS;
}

VOID
LdrpCallTlsInitializers (
    IN PVOID DllBase,
    IN ULONG Reason
    )
{
    PIMAGE_TLS_DIRECTORY TlsImage;
    ULONG TlsSize;
    PIMAGE_TLS_CALLBACK *CallBackArray;
    PIMAGE_TLS_CALLBACK InitRoutine;

    TlsImage = (PIMAGE_TLS_DIRECTORY)RtlImageDirectoryEntryToData(
                       DllBase,
                       TRUE,
                       IMAGE_DIRECTORY_ENTRY_TLS,
                       &TlsSize
                       );


    if (TlsImage) {

            CallBackArray = (PIMAGE_TLS_CALLBACK *)TlsImage->AddressOfCallBacks;
            if ( CallBackArray ) {
                while (*CallBackArray) {

                    InitRoutine = *CallBackArray++;

                    LdrpCallInitRoutine(InitRoutine,
                                        DllBase,
                                        Reason,
                                        0);
                }
            }

    }
}


PVOID NTAPI TlsQuerySystemInformation(_In_ SYSTEM_INFORMATION_CLASS SystemInformationClass, _Out_opt_ LPDWORD ReturnLength)
{

    if (ReturnLength)
        *ReturnLength = 0;

    NTSTATUS status;
    PVOID buffer = nullptr;
    ULONG len = 0;

    do {

        RtlFreeHeap(RtlProcessHeap(), 0, buffer);
        buffer = nullptr;

        if (len) {
            len *= 2;
            buffer = RtlAllocateHeap(RtlProcessHeap(), 0, len);
            if (!buffer)
                return nullptr;
        }

        status = NtQuerySystemInformation(SystemInformationClass, buffer, len, &len);
        if (NT_SUCCESS(status))
            break;
    } while (status == STATUS_INFO_LENGTH_MISMATCH);

    if (ReturnLength)
        *ReturnLength = len;
    return buffer;
}

VOID TlsFixInAllThread(PVOID DllBase)
{

    auto pid = NtCurrentTeb()->ClientId.UniqueProcess;
    auto p_system_process_info = PSYSTEM_PROCESS_INFORMATION(TlsQuerySystemInformation(SystemProcessInformation, nullptr));
    if (!p_system_process_info) {
        return;
    }

    auto p_process_info = p_system_process_info;
    while (true) {
        if (p_process_info->UniqueProcessId == pid) {
            OBJECT_ATTRIBUTES oa{};
            InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
            THREAD_BASIC_INFORMATION tbi{};
            NTSTATUS status = 0;
            PSYSTEM_THREAD_INFORMATION ThreadInfo = (PSYSTEM_THREAD_INFORMATION)(p_process_info + 1);
            for (ULONG i = 0; i < p_process_info->NumberOfThreads; ++i) {
                HANDLE hThread = nullptr;
                status = NtOpenThread(&hThread, THREAD_QUERY_INFORMATION, &oa, &ThreadInfo[i].ClientId);

                if (NT_SUCCESS(status)) {
                    status = NtQueryInformationThread(hThread, ThreadBasicInformation, &tbi, sizeof(tbi), nullptr);
                    if (NT_SUCCESS(status) && !!tbi.TebBaseAddress->ThreadLocalStoragePointer) {
                        LdrpAllocateTls(DllBase, tbi.TebBaseAddress);
                    }

                    CloseHandle(hThread);
                }
            }

            break;
        }

        if (!p_process_info->NextEntryOffset)
            break;
        p_process_info = PSYSTEM_PROCESS_INFORMATION(LPSTR(p_process_info) + p_process_info->NextEntryOffset);
    }

    RtlFreeHeap(RtlProcessHeap(), 0, p_system_process_info);
}

static void TLSInit_DllMain_ThreadAttach(PVOID DllBase)
{
    PPEB Peb;

  if ( DllBase )
  {
    Peb = NtCurrentPeb();
    if ( Peb->OSMajorVersion < 6 )
    {
        LdrpAllocateTls (DllBase,NULL);
    }
  }
}
static void TLSInit_DllMain_ProcessAttach(PVOID DllBase)
{
    PPEB Peb;
    PIMAGE_TLS_DIRECTORY TlsImage;
    ULONG TlsSize;

  if ( DllBase )
  {
    Peb = NtCurrentPeb();
    if ( Peb->OSMajorVersion < 6 )
    {
    TlsImage = (PIMAGE_TLS_DIRECTORY)RtlImageDirectoryEntryToData(
                       DllBase,
                       TRUE,
                       IMAGE_DIRECTORY_ENTRY_TLS,
                       &TlsSize
                       );
        if (TlsImage && !*(PLONG)TlsImage->AddressOfIndex) {

            LdrpInitializeTls ();

            *(PLONG)TlsImage->AddressOfIndex = LdrpNumberOfTlsEntries++;
             LdrpAllocateTls(DllBase, NULL);

            LdrpCallTlsInitializers (DllBase, DLL_PROCESS_ATTACH);
            TlsFixInAllThread (DllBase);
        }
    }
  }
}

extern "C" BOOL WINAPI RawDllMain(HINSTANCE hModule, DWORD  dwReason, LPVOID lpReserved);

extern "C" BOOL (WINAPI* _pRawDllMain)(HINSTANCE, DWORD, LPVOID) = &RawDllMain;


/***************************************************************************\
*
* RawDllMain
*
* RawDllMain() is called after the before CRT has fully ininitialized.
*
\***************************************************************************/

extern "C"
BOOL WINAPI
RawDllMain(HINSTANCE hModule, DWORD  dwReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(lpReserved);
    
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        TLSInit_DllMain_ProcessAttach (hModule);
        break;
        
    case DLL_PROCESS_DETACH:
        break;

    case DLL_THREAD_ATTACH:
        TLSInit_DllMain_ThreadAttach (hModule);
        break;

    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            if (angle::GetEnvironmentVar("ANGLE_WAIT_FOR_DEBUGGER") == "1")
            {
                WaitForDebugger(instance);
            }
            return static_cast<BOOL>(egl::InitializeProcess());

        case DLL_THREAD_ATTACH:
            return static_cast<BOOL>(egl::AllocateCurrentThread() != nullptr);

        case DLL_THREAD_DETACH:
            egl::DeallocateCurrentThread();
            break;

        case DLL_PROCESS_DETACH:
            egl::TerminateProcess();
            break;
    }

    return TRUE;
}
#endif  // defined(ANGLE_PLATFORM_WINDOWS) && !defined(ANGLE_STATIC)

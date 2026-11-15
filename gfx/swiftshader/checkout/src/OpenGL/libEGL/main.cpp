// Copyright 2016 The SwiftShader Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// main.cpp: DLL entry point and management of thread-local data.

#include "main.h"

#include "Context.hpp"
#include "Display.h"
#include "Surface.hpp"
#include "libEGL.hpp"

#include "Common/SharedLibrary.hpp"
#include "Common/Thread.hpp"
#include "common/debug.h"
#include "resource.h"

#include <EGL/eglext.h>

static sw::Thread::LocalStorageKey currentTLS = TLS_OUT_OF_INDEXES;

#if !defined(_MSC_VER)
#define CONSTRUCTOR __attribute__((constructor))
#define DESTRUCTOR __attribute__((destructor))
#else
#define CONSTRUCTOR
#define DESTRUCTOR
#endif

namespace egl
{
void releaseCurrent(void *storage)
{
	// This pthread destructor is called after the TLS is already reset to NULL,
	// so we can't call EGL functions here to do the cleanup.

	Current *current = (Current*)storage;

	if(current)
	{
		if(current->drawSurface)
		{
			current->drawSurface->release();
		}

		if(current->readSurface)
		{
			current->readSurface->release();
		}

		if(current->context)
		{
			current->context->release();
		}

		free(current);
	}
}

Current *attachThread()
{
	TRACE("()");

	if(currentTLS == TLS_OUT_OF_INDEXES)
	{
		currentTLS = sw::Thread::allocateLocalStorageKey(releaseCurrent);
	}

	Current *current = (Current*)sw::Thread::allocateLocalStorage(currentTLS, sizeof(Current));

	current->error = EGL_SUCCESS;
	current->API = EGL_OPENGL_ES_API;
	current->context = nullptr;
	current->drawSurface = nullptr;
	current->readSurface = nullptr;

	return current;
}

void detachThread()
{
	TRACE("()");

	eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_CONTEXT, EGL_NO_SURFACE, EGL_NO_SURFACE);

	sw::Thread::freeLocalStorage(currentTLS);
}

CONSTRUCTOR void attachProcess()
{
	TRACE("()");

	#if !defined(ANGLE_DISABLE_TRACE) && defined(TRACE_OUTPUT_FILE)
		FILE *debug = fopen(TRACE_OUTPUT_FILE, "rt");

		if(debug)
		{
			fclose(debug);
			debug = fopen(TRACE_OUTPUT_FILE, "wt");   // Erase
			fclose(debug);
		}
	#endif

	attachThread();
}

DESTRUCTOR void detachProcess()
{
	TRACE("()");

	detachThread();
	sw::Thread::freeLocalStorageKey(currentTLS);
}
}

#if defined(_WIN32)
#ifdef DEBUGGER_WAIT_DIALOG
static INT_PTR CALLBACK DebuggerWaitDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	RECT rect;

	switch(uMsg)
	{
	case WM_INITDIALOG:
		GetWindowRect(GetDesktopWindow(), &rect);
		SetWindowPos(hwnd, HWND_TOP, rect.right / 2, rect.bottom / 2, 0, 0, SWP_NOSIZE);
		SetTimer(hwnd, 1, 100, NULL);
		return TRUE;
	case WM_COMMAND:
		if(LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hwnd, 0);
		}
		break;
	case WM_TIMER:
		if(IsDebuggerPresent())
		{
			EndDialog(hwnd, 0);
		}
	}

	return FALSE;
}

static void WaitForDebugger(HINSTANCE instance)
{
	if(!IsDebuggerPresent())
	{
		HRSRC dialog = FindResource(instance, MAKEINTRESOURCE(IDD_DIALOG1), RT_DIALOG);
		DLGTEMPLATE *dialogTemplate = (DLGTEMPLATE*)LoadResource(instance, dialog);
		DialogBoxIndirect(instance, dialogTemplate, NULL, DebuggerWaitDialogProc);
	}
}
#endif

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
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	switch(reason)
	{
	case DLL_PROCESS_ATTACH:
		#ifdef DEBUGGER_WAIT_DIALOG
		{
			char disable_debugger_wait_dialog[] = "0";
			GetEnvironmentVariable("SWIFTSHADER_DISABLE_DEBUGGER_WAIT_DIALOG", disable_debugger_wait_dialog, sizeof(disable_debugger_wait_dialog));

			if(disable_debugger_wait_dialog[0] != '1')
			{
				WaitForDebugger(instance);
			}
		}
		#endif
		egl::attachProcess();
		break;
	case DLL_THREAD_ATTACH:
		egl::attachThread();
		break;
	case DLL_THREAD_DETACH:
		egl::detachThread();
		break;
	case DLL_PROCESS_DETACH:
		egl::detachProcess();
		break;
	default:
		break;
	}

	return TRUE;
}
#endif

namespace egl
{
static Current *getCurrent(void)
{
	Current *current = (Current*)sw::Thread::getLocalStorage(currentTLS);

	if(!current)
	{
		current = attachThread();
	}

	return current;
}

void setCurrentError(EGLint error)
{
	Current *current = getCurrent();

	current->error = error;
}

EGLint getCurrentError()
{
	Current *current = getCurrent();

	return current->error;
}

void setCurrentAPI(EGLenum API)
{
	Current *current = getCurrent();

	current->API = API;
}

EGLenum getCurrentAPI()
{
	Current *current = getCurrent();

	return current->API;
}

void setCurrentContext(egl::Context *ctx)
{
	Current *current = getCurrent();

	if(ctx)
	{
		ctx->addRef();
	}

	if(current->context)
	{
		current->context->release();
	}

	current->context = ctx;
}

NO_SANITIZE_FUNCTION egl::Context *getCurrentContext()
{
	Current *current = getCurrent();

	return current->context;
}

void setCurrentDrawSurface(egl::Surface *surface)
{
	Current *current = getCurrent();

	if(surface)
	{
		surface->addRef();
	}

	if(current->drawSurface)
	{
		current->drawSurface->release();
	}

	current->drawSurface = surface;
}

egl::Surface *getCurrentDrawSurface()
{
	Current *current = getCurrent();

	return current->drawSurface;
}

void setCurrentReadSurface(egl::Surface *surface)
{
	Current *current = getCurrent();

	if(surface)
	{
		surface->addRef();
	}

	if(current->readSurface)
	{
		current->readSurface->release();
	}

	current->readSurface = surface;
}

egl::Surface *getCurrentReadSurface()
{
	Current *current = getCurrent();

	return current->readSurface;
}

void error(EGLint errorCode)
{
	egl::setCurrentError(errorCode);

	if(errorCode != EGL_SUCCESS)
	{
		switch(errorCode)
		{
		case EGL_NOT_INITIALIZED:     TRACE("\t! Error generated: not initialized\n");     break;
		case EGL_BAD_ACCESS:          TRACE("\t! Error generated: bad access\n");          break;
		case EGL_BAD_ALLOC:           TRACE("\t! Error generated: bad alloc\n");           break;
		case EGL_BAD_ATTRIBUTE:       TRACE("\t! Error generated: bad attribute\n");       break;
		case EGL_BAD_CONFIG:          TRACE("\t! Error generated: bad config\n");          break;
		case EGL_BAD_CONTEXT:         TRACE("\t! Error generated: bad context\n");         break;
		case EGL_BAD_CURRENT_SURFACE: TRACE("\t! Error generated: bad current surface\n"); break;
		case EGL_BAD_DISPLAY:         TRACE("\t! Error generated: bad display\n");         break;
		case EGL_BAD_MATCH:           TRACE("\t! Error generated: bad match\n");           break;
		case EGL_BAD_NATIVE_PIXMAP:   TRACE("\t! Error generated: bad native pixmap\n");   break;
		case EGL_BAD_NATIVE_WINDOW:   TRACE("\t! Error generated: bad native window\n");   break;
		case EGL_BAD_PARAMETER:       TRACE("\t! Error generated: bad parameter\n");       break;
		case EGL_BAD_SURFACE:         TRACE("\t! Error generated: bad surface\n");         break;
		case EGL_CONTEXT_LOST:        TRACE("\t! Error generated: context lost\n");        break;
		default:                      TRACE("\t! Error generated: <0x%X>\n", errorCode);   break;
		}
	}
}

}

namespace egl
{
EGLint EGLAPIENTRY GetError(void);
EGLDisplay EGLAPIENTRY GetDisplay(EGLNativeDisplayType display_id);
EGLBoolean EGLAPIENTRY Initialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean EGLAPIENTRY Terminate(EGLDisplay dpy);
const char *EGLAPIENTRY QueryString(EGLDisplay dpy, EGLint name);
EGLBoolean EGLAPIENTRY GetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean EGLAPIENTRY ChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean EGLAPIENTRY GetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);
EGLSurface EGLAPIENTRY CreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType window, const EGLint *attrib_list);
EGLSurface EGLAPIENTRY CreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
EGLSurface EGLAPIENTRY CreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list);
EGLBoolean EGLAPIENTRY DestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean EGLAPIENTRY QuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);
EGLBoolean EGLAPIENTRY BindAPI(EGLenum api);
EGLenum EGLAPIENTRY QueryAPI(void);
EGLBoolean EGLAPIENTRY WaitClient(void);
EGLBoolean EGLAPIENTRY ReleaseThread(void);
EGLSurface EGLAPIENTRY CreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list);
EGLBoolean EGLAPIENTRY SurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value);
EGLBoolean EGLAPIENTRY BindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean EGLAPIENTRY ReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean EGLAPIENTRY SwapInterval(EGLDisplay dpy, EGLint interval);
EGLContext EGLAPIENTRY CreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
EGLBoolean EGLAPIENTRY DestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLBoolean EGLAPIENTRY MakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLContext EGLAPIENTRY GetCurrentContext(void);
EGLSurface EGLAPIENTRY GetCurrentSurface(EGLint readdraw);
EGLDisplay EGLAPIENTRY GetCurrentDisplay(void);
EGLBoolean EGLAPIENTRY QueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value);
EGLBoolean EGLAPIENTRY WaitGL(void);
EGLBoolean EGLAPIENTRY WaitNative(EGLint engine);
EGLBoolean EGLAPIENTRY SwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean EGLAPIENTRY CopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target);
EGLImageKHR EGLAPIENTRY CreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
EGLImageKHR EGLAPIENTRY CreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list);
EGLBoolean EGLAPIENTRY DestroyImageKHR(EGLDisplay dpy, EGLImageKHR image);
EGLDisplay EGLAPIENTRY GetPlatformDisplayEXT(EGLenum platform, void *native_display, const EGLint *attrib_list);
EGLDisplay EGLAPIENTRY GetPlatformDisplay(EGLenum platform, void *native_display, const EGLAttrib *attrib_list);
EGLSurface EGLAPIENTRY CreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native_window, const EGLint *attrib_list);
EGLSurface EGLAPIENTRY CreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void *native_window, const EGLAttrib *attrib_list);
EGLSurface EGLAPIENTRY CreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLint *attrib_list);
EGLSurface EGLAPIENTRY CreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLAttrib *attrib_list);
EGLSyncKHR EGLAPIENTRY CreateSyncKHR(EGLDisplay dpy, EGLenum type, const EGLint *attrib_list);
EGLSyncKHR EGLAPIENTRY CreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list);
EGLBoolean EGLAPIENTRY DestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync);
EGLint EGLAPIENTRY ClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout);
EGLBoolean EGLAPIENTRY GetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint attribute, EGLint *value);
EGLBoolean EGLAPIENTRY GetSyncAttrib(EGLDisplay dpy, EGLSyncKHR sync, EGLint attribute, EGLAttrib *value);
__eglMustCastToProperFunctionPointerType EGLAPIENTRY GetProcAddress(const char *procname);
}

extern "C"
{
EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
	return egl::GetError();
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id)
{
	return egl::GetDisplay(display_id);
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	return egl::Initialize(dpy, major, minor);
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
	return egl::Terminate(dpy);
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
	return egl::QueryString(dpy, name);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	return egl::GetConfigs(dpy, configs, config_size, num_config);
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	return egl::ChooseConfig(dpy, attrib_list, configs, config_size, num_config);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
	return egl::GetConfigAttrib(dpy, config, attribute, value);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType window, const EGLint *attrib_list)
{
	return egl::CreateWindowSurface(dpy, config, window, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list)
{
	return egl::CreatePbufferSurface(dpy, config, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list)
{
	return egl::CreatePixmapSurface(dpy, config, pixmap, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
	return egl::DestroySurface(dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
	return egl::QuerySurface(dpy, surface, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
	return egl::BindAPI(api);
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
	return egl::QueryAPI();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient(void)
{
	return egl::WaitClient();
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
	return egl::ReleaseThread();
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
	return egl::CreatePbufferFromClientBuffer(dpy, buftype, buffer, config, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
	return egl::SurfaceAttrib(dpy, surface, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	return egl::BindTexImage(dpy, surface, buffer);
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	return egl::ReleaseTexImage(dpy, surface, buffer);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
	return egl::SwapInterval(dpy, interval);
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
	return egl::CreateContext(dpy, config, share_context, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
	return egl::DestroyContext(dpy, ctx);
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
	return egl::MakeCurrent(dpy, draw, read, ctx);
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
	return egl::GetCurrentContext();
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
	return egl::GetCurrentSurface(readdraw);
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
	return egl::GetCurrentDisplay();
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
	return egl::QueryContext(dpy, ctx, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void)
{
	return egl::WaitClient();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine)
{
	return egl::WaitNative(engine);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
	return egl::SwapBuffers(dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{
	return egl::CopyBuffers(dpy, surface, target);
}

EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
	return egl::CreateImageKHR(dpy, ctx, target, buffer, attrib_list);
}

EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list)
{
	return egl::CreateImage(dpy, ctx, target, buffer, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image)
{
	return egl::DestroyImageKHR(dpy, image);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImage(EGLDisplay dpy, EGLImageKHR image)
{
	return egl::DestroyImageKHR(dpy, image);
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplayEXT(EGLenum platform, void *native_display, const EGLint *attrib_list)
{
	return egl::GetPlatformDisplayEXT(platform, native_display, attrib_list);
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplay(EGLenum platform, void *native_display, const EGLAttrib *attrib_list)
{
	return egl::GetPlatformDisplay(platform, native_display, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native_window, const EGLint *attrib_list)
{
	return egl::CreatePlatformWindowSurfaceEXT(dpy, config, native_window, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void *native_window, const EGLAttrib *attrib_list)
{
	return egl::CreatePlatformWindowSurface(dpy, config, native_window, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLint *attrib_list)
{
	return egl::CreatePlatformPixmapSurfaceEXT(dpy, config, native_pixmap, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLAttrib *attrib_list)
{
	return egl::CreatePlatformPixmapSurface(dpy, config, native_pixmap, attrib_list);
}

EGLAPI EGLSyncKHR EGLAPIENTRY eglCreateSyncKHR(EGLDisplay dpy, EGLenum type, const EGLint *attrib_list)
{
	return egl::CreateSyncKHR(dpy, type, attrib_list);
}

EGLAPI EGLSyncKHR EGLAPIENTRY eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list)
{
	return egl::CreateSync(dpy, type, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync)
{
	return egl::DestroySyncKHR(dpy, sync);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySync(EGLDisplay dpy, EGLSyncKHR sync)
{
	return egl::DestroySyncKHR(dpy, sync);
}

EGLAPI EGLint EGLAPIENTRY eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout)
{
	return egl::ClientWaitSyncKHR(dpy, sync, flags, timeout);
}

EGLAPI EGLint EGLAPIENTRY eglClientWaitSync(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout)
{
	return egl::ClientWaitSyncKHR(dpy, sync, flags, timeout);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint attribute, EGLint *value)
{
	return egl::GetSyncAttribKHR(dpy, sync, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetSyncAttrib(EGLDisplay dpy, EGLSyncKHR sync, EGLint attribute, EGLAttrib *value)
{
	return egl::GetSyncAttrib(dpy, sync, attribute, value);
}

EGLAPI EGLint EGLAPIENTRY eglWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags)
{
	return egl::ClientWaitSyncKHR(dpy, sync, flags, EGL_FOREVER_KHR);
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags)
{
	return egl::ClientWaitSyncKHR(dpy, sync, flags, EGL_FOREVER_KHR);
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *procname)
{
	return egl::GetProcAddress(procname);
}
}

LibEGLexports::LibEGLexports()
{
	this->eglGetError = egl::GetError;
	this->eglGetDisplay = egl::GetDisplay;
	this->eglInitialize = egl::Initialize;
	this->eglTerminate = egl::Terminate;
	this->eglQueryString = egl::QueryString;
	this->eglGetConfigs = egl::GetConfigs;
	this->eglChooseConfig = egl::ChooseConfig;
	this->eglGetConfigAttrib = egl::GetConfigAttrib;
	this->eglCreateWindowSurface = egl::CreateWindowSurface;
	this->eglCreatePbufferSurface = egl::CreatePbufferSurface;
	this->eglCreatePixmapSurface = egl::CreatePixmapSurface;
	this->eglDestroySurface = egl::DestroySurface;
	this->eglQuerySurface = egl::QuerySurface;
	this->eglBindAPI = egl::BindAPI;
	this->eglQueryAPI = egl::QueryAPI;
	this->eglWaitClient = egl::WaitClient;
	this->eglReleaseThread = egl::ReleaseThread;
	this->eglCreatePbufferFromClientBuffer = egl::CreatePbufferFromClientBuffer;
	this->eglSurfaceAttrib = egl::SurfaceAttrib;
	this->eglBindTexImage = egl::BindTexImage;
	this->eglReleaseTexImage = egl::ReleaseTexImage;
	this->eglSwapInterval = egl::SwapInterval;
	this->eglCreateContext = egl::CreateContext;
	this->eglDestroyContext = egl::DestroyContext;
	this->eglMakeCurrent = egl::MakeCurrent;
	this->eglGetCurrentContext = egl::GetCurrentContext;
	this->eglGetCurrentSurface = egl::GetCurrentSurface;
	this->eglGetCurrentDisplay = egl::GetCurrentDisplay;
	this->eglQueryContext = egl::QueryContext;
	this->eglWaitGL = egl::WaitGL;
	this->eglWaitNative = egl::WaitNative;
	this->eglSwapBuffers = egl::SwapBuffers;
	this->eglCopyBuffers = egl::CopyBuffers;
	this->eglCreateImageKHR = egl::CreateImageKHR;
	this->eglDestroyImageKHR = egl::DestroyImageKHR;
	this->eglGetProcAddress = egl::GetProcAddress;
	this->eglCreateSyncKHR = egl::CreateSyncKHR;
	this->eglDestroySyncKHR = egl::DestroySyncKHR;
	this->eglClientWaitSyncKHR = egl::ClientWaitSyncKHR;
	this->eglGetSyncAttribKHR = egl::GetSyncAttribKHR;

	this->clientGetCurrentContext = egl::getCurrentContext;
}

extern "C" EGLAPI LibEGLexports *libEGL_swiftshader()
{
	static LibEGLexports libEGL;
	return &libEGL;
}

LibGLESv2 libGLESv2;

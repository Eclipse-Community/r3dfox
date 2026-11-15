#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include "EnlyzeWinCompatLibInternal.h"

#pragma comment(lib, "ntdll.lib")
extern "C" DECLSPEC_IMPORT NTSTATUS WINAPI NtWaitForKeyedEvent(HANDLE, const void*, BOOLEAN, const LARGE_INTEGER*);
extern "C" DECLSPEC_IMPORT NTSTATUS WINAPI NtReleaseKeyedEvent(HANDLE, const void*, BOOLEAN, const LARGE_INTEGER*);
/******************************************************************
 *              RtlRunOnceBeginInitialize (NTDLL.@)
 */
DWORD WINAPI RtlRunOnceBeginInitialize( INIT_ONCE *once, ULONG flags, void **context )
{
    if (flags & RTL_RUN_ONCE_CHECK_ONLY)
    {
        ULONG_PTR val = (ULONG_PTR)( once->Ptr );

        if (flags & RTL_RUN_ONCE_ASYNC) return STATUS_INVALID_PARAMETER;
        if ((val & 3) != 2) return STATUS_UNSUCCESSFUL;
        if (context) *context = (void *)(val & ~3);
        return STATUS_SUCCESS;
    }

    for (;;)
    {
        ULONG_PTR next, val = (ULONG_PTR)( once->Ptr );

        switch (val & 3)
        {
        case 0:  /* first time */
            if (!InterlockedCompareExchangePointer( &once->Ptr,
                                                    (flags & RTL_RUN_ONCE_ASYNC) ? (void *)3 : (void *)1, 0 ))
                return STATUS_PENDING;
            break;

        case 1:  /* in progress, wait */
            if (flags & RTL_RUN_ONCE_ASYNC) return STATUS_INVALID_PARAMETER;
            next = val & ~3;
            if (InterlockedCompareExchangePointer( &once->Ptr, (void *)((ULONG_PTR)&next | 1),
                                                   (void *)val ) == (void *)val)
                NtWaitForKeyedEvent( 0, &next, FALSE, NULL );
            break;

        case 2:  /* done */
            if (context) *context = (void *)(val & ~3);
            return STATUS_SUCCESS;

        case 3:  /* in progress, async */
            if (!(flags & RTL_RUN_ONCE_ASYNC)) return STATUS_INVALID_PARAMETER;
            return STATUS_PENDING;
        }
    }
}

/******************************************************************
 *              RtlRunOnceComplete (NTDLL.@)
 */
DWORD WINAPI RtlRunOnceComplete( INIT_ONCE *once, ULONG flags, void *context )
{
    if ((ULONG_PTR)context & 3) return STATUS_INVALID_PARAMETER;

    if (flags & RTL_RUN_ONCE_INIT_FAILED)
    {
        if (context) return STATUS_INVALID_PARAMETER;
        if (flags & RTL_RUN_ONCE_ASYNC) return STATUS_INVALID_PARAMETER;
    }
    else context = (void *)((ULONG_PTR)context | 2);

    for (;;)
    {
        ULONG_PTR val = (ULONG_PTR)( once->Ptr );

        switch (val & 3)
        {
        case 1:  /* in progress */
            if (InterlockedCompareExchangePointer( &once->Ptr, context, (void *)val ) != (void *)val) break;
            val &= ~3;
            while (val)
            {
                ULONG_PTR next = *(ULONG_PTR *)val;
                NtReleaseKeyedEvent( 0, (void *)val, FALSE, NULL );
                val = next;
            }
            return STATUS_SUCCESS;

        case 3:  /* in progress, async */
            if (!(flags & RTL_RUN_ONCE_ASYNC)) return STATUS_INVALID_PARAMETER;
            if (InterlockedCompareExchangePointer( &once->Ptr, context, (void *)val ) != (void *)val) break;
            return STATUS_SUCCESS;

        default:
            return STATUS_UNSUCCESSFUL;
        }
    }
}


extern "C" BOOL WINAPI LibInitOnceBeginInitialize(
  LPINIT_ONCE lpInitOnce,
  DWORD       dwFlags,
  PBOOL       fPending,
  LPVOID      *lpContext
)
{
    DWORD ret = RtlRunOnceBeginInitialize( lpInitOnce, dwFlags, lpContext );

    *fPending = FALSE;
    if (ret == STATUS_SUCCESS) return TRUE;

    if (ret == STATUS_PENDING)
    {
        *fPending = TRUE;
        return TRUE;
    }

    return FALSE;
}

extern "C" BOOL WINAPI LibInitOnceComplete(
  LPINIT_ONCE lpInitOnce,
  DWORD       dwFlags,
  LPVOID      lpContext
){
    return !RtlRunOnceComplete( lpInitOnce, dwFlags, lpContext );
}

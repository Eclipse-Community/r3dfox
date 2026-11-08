;
; EnlyzeWinCompatLib - Synchronization & FLS Assembly Wrappers (x86 / stdcall)
; Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
; SPDX-License-Identifier: MIT
;

.model flat

EXTERN _LibInitOnceBeginInitialize@16 : PROC
EXTERN _LibInitOnceComplete@12 : PROC

.code

PUBLIC _InitOnceBeginInitialize@16
_InitOnceBeginInitialize@16 PROC
    jmp _LibInitOnceBeginInitialize@16
_InitOnceBeginInitialize@16 ENDP

PUBLIC _InitOnceComplete@12
_InitOnceComplete@12 PROC
    jmp _LibInitOnceComplete@12
_InitOnceComplete@12 ENDP

CONST SEGMENT

PUBLIC __imp__InitOnceBeginInitialize@16
__imp__InitOnceBeginInitialize@16 dd _LibInitOnceBeginInitialize@16

PUBLIC __imp__InitOnceComplete@12
__imp__InitOnceComplete@12 dd _LibInitOnceComplete@12

CONST ENDS

END

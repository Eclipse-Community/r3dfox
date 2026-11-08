;
; EnlyzeWinCompatLib - Synchronization & FLS Assembly Wrappers (x64)
; Copyright (c) 2021 Colin Finck, ENLYZE GmbH <c.finck@enlyze.com>
; SPDX-License-Identifier: MIT
;

OPTION CASEMAP:NONE

EXTERN LibInitOnceBeginInitialize : PROC
EXTERN LibInitOnceComplete : PROC

.code

PUBLIC InitOnceBeginInitialize
InitOnceBeginInitialize PROC
    jmp LibInitOnceBeginInitialize
InitOnceBeginInitialize ENDP

PUBLIC InitOnceComplete
InitOnceComplete PROC
    jmp LibInitOnceComplete
InitOnceComplete ENDP

.const
ALIGN 8

PUBLIC __imp_InitOnceBeginInitialize
__imp_InitOnceBeginInitialize dq LibInitOnceBeginInitialize

PUBLIC __imp_InitOnceComplete
__imp_InitOnceComplete dq LibInitOnceComplete

CONST ENDS

END

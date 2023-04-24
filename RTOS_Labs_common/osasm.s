;/*****************************************************************************/
;/* OSasm.s: low-level OS commands, written in assembly                       */
;/* derived from uCOS-II                                                      */
;/*****************************************************************************/
;Jonathan Valvano, OS Lab2/3/4/5, 1/12/20
;Students will implement these functions as part of EE445M/EE380L.12 Lab

        AREA |.text|, CODE, READONLY, ALIGN=2
        THUMB
        REQUIRE8
        PRESERVE8

        EXTERN  OS          ; Struct of type OS_DATA Relevently Contains:
                            ; MEM[OS+0] Currently running thread
                            ; MEM[OS+4] Jump thread

        EXPORT  StartOS
        EXPORT  ContextSwitch
        EXPORT  PendSV_Handler
        EXPORT  SVC_Handler

NVIC_INT_CTRL    EQU     0xE000ED04                              ; Interrupt control state register.
NVIC_SYSPRI14    EQU     0xE000ED22                              ; PendSV priority register (position 14).
NVIC_SYSPRI15    EQU     0xE000ED23                              ; Systick priority register (position 15).
NVIC_LEVEL14     EQU           0xEF                              ; Systick priority value (second lowest).
NVIC_LEVEL15     EQU           0xFF                              ; PendSV priority value (lowest).
NVIC_PENDSVSET   EQU     0x10000000                              ; Value to trigger PendSV exception.
NVIC_UNPENDSVSET EQU     0x08000000                              ; Value to disarm  PendSV exception.
EXE_RET_HANDLER  EQU     0xFFFFFFF1                              ; Non FP, Return to handler, Use MSP 
EXE_RET_THREAD   EQU     0xFFFFFFF9                              ; Non FP, Return to thread, Use MSP 
    
StartOS
    CPSID   I                       ; Disable interupts
    LDR     R0, =OS                 ; R0 = pointer to OS_DATA:{ TCB_RunPt, TCB_Jump ... }
    LDR     R1, [R0]                ; R1 = TCB_RunPt
    
    ;Load SP from TCB, do not modify given TCB
    LDR     SP, [R1, #4]            ; Changes with TCB!, assumes that SP is 2nd element of TCB!
    
    ;Pop values of stack to start execution
    POP     {R4-R11}            ; restore manually pushed registers in order
    POP     {R0-R3}             ; restore ISR Registers (4/8)
    POP     {R12}               ; restore ISR Registers (5/8)
    POP     {LR}                ; discard initial LR
    POP     {LR}                ; Load new thread's PC into LR
    POP     {R1}                ; discard PSR
    
    ;Re-enable Interupts and jump to thread
    CPSIE   I                   ; Enable interrupts at processor level
    BX      LR                  ; start first thread
    
OSStartHang
    B       OSStartHang        ; Should never get here

;********************************************************************************************************
;                               PERFORM A CONTEXT SWITCH (From task level)
;                                           void ContextSwitch(void)
;
; Note(s) : 1) ContextSwitch() is called when OS wants to perform a task context switch.  This function
;              triggers the PendSV exception which is where the real work is done.
;********************************************************************************************************

ContextSwitch
    LDR     R0, =NVIC_INT_CTRL  ; R0 = 0xE000ED04 
    LDR     R1, =NVIC_PENDSVSET ; R2 = 0x10000000
    STR     R1, [R0]            ; Activate PendSV, since Memory map states writes of 0 have no impact, this should be safe
    BX      LR
    
;********************************************************************************************************
;                                         HANDLE PendSV EXCEPTION
;                                     void OS_CPU_PendSVHandler(void)
;
; Note(s) : 1) PendSV is used to cause a context switch.  This is a recommended method for performing
;              context switches with Cortex-M.  This is because the Cortex-M3 auto-saves half of the
;              processor context on any exception, and restores same on return from exception.  So only
;              saving of R4-R11 is required and fixing up the stack pointers.  Using the PendSV exception
;              this way means that context saving and restoring is identical whether it is initiated from
;              a thread or occurs due to an interrupt or exception.
;
;           2) Pseudo-code is:
;              a) Get the process SP, if 0 then skip (goto d) the saving part (first context switch);
;              b) Save remaining regs r4-r11 on process stack;
;              c) Save the process SP in its TCB, OSTCBCur->OSTCBStkPtr = SP;
;              d) Call OSTaskSwHook();
;              e) Get current high priority, OSPrioCur = OSPrioHighRdy;
;              f) Get current ready thread TCB, OSTCBCur = OSTCBHighRdy;
;              g) Get new process SP from TCB, SP = OSTCBHighRdy->OSTCBStkPtr;
;              h) Restore R4-R11 from new process stack;
;              i) Perform exception return which will restore remaining context.
;
;           3) On entry into PendSV handler:
;              a) The following have been saved on the process stack (by processor):
;                 xPSR, PC, LR, R12, R0-R3
;              b) Processor mode is switched to Handler mode (from Thread mode)
;              c) Stack is Main stack (switched from Process stack)
;              d) OSTCBCur      points to the OS_TCB of the task to suspend
;                 OSTCBHighRdy  points to the OS_TCB of the task to resume
;
;           4) Since PendSV is set to lowest priority in the system (by OSStartHighRdy() above), we
;              know that it will only be run when no other exception or interrupt is active, and
;              therefore safe to assume that context being switched out was using the process stack (PSP).
;********************************************************************************************************

PendSV_Handler
    CPSID   I
    ;Acknowlege pendsv interupt
    LDR     R0, =NVIC_INT_CTRL    ; R0 = 0xE000ED04 
    LDR     R1, =NVIC_UNPENDSVSET ; R2 = 0x08000000 
    STR     R1, [R0]              ; Acknowlege interupt
    
    ;Push Data to the stack that was not pushed by interupt handler to the stack
    PUSH    {R4-R11}            ; Stack now looks like { R4-R11, R0-R3, R12, LR(R14), PC(R15), PSR } from interupt
    
    ;Get TCB_RunPT and TCB_Jump. Save stack pointer then determine if jump os required
    LDR     R0, =OS             ; R0 = pointer to OS_DATA:{ TCB_RunPt, TCB_Jump ... }
    LDR     R1, [R0]            ; R1 = TCB_RunPt = { *TCB_Next, curr_stack_ptr ... }
    LDR     R2, [R0, #4]        ; R2 = TCB_Jump
    STR     SP, [R1, #4]        ; TCB_RunPt->curr_stack_ptr = SP 
    CMP     R2, #0              ; Check if need to do a TCB JMP
    BEQ     Normal_CTX          ; Either Continue with a normal CTX or do a JMP
    
    ; If JMP: Clear the JMP, update TCB_RunPt,
    MOV     R1, #0
    STR     R1, [R0, #4]        ; TCB_Jump = 0
    STR     R2, [R0]            ; TCB_RunPt = TCB_Jump
    LDR     SP, [R2, #4]        ; Changes with TCB!, assumes that SP is 2nd element of TCB!
    POP     {R4-R11}            ; Load new Context using SP, but not the remaining 8 ISR registers
    CPSIE   I                   ; Re-enable Interupts and jump to thread
    BX      LR                  ; Start/Continue Thread
    
    
Normal_CTX                      ; If we are a normal context switch, 
    LDR     R1, [R1]            ; R1 = TCB_RunPt->next_tcb
    STR     R1, [R0]            ; TCB_RunPt = R1    
    LDR     SP, [R1, #4]        ; Changes with TCB!, assumes that SP is 2nd element of TCB!
    POP     {R4-R11}            ; Load new Context using SP, but not the remaining 8 ISR registers
    CPSIE   I                   ; Re-enable Interupts and jump to thread
    BX      LR                  ; Start/Continue Thread
    

;********************************************************************************************************
;                                         HANDLE SVC EXCEPTION
;                                     void OS_CPU_SVCHandler(void)
;
; Note(s) : SVC is a software-triggered exception to make OS kernel calls from user land. 
;           The function ID to call is encoded in the instruction itself, the location of which can be
;           found relative to the return address saved on the stack on exception entry.
;           Function-call paramters in R0..R3 are also auto-saved on stack on exception entry.
;********************************************************************************************************

        IMPORT    OS_Id
        IMPORT    OS_Kill
        IMPORT    OS_Sleep
        IMPORT    OS_Time
        IMPORT    OS_AddThread

SVC_Handler
; put your Lab 5 code here


    BX      LR                   ; Return from exception



    ALIGN
    END

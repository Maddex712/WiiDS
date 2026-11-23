.text
.p2align 2
.global XdContext_Swap
.type XdContext_Swap,%function
XdContext_Swap:
    stp x19,x20,[x0,#0]
    stp x21,x22,[x0,#16]
    stp x23,x24,[x0,#32]
    stp x25,x26,[x0,#48]
    stp x27,x28,[x0,#64]
    stp x29,x30,[x0,#80]
    mov x9,sp
    str x9,[x0,#96]
    mrs x9,fpcr
    str w9,[x0,#104]
    mrs x9,fpsr
    str w9,[x0,#108]
    stp d8,d9,[x0,#112]
    stp d10,d11,[x0,#128]
    stp d12,d13,[x0,#144]
    stp d14,d15,[x0,#160]
    ldp x19,x20,[x1,#0]
    ldp x21,x22,[x1,#16]
    ldp x23,x24,[x1,#32]
    ldp x25,x26,[x1,#48]
    ldp x27,x28,[x1,#64]
    ldp x29,x30,[x1,#80]
    ldr x9,[x1,#96]
    mov sp,x9
    ldr w9,[x1,#104]
    msr fpcr,x9
    ldr w9,[x1,#108]
    msr fpsr,x9
    ldp d8,d9,[x1,#112]
    ldp d10,d11,[x1,#128]
    ldp d12,d13,[x1,#144]
    ldp d14,d15,[x1,#160]
    mov w0,#0
    ret
.size XdContext_Swap,.-XdContext_Swap
.global XdContext_Start
.type XdContext_Start,%function
XdContext_Start:
    mov x0,x19
    b XdContext_Run
.section .note.GNU-stack,"",%progbits

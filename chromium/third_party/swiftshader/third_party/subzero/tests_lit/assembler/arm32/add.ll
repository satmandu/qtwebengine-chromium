; Show that we know how to translate add.

; NOTE: We use -O2 to get rid of memory stores.

; REQUIRES: allow_dump

; Compile using standalone assembler.
; RUN: %p2i --filetype=asm -i %s --target=arm32 --args -O2 \
; RUN:   | FileCheck %s --check-prefix=ASM

; Show bytes in assembled standalone code.
; RUN: %p2i --filetype=asm -i %s --target=arm32 --assemble --disassemble \
; RUN:   --args -O2 | FileCheck %s --check-prefix=DIS

; Compile using integrated assembler.
; RUN: %p2i --filetype=iasm -i %s --target=arm32 --args -O2 \
; RUN:   | FileCheck %s --check-prefix=IASM

; Show bytes in assembled integrated code.
; RUN: %p2i --filetype=iasm -i %s --target=arm32 --assemble --disassemble \
; RUN:   --args -O2 | FileCheck %s --check-prefix=DIS

define internal i32 @add1ToR0(i32 %p) {
  %v = add i32 %p, 1
  ret i32 %v
}

; ASM-LABEL: add1ToR0:
; ASM-NEXT:  .Ladd1ToR0$__0:
; ASM-NEXT:     add     r0, r0, #1
; ASM-NEXT:     bx      lr

; DIS-LABEL:00000000 <add1ToR0>:
; DIS-NEXT:   0:        e2800001
; DIS-NEXT:   4:        e12fff1e

; IASM-LABEL: add1ToR0:
; IASM-LABEL: .Ladd1ToR0$__0:
; IASM-NEXT:    .byte 0x1
; IASM-NEXT:    .byte 0x0
; IASM-NEXT:    .byte 0x80
; IASM-NEXT:    .byte 0xe2

; IASM-NEXT:    .byte 0x1e
; IASM-NEXT:    .byte 0xff
; IASM-NEXT:    .byte 0x2f
; IASM-NEXT:    .byte 0xe1

define internal i32 @Add2Regs(i32 %p1, i32 %p2) {
  %v = add i32 %p1, %p2
  ret i32 %v
}

; ASM-LABEL: Add2Regs:
; ASM-NEXT:  .LAdd2Regs$__0:
; ASM-NEXT:     add r0, r0, r1
; ASM-NEXT:     bx lr

; DIS-LABEL:00000010 <Add2Regs>:
; DIS-NEXT:  10:        e0800001
; DIS-NEXT:  14:        e12fff1e

; IASM-LABEL: Add2Regs:
; IASM-NEXT:  .LAdd2Regs$__0:
; IASM-NEXT:    .byte 0x1
; IASM-NEXT:    .byte 0x0
; IASM-NEXT:    .byte 0x80
; IASM-NEXT:    .byte 0xe0

; IASM-NEXT:    .byte 0x1e
; IASM-NEXT:    .byte 0xff
; IASM-NEXT:    .byte 0x2f
; IASM-NEXT:    .byte 0xe1

define internal i64 @addI64ToR0R1(i64 %p) {
  %v = add i64 %p, 1
  ret i64 %v
}

; ASM-LABEL:addI64ToR0R1:
; ASM-NEXT:.LaddI64ToR0R1$__0:
; ASM-NEXT:     adds    r0, r0, #1
; ASM-NEXT:     adc     r1, r1, #0

; DIS-LABEL:00000020 <addI64ToR0R1>:
; DIS-NEXT:  20:        e2900001
; DIS-NEXT:  24:        e2a11000

; IASM-LABEL:addI64ToR0R1:
; IASM-NEXT:.LaddI64ToR0R1$__0:
; IASM-NEXT:    .byte 0x1
; IASM-NEXT:    .byte 0x0
; IASM-NEXT:    .byte 0x90
; IASM-NEXT:    .byte 0xe2
; IASM-NEXT:    .byte 0x0
; IASM-NEXT:    .byte 0x10
; IASM-NEXT:    .byte 0xa1
; IASM-NEXT:    .byte 0xe2

define internal i64 @AddI64Regs(i64 %p1, i64 %p2) {
  %v = add i64 %p1, %p2
  ret i64 %v
}

; ASM-LABEL:AddI64Regs:
; ASM-NEXT:.LAddI64Regs$__0:
; ASM-NEXT:     adds    r0, r0, r2
; ASM-NEXT:     adc     r1, r1, r3

; DIS-LABEL:00000030 <AddI64Regs>:
; DIS-NEXT:  30:	e0900002
; DIS-NEXT:  34:	e0a11003

; IASM-LABEL:AddI64Regs:
; IASM-NEXT:.LAddI64Regs$__0:
; IASM-NEXT:    .byte 0x2
; IASM-NEXT:    .byte 0x0
; IASM-NEXT:    .byte 0x90
; IASM-NEXT:    .byte 0xe0
; IASM-NEXT:    .byte 0x3
; IASM-NEXT:    .byte 0x10
; IASM-NEXT:    .byte 0xa1
; IASM-NEXT:    .byte 0xe0

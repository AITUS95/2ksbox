; A boot sector that pushes from MMIO (QEMU patch 93), for test.sh's
; whpx-i386 check. VGA text memory at B800:0 is not RAM to WHPX (QEMU's
; vga-lowmem is an I/O region), so every access to it exits and runs
; through QEMU's x86 emulator, whose PUSH did nothing before patch 93: ESP
; never moved, and a guest's stack came out one slot short (Warcraft III
; bugchecked XP through our display driver's `push [regs+0xa00]`). A word
; and a dword push from B800:0, each popped back, must leave SP where it
; was and pop what a plain load reads. The verdict goes to SeaBIOS's
; debug port (0x402), the file the check reads.
;
;   nasm -f bin -o push.img tools/whpx-mmio-push.asm
;   qemu-system-i386 -accel whpx -drive file=push.img,format=raw,if=floppy ...
bits 16
org 0x7c00

    cli
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov sp, 0x7c00
    mov ax, 0xb800
    mov es, ax
    mov word [es:0], 0x1e41     ; 'A', yellow on blue
    mov word [es:2], 0x1e42     ; 'B'
    mov ebx, [es:0]             ; a plain load: what the pushes must carry

    push word [es:0]            ; 26 ff 36: the emulator's PUSH, 2 bytes
    pop ax
    cmp sp, 0x7c00
    jne bad
    cmp ax, bx
    jne bad

    push dword [es:0]           ; 66 26 ff 36: 4 bytes on a 16-bit stack
    pop eax
    cmp sp, 0x7c00
    jne bad
    cmp eax, ebx
    jne bad
    cmp ebx, 0x1e421e41
    jne bad

    mov si, ok
    jmp print
bad:
    mov si, fail
print:
    mov dx, 0x402
.next:
    lodsb
    test al, al
    jz halt
    out dx, al
    jmp .next
halt:
    hlt
    jmp halt

ok:   db "MMIO PUSH OK", 10, 0
fail: db "MMIO PUSH BAD", 10, 0

    times 510 - ($ - $$) db 0
    dw 0xaa55

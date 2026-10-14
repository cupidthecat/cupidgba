@ Sleep entrypoints share the timed byte strobe and ordinary SWI return.
.org 0x1800
.arm
Stop:
    mov r2, #0x80
CustomHalt:
    mov r12, #0x04000000
    strb r2, [r12, #0x301]
    bx lr

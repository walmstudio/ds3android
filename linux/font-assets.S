.section .rodata
#define FONT(symbol, path) .balign 16; .globl symbol; symbol: .incbin path
FONT(me1a_exo_reg, "linux/assets/Exo2-Regular.ttf")
FONT(me1a_exo_bold, "linux/assets/Exo2-SemiBold.ttf")
FONT(me1a_mono, "linux/assets/DejaVuSansMono.ttf")
FONT(me1a_sym, "linux/assets/DejaVuSans-sym.ttf")
.section .note.GNU-stack,"",%progbits

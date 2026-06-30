  .text
  .globl main
main:
  addi sp, sp, -16
  sw ra, 12(sp)
  sw s0, 8(sp)
  addi s0, sp, 16
.entry:
  li a0, 43
  j .Lmain_exit
.Lmain_exit:
  lw ra, 12(sp)
  lw s0, 8(sp)
  addi sp, sp, 16
  ret

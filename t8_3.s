  .text
  .globl f
f:
  addi sp, sp, -128
.f_entry:
  mv t0, a0
  sw t0, 0(sp)
  mv t1, a1
  sw t1, 4(sp)
  mv t2, a2
  sw t2, 8(sp)
  mv t3, a3
  sw t3, 12(sp)
  mv t4, a4
  sw t4, 16(sp)
  mv t5, a5
  sw t5, 20(sp)
  mv t6, a6
  sw t6, 24(sp)
  mv t0, a7
  sw t0, 28(sp)
  lw t1, 128(sp)
  sw t1, 32(sp)
  lw t2, 132(sp)
  sw t2, 36(sp)
  lw t3, 0(sp)
  sw t3, 40(sp)
  lw t4, 4(sp)
  sw t4, 44(sp)
  lw t5, 40(sp)
  lw t6, 44(sp)
  add t0, t5, t6
  sw t0, 48(sp)
  lw t1, 8(sp)
  sw t1, 52(sp)
  lw t2, 48(sp)
  lw t3, 52(sp)
  add t4, t2, t3
  sw t4, 56(sp)
  lw t5, 12(sp)
  sw t5, 60(sp)
  lw t6, 56(sp)
  lw t0, 60(sp)
  add t1, t6, t0
  sw t1, 64(sp)
  lw t2, 16(sp)
  sw t2, 68(sp)
  lw t3, 64(sp)
  lw t4, 68(sp)
  add t5, t3, t4
  sw t5, 72(sp)
  lw t6, 20(sp)
  sw t6, 76(sp)
  lw t0, 72(sp)
  lw t1, 76(sp)
  add t2, t0, t1
  sw t2, 80(sp)
  lw t3, 24(sp)
  sw t3, 84(sp)
  lw t4, 80(sp)
  lw t5, 84(sp)
  add t6, t4, t5
  sw t6, 88(sp)
  lw t0, 28(sp)
  sw t0, 92(sp)
  lw t1, 88(sp)
  lw t2, 92(sp)
  add t3, t1, t2
  sw t3, 96(sp)
  lw t4, 32(sp)
  sw t4, 100(sp)
  lw t5, 96(sp)
  lw t6, 100(sp)
  add t0, t5, t6
  sw t0, 104(sp)
  lw t1, 36(sp)
  sw t1, 108(sp)
  lw t2, 104(sp)
  lw t3, 108(sp)
  add t4, t2, t3
  sw t4, 112(sp)
  lw a0, 112(sp)
  j .Lf_exit
.Lf_exit:
  addi sp, sp, 128
  ret
  .text
  .globl main
main:
  addi sp, sp, -16
  sw ra, 12(sp)
.main_entry:
  li t5, 1
  mv a0, t5
  li t6, 2
  mv a1, t6
  li t0, 3
  mv a2, t0
  li t1, 4
  mv a3, t1
  li t2, 5
  mv a4, t2
  li t3, 6
  mv a5, t3
  li t4, 7
  mv a6, t4
  li t5, 8
  mv a7, t5
  li t6, 9
  li t0, 10
  addi sp, sp, -8
  sw t6, 0(sp)
  sw t0, 4(sp)
  call f
  addi sp, sp, 8
  sw a0, 0(sp)
  lw a0, 0(sp)
  j .Lmain_exit
.Lmain_exit:
  lw ra, 12(sp)
  addi sp, sp, 16
  ret

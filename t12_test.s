  .text
  .globl f1
f1:
  addi sp, sp, -208
.f1_entry:
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
  lw t1, 208(sp)
  sw t1, 32(sp)
  lw t2, 212(sp)
  sw t2, 36(sp)
  lw t3, 0(sp)
  sw t3, 40(sp)
  lw t4, 40(sp)
  li t5, 0
  slli t6, t5, 2
  add t0, t4, t6
  sw t0, 44(sp)
  lw t2, 44(sp)
  lw t1, 0(t2)
  sw t1, 48(sp)
  lw t3, 4(sp)
  sw t3, 52(sp)
  lw t4, 52(sp)
  li t5, 1
  slli t6, t5, 2
  add t0, t4, t6
  sw t0, 56(sp)
  lw t2, 56(sp)
  lw t1, 0(t2)
  sw t1, 60(sp)
  lw t3, 48(sp)
  lw t4, 60(sp)
  add t5, t3, t4
  sw t5, 64(sp)
  lw t6, 8(sp)
  sw t6, 68(sp)
  lw t0, 68(sp)
  li t1, 2
  slli t2, t1, 2
  add t3, t0, t2
  sw t3, 72(sp)
  lw t5, 72(sp)
  lw t4, 0(t5)
  sw t4, 76(sp)
  lw t6, 64(sp)
  lw t0, 76(sp)
  add t1, t6, t0
  sw t1, 80(sp)
  lw t2, 12(sp)
  sw t2, 84(sp)
  lw t3, 84(sp)
  li t4, 3
  slli t5, t4, 2
  add t6, t3, t5
  sw t6, 88(sp)
  lw t1, 88(sp)
  lw t0, 0(t1)
  sw t0, 92(sp)
  lw t2, 80(sp)
  lw t3, 92(sp)
  add t4, t2, t3
  sw t4, 96(sp)
  lw t5, 16(sp)
  sw t5, 100(sp)
  lw t6, 100(sp)
  li t0, 4
  slli t1, t0, 2
  add t2, t6, t1
  sw t2, 104(sp)
  lw t4, 104(sp)
  lw t3, 0(t4)
  sw t3, 108(sp)
  lw t5, 96(sp)
  lw t6, 108(sp)
  add t0, t5, t6
  sw t0, 112(sp)
  lw t1, 20(sp)
  sw t1, 116(sp)
  lw t2, 116(sp)
  li t3, 5
  slli t4, t3, 2
  add t5, t2, t4
  sw t5, 120(sp)
  lw t0, 120(sp)
  lw t6, 0(t0)
  sw t6, 124(sp)
  lw t1, 112(sp)
  lw t2, 124(sp)
  add t3, t1, t2
  sw t3, 128(sp)
  lw t4, 24(sp)
  sw t4, 132(sp)
  lw t5, 132(sp)
  li t6, 6
  slli t0, t6, 2
  add t1, t5, t0
  sw t1, 136(sp)
  lw t3, 136(sp)
  lw t2, 0(t3)
  sw t2, 140(sp)
  lw t4, 128(sp)
  lw t5, 140(sp)
  add t6, t4, t5
  sw t6, 144(sp)
  lw t0, 28(sp)
  sw t0, 148(sp)
  lw t1, 148(sp)
  li t2, 7
  slli t3, t2, 2
  add t4, t1, t3
  sw t4, 152(sp)
  lw t6, 152(sp)
  lw t5, 0(t6)
  sw t5, 156(sp)
  lw t0, 144(sp)
  lw t1, 156(sp)
  add t2, t0, t1
  sw t2, 160(sp)
  lw t3, 32(sp)
  sw t3, 164(sp)
  lw t4, 164(sp)
  li t5, 8
  slli t6, t5, 2
  add t0, t4, t6
  sw t0, 168(sp)
  lw t2, 168(sp)
  lw t1, 0(t2)
  sw t1, 172(sp)
  lw t3, 160(sp)
  lw t4, 172(sp)
  add t5, t3, t4
  sw t5, 176(sp)
  lw t6, 36(sp)
  sw t6, 180(sp)
  lw t0, 180(sp)
  li t1, 9
  slli t2, t1, 2
  add t3, t0, t2
  sw t3, 184(sp)
  lw t5, 184(sp)
  lw t4, 0(t5)
  sw t4, 188(sp)
  lw t6, 176(sp)
  lw t0, 188(sp)
  add t1, t6, t0
  sw t1, 192(sp)
  lw a0, 192(sp)
  j .Lf1_exit
.Lf1_exit:
  addi sp, sp, 208
  ret
  .text
  .globl f2
f2:
  addi sp, sp, -208
.f2_entry:
  mv t2, a0
  sw t2, 0(sp)
  mv t3, a1
  sw t3, 4(sp)
  mv t4, a2
  sw t4, 8(sp)
  mv t5, a3
  sw t5, 12(sp)
  mv t6, a4
  sw t6, 16(sp)
  mv t0, a5
  sw t0, 20(sp)
  mv t1, a6
  sw t1, 24(sp)
  mv t2, a7
  sw t2, 28(sp)
  lw t3, 208(sp)
  sw t3, 32(sp)
  lw t4, 212(sp)
  sw t4, 36(sp)
  lw t5, 0(sp)
  sw t5, 40(sp)
  lw t6, 40(sp)
  li t0, 0
  li t2, 40
  mul t1, t0, t2
  add t3, t6, t1
  sw t3, 44(sp)
  lw t4, 44(sp)
  li t5, 9
  slli t6, t5, 2
  add t0, t4, t6
  sw t0, 48(sp)
  lw t2, 48(sp)
  lw t1, 0(t2)
  sw t1, 52(sp)
  lw t3, 4(sp)
  sw t3, 56(sp)
  lw t4, 56(sp)
  li t5, 1
  slli t6, t5, 2
  add t0, t4, t6
  sw t0, 60(sp)
  lw t2, 60(sp)
  lw t1, 0(t2)
  sw t1, 64(sp)
  lw t3, 52(sp)
  lw t4, 64(sp)
  add t5, t3, t4
  sw t5, 68(sp)
  lw t6, 8(sp)
  sw t6, 72(sp)
  lw t0, 68(sp)
  lw t1, 72(sp)
  add t2, t0, t1
  sw t2, 76(sp)
  lw t3, 12(sp)
  sw t3, 80(sp)
  lw t4, 80(sp)
  li t5, 3
  slli t6, t5, 2
  add t0, t4, t6
  sw t0, 84(sp)
  lw t2, 84(sp)
  lw t1, 0(t2)
  sw t1, 88(sp)
  lw t3, 76(sp)
  lw t4, 88(sp)
  add t5, t3, t4
  sw t5, 92(sp)
  lw t6, 16(sp)
  sw t6, 96(sp)
  lw t0, 96(sp)
  li t1, 4
  slli t2, t1, 2
  add t3, t0, t2
  sw t3, 100(sp)
  lw t5, 100(sp)
  lw t4, 0(t5)
  sw t4, 104(sp)
  lw t6, 92(sp)
  lw t0, 104(sp)
  add t1, t6, t0
  sw t1, 108(sp)
  lw t2, 20(sp)
  sw t2, 112(sp)
  lw t3, 112(sp)
  li t4, 5
  li t6, 400
  mul t5, t4, t6
  add t0, t3, t5
  sw t0, 116(sp)
  lw t1, 116(sp)
  li t2, 5
  li t4, 40
  mul t3, t2, t4
  add t5, t1, t3
  sw t5, 120(sp)
  lw t6, 120(sp)
  li t0, 5
  slli t1, t0, 2
  add t2, t6, t1
  sw t2, 124(sp)
  lw t4, 124(sp)
  lw t3, 0(t4)
  sw t3, 128(sp)
  lw t5, 108(sp)
  lw t6, 128(sp)
  add t0, t5, t6
  sw t0, 132(sp)
  lw t1, 24(sp)
  sw t1, 136(sp)
  lw t2, 136(sp)
  li t3, 6
  slli t4, t3, 2
  add t5, t2, t4
  sw t5, 140(sp)
  lw t0, 140(sp)
  lw t6, 0(t0)
  sw t6, 144(sp)
  lw t1, 132(sp)
  lw t2, 144(sp)
  add t3, t1, t2
  sw t3, 148(sp)
  lw t4, 28(sp)
  sw t4, 152(sp)
  lw t5, 152(sp)
  li t6, 7
  slli t0, t6, 2
  add t1, t5, t0
  sw t1, 156(sp)
  lw t3, 156(sp)
  lw t2, 0(t3)
  sw t2, 160(sp)
  lw t4, 148(sp)
  lw t5, 160(sp)
  add t6, t4, t5
  sw t6, 164(sp)
  lw t0, 32(sp)
  sw t0, 168(sp)
  lw t1, 164(sp)
  lw t2, 168(sp)
  add t3, t1, t2
  sw t3, 172(sp)
  lw t4, 36(sp)
  sw t4, 176(sp)
  lw t5, 176(sp)
  li t6, 9
  li t1, 40
  mul t0, t6, t1
  add t2, t5, t0
  sw t2, 180(sp)
  lw t3, 180(sp)
  li t4, 8
  slli t5, t4, 2
  add t6, t3, t5
  sw t6, 184(sp)
  lw t1, 184(sp)
  lw t0, 0(t1)
  sw t0, 188(sp)
  lw t2, 172(sp)
  lw t3, 188(sp)
  add t4, t2, t3
  sw t4, 192(sp)
  lw a0, 192(sp)
  j .Lf2_exit
.Lf2_exit:
  addi sp, sp, 208
  ret
  .text
  .globl init
init:
  addi sp, sp, -128
.init_entry:
  mv t5, a0
  sw t5, 0(sp)
  li t6, 0
  sw t6, 4(sp)
  j .init_while_cond_0
.init_while_cond_0:
  lw t0, 4(sp)
  sw t0, 16(sp)
  lw t1, 16(sp)
  li t2, 10
  slt t3, t1, t2
  sw t3, 20(sp)
  lw t4, 20(sp)
  bnez t4, .init_while_body_1
  j .init_while_end_2
.init_while_body_1:
  li t5, 0
  sw t5, 8(sp)
  j .init_while_cond_3
.init_while_end_2:
  li a0, 0
  j .Linit_exit
.init_while_cond_3:
  lw t6, 8(sp)
  sw t6, 24(sp)
  lw t0, 24(sp)
  li t1, 10
  slt t2, t0, t1
  sw t2, 28(sp)
  lw t3, 28(sp)
  bnez t3, .init_while_body_4
  j .init_while_end_5
.init_while_body_4:
  li t4, 0
  sw t4, 12(sp)
  j .init_while_cond_6
.init_while_end_5:
  lw t5, 4(sp)
  sw t5, 32(sp)
  lw t6, 32(sp)
  li t0, 1
  add t1, t6, t0
  sw t1, 36(sp)
  lw t2, 36(sp)
  sw t2, 4(sp)
  j .init_while_cond_0
.init_while_cond_6:
  lw t3, 12(sp)
  sw t3, 40(sp)
  lw t4, 40(sp)
  li t5, 10
  slt t6, t4, t5
  sw t6, 44(sp)
  lw t0, 44(sp)
  bnez t0, .init_while_body_7
  j .init_while_end_8
.init_while_body_7:
  lw t1, 4(sp)
  sw t1, 48(sp)
  lw t2, 48(sp)
  li t3, 100
  mul t4, t2, t3
  sw t4, 52(sp)
  lw t5, 8(sp)
  sw t5, 56(sp)
  lw t6, 56(sp)
  li t0, 10
  mul t1, t6, t0
  sw t1, 60(sp)
  lw t2, 52(sp)
  lw t3, 60(sp)
  add t4, t2, t3
  sw t4, 64(sp)
  lw t5, 12(sp)
  sw t5, 68(sp)
  lw t6, 64(sp)
  lw t0, 68(sp)
  add t1, t6, t0
  sw t1, 72(sp)
  lw t2, 4(sp)
  sw t2, 76(sp)
  lw t3, 8(sp)
  sw t3, 80(sp)
  lw t4, 12(sp)
  sw t4, 84(sp)
  lw t5, 0(sp)
  sw t5, 88(sp)
  lw t6, 88(sp)
  lw t0, 76(sp)
  li t2, 400
  mul t1, t0, t2
  add t3, t6, t1
  sw t3, 92(sp)
  lw t4, 92(sp)
  lw t5, 80(sp)
  li t0, 40
  mul t6, t5, t0
  add t1, t4, t6
  sw t1, 96(sp)
  lw t2, 96(sp)
  lw t3, 84(sp)
  slli t4, t3, 2
  add t5, t2, t4
  sw t5, 100(sp)
  lw t6, 72(sp)
  lw t0, 100(sp)
  sw t6, 0(t0)
  lw t1, 12(sp)
  sw t1, 104(sp)
  lw t2, 104(sp)
  li t3, 1
  add t4, t2, t3
  sw t4, 108(sp)
  lw t5, 108(sp)
  sw t5, 12(sp)
  j .init_while_cond_6
.init_while_end_8:
  lw t6, 8(sp)
  sw t6, 112(sp)
  lw t0, 112(sp)
  li t1, 1
  add t2, t0, t1
  sw t2, 116(sp)
  lw t3, 116(sp)
  sw t3, 8(sp)
  j .init_while_cond_3
.Linit_exit:
  addi sp, sp, 128
  ret
  .text
  .globl main
main:
  li t4, 4160
  sub sp, sp, t4
  mv a7, ra
  li t5, 4156
  add t5, sp, t5
  sw a7, 0(t5)
.main_entry:
  li t5, 0
  mv a7, t5
  li t5, 4000
  add t5, sp, t5
  sw a7, 0(t5)
  addi t6, sp, 0
  li t0, 0
  li t2, 400
  mul t1, t0, t2
  add t3, t6, t1
  mv a7, t3
  li t5, 4004
  add t5, sp, t5
  sw a7, 0(t5)
  li t5, 4004
  add t5, sp, t5
  lw t4, 0(t5)
  mv a0, t4
  call init
  mv a7, a0
  li t5, 4008
  add t5, sp, t5
  sw a7, 0(t5)
  li t0, 4000
  add t0, sp, t0
  lw t6, 0(t0)
  mv a7, t6
  li t5, 4012
  add t5, sp, t5
  sw a7, 0(t5)
  addi t1, sp, 0
  li t2, 0
  li t4, 400
  mul t3, t2, t4
  add t5, t1, t3
  mv a7, t5
  li t5, 4016
  add t5, sp, t5
  sw a7, 0(t5)
  li t0, 4016
  add t0, sp, t0
  lw t6, 0(t0)
  li t1, 0
  li t3, 40
  mul t2, t1, t3
  add t4, t6, t2
  mv a7, t4
  li t5, 4020
  add t5, sp, t5
  sw a7, 0(t5)
  li t6, 4020
  add t6, sp, t6
  lw t5, 0(t6)
  li t0, 0
  slli t1, t0, 2
  add t2, t5, t1
  mv a7, t2
  li t5, 4024
  add t5, sp, t5
  sw a7, 0(t5)
  addi t3, sp, 0
  li t4, 1
  li t6, 400
  mul t5, t4, t6
  add t0, t3, t5
  mv a7, t0
  li t5, 4028
  add t5, sp, t5
  sw a7, 0(t5)
  li t2, 4028
  add t2, sp, t2
  lw t1, 0(t2)
  li t3, 1
  li t5, 40
  mul t4, t3, t5
  add t6, t1, t4
  mv a7, t6
  li t5, 4032
  add t5, sp, t5
  sw a7, 0(t5)
  li t1, 4032
  add t1, sp, t1
  lw t0, 0(t1)
  li t2, 0
  slli t3, t2, 2
  add t4, t0, t3
  mv a7, t4
  li t5, 4036
  add t5, sp, t5
  sw a7, 0(t5)
  addi t5, sp, 0
  li t6, 2
  li t1, 400
  mul t0, t6, t1
  add t2, t5, t0
  mv a7, t2
  li t5, 4040
  add t5, sp, t5
  sw a7, 0(t5)
  li t4, 4040
  add t4, sp, t4
  lw t3, 0(t4)
  li t5, 2
  li t0, 40
  mul t6, t5, t0
  add t1, t3, t6
  mv a7, t1
  li t5, 4044
  add t5, sp, t5
  sw a7, 0(t5)
  li t3, 4044
  add t3, sp, t3
  lw t2, 0(t3)
  li t4, 0
  slli t5, t4, 2
  add t6, t2, t5
  mv a7, t6
  li t5, 4048
  add t5, sp, t5
  sw a7, 0(t5)
  addi t0, sp, 0
  li t1, 3
  li t3, 400
  mul t2, t1, t3
  add t4, t0, t2
  mv a7, t4
  li t5, 4052
  add t5, sp, t5
  sw a7, 0(t5)
  li t6, 4052
  add t6, sp, t6
  lw t5, 0(t6)
  li t0, 3
  li t2, 40
  mul t1, t0, t2
  add t3, t5, t1
  mv a7, t3
  li t5, 4056
  add t5, sp, t5
  sw a7, 0(t5)
  li t5, 4056
  add t5, sp, t5
  lw t4, 0(t5)
  li t6, 0
  slli t0, t6, 2
  add t1, t4, t0
  mv a7, t1
  li t5, 4060
  add t5, sp, t5
  sw a7, 0(t5)
  addi t2, sp, 0
  li t3, 4
  li t5, 400
  mul t4, t3, t5
  add t6, t2, t4
  mv a7, t6
  li t5, 4064
  add t5, sp, t5
  sw a7, 0(t5)
  li t1, 4064
  add t1, sp, t1
  lw t0, 0(t1)
  li t2, 4
  li t4, 40
  mul t3, t2, t4
  add t5, t0, t3
  mv a7, t5
  li t5, 4068
  add t5, sp, t5
  sw a7, 0(t5)
  li t0, 4068
  add t0, sp, t0
  lw t6, 0(t0)
  li t1, 0
  slli t2, t1, 2
  add t3, t6, t2
  mv a7, t3
  li t5, 4072
  add t5, sp, t5
  sw a7, 0(t5)
  addi t4, sp, 0
  li t5, 5
  li t0, 400
  mul t6, t5, t0
  add t1, t4, t6
  mv a7, t1
  li t5, 4076
  add t5, sp, t5
  sw a7, 0(t5)
  li t3, 4076
  add t3, sp, t3
  lw t2, 0(t3)
  li t4, 5
  li t6, 40
  mul t5, t4, t6
  add t0, t2, t5
  mv a7, t0
  li t5, 4080
  add t5, sp, t5
  sw a7, 0(t5)
  li t2, 4080
  add t2, sp, t2
  lw t1, 0(t2)
  li t3, 0
  slli t4, t3, 2
  add t5, t1, t4
  mv a7, t5
  li t5, 4084
  add t5, sp, t5
  sw a7, 0(t5)
  addi t6, sp, 0
  li t0, 6
  li t2, 400
  mul t1, t0, t2
  add t3, t6, t1
  mv a7, t3
  li t5, 4088
  add t5, sp, t5
  sw a7, 0(t5)
  li t5, 4088
  add t5, sp, t5
  lw t4, 0(t5)
  li t6, 6
  li t1, 40
  mul t0, t6, t1
  add t2, t4, t0
  mv a7, t2
  li t5, 4092
  add t5, sp, t5
  sw a7, 0(t5)
  li t4, 4092
  add t4, sp, t4
  lw t3, 0(t4)
  li t5, 0
  slli t6, t5, 2
  add t0, t3, t6
  mv a7, t0
  li t5, 4096
  add t5, sp, t5
  sw a7, 0(t5)
  addi t1, sp, 0
  li t2, 7
  li t4, 400
  mul t3, t2, t4
  add t5, t1, t3
  mv a7, t5
  li t5, 4100
  add t5, sp, t5
  sw a7, 0(t5)
  li t0, 4100
  add t0, sp, t0
  lw t6, 0(t0)
  li t1, 7
  li t3, 40
  mul t2, t1, t3
  add t4, t6, t2
  mv a7, t4
  li t5, 4104
  add t5, sp, t5
  sw a7, 0(t5)
  li t6, 4104
  add t6, sp, t6
  lw t5, 0(t6)
  li t0, 0
  slli t1, t0, 2
  add t2, t5, t1
  mv a7, t2
  li t5, 4108
  add t5, sp, t5
  sw a7, 0(t5)
  addi t3, sp, 0
  li t4, 8
  li t6, 400
  mul t5, t4, t6
  add t0, t3, t5
  mv a7, t0
  li t5, 4112
  add t5, sp, t5
  sw a7, 0(t5)
  li t2, 4112
  add t2, sp, t2
  lw t1, 0(t2)
  li t3, 8
  li t5, 40
  mul t4, t3, t5
  add t6, t1, t4
  mv a7, t6
  li t5, 4116
  add t5, sp, t5
  sw a7, 0(t5)
  li t1, 4116
  add t1, sp, t1
  lw t0, 0(t1)
  li t2, 0
  slli t3, t2, 2
  add t4, t0, t3
  mv a7, t4
  li t5, 4120
  add t5, sp, t5
  sw a7, 0(t5)
  addi t5, sp, 0
  li t6, 9
  li t1, 400
  mul t0, t6, t1
  add t2, t5, t0
  mv a7, t2
  li t5, 4124
  add t5, sp, t5
  sw a7, 0(t5)
  li t4, 4124
  add t4, sp, t4
  lw t3, 0(t4)
  li t5, 9
  li t0, 40
  mul t6, t5, t0
  add t1, t3, t6
  mv a7, t1
  li t5, 4128
  add t5, sp, t5
  sw a7, 0(t5)
  li t3, 4128
  add t3, sp, t3
  lw t2, 0(t3)
  li t4, 0
  slli t5, t4, 2
  add t6, t2, t5
  mv a7, t6
  li t5, 4132
  add t5, sp, t5
  sw a7, 0(t5)
  addi sp, sp, -8
  li t1, 4120
  add t1, sp, t1
  lw t0, 0(t1)
  sw t0, 0(sp)
  li t3, 4132
  add t3, sp, t3
  lw t2, 0(t3)
  sw t2, 4(sp)
  li t5, 4024
  add t5, sp, t5
  lw t4, 0(t5)
  mv a0, t4
  li t0, 4036
  add t0, sp, t0
  lw t6, 0(t0)
  mv a1, t6
  li t2, 4048
  add t2, sp, t2
  lw t1, 0(t2)
  mv a2, t1
  li t4, 4060
  add t4, sp, t4
  lw t3, 0(t4)
  mv a3, t3
  li t6, 4072
  add t6, sp, t6
  lw t5, 0(t6)
  mv a4, t5
  li t1, 4084
  add t1, sp, t1
  lw t0, 0(t1)
  mv a5, t0
  li t3, 4096
  add t3, sp, t3
  lw t2, 0(t3)
  mv a6, t2
  li t5, 4108
  add t5, sp, t5
  lw t4, 0(t5)
  mv a7, t4
  call f1
  addi sp, sp, 8
  mv a7, a0
  li t5, 4136
  add t5, sp, t5
  sw a7, 0(t5)
  li t0, 4012
  add t0, sp, t0
  lw t6, 0(t0)
  li t2, 4136
  add t2, sp, t2
  lw t1, 0(t2)
  add t3, t6, t1
  mv a7, t3
  li t5, 4140
  add t5, sp, t5
  sw a7, 0(t5)
  li t5, 4140
  add t5, sp, t5
  lw t4, 0(t5)
  mv a7, t4
  li t5, 4000
  add t5, sp, t5
  sw a7, 0(t5)
  li t0, 4000
  add t0, sp, t0
  lw t6, 0(t0)
  mv a7, t6
  li t5, 4144
  add t5, sp, t5
  sw a7, 0(t5)
  li t1, 4144
  add t1, sp, t1
  lw a0, 0(t1)
  j .Lmain_exit
.Lmain_exit:
  li t2, 4156
  add t2, sp, t2
  lw ra, 0(t2)
  li t3, 4160
  add sp, sp, t3
  ret

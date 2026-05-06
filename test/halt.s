	.file	1 "halt.c"
gcc2_compiled.:
__gnu_compiled_c:
	.rdata
	.align	2
$LC0:
	.ascii	"I will shut down!\n\000"
	.text
	.align	2
	.globl	main
	.ent	main
main:
	.frame	$fp,24,$31		# vars= 0, regs= 2/0, args= 16, extra= 0
	.mask	0xc0000000,-4
	.fmask	0x00000000,0
	subu	$sp,$sp,24
	sw	$31,20($sp)
	sw	$fp,16($sp)
	move	$fp,$sp
	jal	__main
	la	$4,$LC0
	li	$5,18			# 0x00000012
	li	$6,1			# 0x00000001
	jal	Write
	jal	Halt
$L1:
	move	$sp,$fp
	lw	$31,20($sp)
	lw	$fp,16($sp)
	addu	$sp,$sp,24
	j	$31
	.end	main

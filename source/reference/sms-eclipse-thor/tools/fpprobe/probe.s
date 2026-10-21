# Black-box probe of Gekko frsqrte / fres (as modelled by the dolphin-oracle
# emulator): writes estimate results for a grid of inputs and for a list of
# random inputs to MEM1, then spins.
	.text
	.globl _start
_start:
	mfmsr   3
	ori     3, 3, 0x2000          # MSR[FP]
	mtmsr   3
	isync
	lis     6, 0x800F             # scratch 0x800F0000
	# --- frsqrte grid: k = 0..65535, bit 15 = exponent parity, bits 0..14 = top mantissa bits
	lis     4, 0x8010
	li      5, 0
	lis     10, 1
1:	rlwinm  7, 5, 5, 12, 26
	srwi    8, 5, 15
	addi    8, 8, 1023
	slwi    8, 8, 20
	or      7, 7, 8
	stw     7, 0(6)
	li      9, 0
	stw     9, 4(6)
	lfd     1, 0(6)
	frsqrte 2, 1
	stfd    2, 0(4)
	addi    4, 4, 8
	addi    5, 5, 1
	cmpw    5, 10
	blt     1b
	# --- fres grid: k = 0..32767, x = 1.<k:15 bits> (exponent 0)
	lis     4, 0x8018
	li      5, 0
	lis     10, 0
	ori     10, 10, 0x8000
2:	rlwinm  7, 5, 5, 12, 26
	oris    7, 7, 0x3FF0
	stw     7, 0(6)
	li      9, 0
	stw     9, 4(6)
	lfd     1, 0(6)
	fres    2, 1
	stfd    2, 0(4)
	addi    4, 4, 8
	addi    5, 5, 1
	cmpw    5, 10
	blt     2b
	# --- random inputs at 0x80200000 (4096 doubles): frsqrte -> 0x801C0000, fres -> 0x801C8000
	lis     3, 0x8020
	lis     4, 0x801C
	lis     11, 0x801C
	ori     11, 11, 0x8000
	li      5, 0
	li      10, 4096
3:	lfd     1, 0(3)
	frsqrte 2, 1
	stfd    2, 0(4)
	fres    2, 1
	stfd    2, 0(11)
	addi    3, 3, 8
	addi    4, 4, 8
	addi    11, 11, 8
	addi    5, 5, 1
	cmpw    5, 10
	blt     3b
	# done marker
	lis     3, 0x801D
	lis     4, 0x444F
	ori     4, 4, 0x4E45           # "DONE"
	stw     4, 0(3)
4:	b       4b

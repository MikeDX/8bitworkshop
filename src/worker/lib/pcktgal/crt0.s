; Startup code for cc65 — Data East Pocket Gal
; based on Exidy crt0

	.export _exit,__STARTUP__:absolute=1
	.export _HandyRTI
	.exportzp _INTVEC
	.export	NMI,IRQ,START
	.import initlib,push0,popa,popax,_main,zerobss,copydata
	.importzp sp

	.import __RAM0_START__  ,__RAM0_SIZE__

.segment "ZEROPAGE"

_INTVEC:         .res 2

.segment "STARTUP"

START:
_exit:
	sei
	cld
	ldx #$ff
	txs

	lda #$00
@clear:
	sta $0000,x
	sta $0100,x
	sta $0200,x
	sta $0300,x
	sta $0400,x
	sta $0500,x
	sta $0600,x
	sta $0700,x
	inx
	bne @clear

	; clear playfield + sprite RAM
	ldx #$00
@clearvram:
	sta $0800,x
	sta $0900,x
	sta $0a00,x
	sta $0b00,x
	sta $0c00,x
	sta $0d00,x
	sta $0e00,x
	sta $0f00,x
	sta $1000,x
	sta $1100,x
	inx
	bne @clearvram

	; BAC06: 8x8 tile mode, bit1 set = no TILE_FLIPX
	lda #$03
	sta $1800

	jsr	copydata

	lda	#<(__RAM0_START__+__RAM0_SIZE__)
	sta	sp
	lda	#>(__RAM0_START__+__RAM0_SIZE__)
	sta	sp+1

	jsr	initlib

	lda	#<_HandyRTI
	sta	_INTVEC
	lda	#>_HandyRTI
	sta	_INTVEC+1
	cli

	jmp	_main

NMI:
IRQ:
	jmp	(_INTVEC)

_HandyRTI:
	rti

.segment "VECTORS"

	.word NMI
	.word START
	.word IRQ

; Minimal crt0 for tcc816-generated SNES C.
.include "hdr.asm"

.RAMSECTION ".registers" BANK 0 SLOT 1 ORGA 0 FORCE PRIORITY 1000
tcc__registers dsb 0
tcc__r0 dsb 2
tcc__r0h dsb 2
tcc__r1 dsb 2
tcc__r1h dsb 2
tcc__r2 dsb 2
tcc__r2h dsb 2
tcc__r3 dsb 2
tcc__r3h dsb 2
tcc__r4 dsb 2
tcc__r4h dsb 2
tcc__r5 dsb 2
tcc__r5h dsb 2
tcc__r9 dsb 2
tcc__r9h dsb 2
tcc__r10 dsb 2
tcc__r10h dsb 2
.ENDS

.RAMSECTION "globram.data" BANK $7f SLOT 3 KEEP
.ENDS

.BANK 0
.SECTION "glob.data" SEMIFREE KEEP
.ENDS

.BANK 0
.SECTION "EmptyVectors" SEMIFREE
EmptyHandler:
  rti
.ENDS

.BANK 0
.SECTION ".start" SEMIFREE
.accu 16
.index 16
.16bit

tcc__start:
  sei
  clc
  xce
  rep #$18
  ldx #$1FFF
  txs

  sep #$20
  lda #$8F
  sta $2100
  stz $2101
  stz $2102
  stz $2103
  stz $2105
  stz $2106
  stz $2107
  stz $2108
  stz $2109
  stz $210A
  stz $210B
  stz $210C
  stz $212C
  stz $212D
  stz $4200
  lda #$FF
  sta $4201

  rep #$30
  lda #tcc__registers
  tcd

  pea $0000
  plb
  plb

  jsr.l main
  stp
.ENDS

.RAMSECTION ".bss" BANK $7e SLOT 2
.ENDS

; Thin SNES runtime helpers (Mode7 + DMA + VBlank).
.include "hdr.asm"

;------------------------------------------------------------------------------
; Sin table: signed 8-bit, 256 entries. sin(64)=127, sin(192)=-127
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".snestable" SUPERFREE
snes_sin:
  .DB 0,3,6,9,12,16,19,22,25,28,31,34,37,40,43,46
  .DB 49,51,54,57,60,63,65,68,71,73,76,78,81,83,85,88
  .DB 90,92,94,96,98,100,102,104,106,107,109,111,112,113,115,116
  .DB 117,118,120,121,122,122,123,124,125,125,126,126,126,127,127,127
  .DB 127,127,127,127,126,126,126,125,125,124,123,122,122,121,120,118
  .DB 117,116,115,113,112,111,109,107,106,104,102,100,98,96,94,92
  .DB 90,88,85,83,81,78,76,73,71,68,65,63,60,57,54,51
  .DB 49,46,43,40,37,34,31,28,25,22,19,16,12,9,6,3
  .DB 0,$FD,$FA,$F7,$F4,$F0,$ED,$EA,$E7,$E4,$E1,$DE,$DB,$D8,$D5,$D2
  .DB $CF,$CD,$CA,$C7,$C4,$C1,$BF,$BC,$B9,$B7,$B4,$B2,$AF,$AD,$AB,$A8
  .DB $A6,$A4,$A2,$A0,$9E,$9C,$9A,$98,$96,$95,$93,$91,$90,$8F,$8D,$8C
  .DB $8B,$8A,$88,$87,$86,$86,$85,$84,$83,$83,$82,$82,$82,$81,$81,$81
  .DB $81,$81,$81,$81,$82,$82,$82,$83,$83,$84,$85,$86,$86,$87,$88,$8A
  .DB $8B,$8C,$8D,$8F,$90,$91,$93,$95,$96,$98,$9A,$9C,$9E,$A0,$A2,$A4
  .DB $A6,$A8,$AB,$AD,$AF,$B2,$B4,$B7,$B9,$BC,$BF,$C1,$C4,$C7,$CA,$CD
  .DB $CF,$D2,$D5,$D8,$DB,$DE,$E1,$E4,$E7,$EA,$ED,$F0,$F4,$F7,$FA,$FD
.ENDS

;------------------------------------------------------------------------------
; void poke8(unsigned addr, unsigned char val)
; [ret][addr][val]
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".poke8text" SUPERFREE
.accu 16
.index 16
.16bit
poke8:
  lda 4,s
  tax
  sep #$20
  lda 6,s
  sta.l $000000,x
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void poke16(unsigned addr, unsigned val) — write-twice (low, high)
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".poke16text" SUPERFREE
.accu 16
.index 16
.16bit
poke16:
  lda 4,s
  tax
  lda 6,s
  sep #$20
  sta.l $000000,x
  xba
  sta.l $000000,x
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void dma_to_ppu(...) far src: [ret][off][bank][dest][size][mode]
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".dmatext" SUPERFREE
.accu 16
.index 16
.16bit
dma_to_ppu:
  sep #$20
  lda 11,s
  sta $4300
  lda 8,s
  sta $4301
  rep #$20
  lda 4,s
  sta $4302
  sep #$20
  lda 6,s
  sta $4304
  rep #$20
  lda 9,s
  sta $4305
  sep #$20
  lda #$01
  sta $420B
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_wait_vblank(void)
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".waitvbltext" SUPERFREE
.accu 16
.index 16
.16bit
snes_wait_vblank:
  sep #$20
- lda $4210
  bmi -
- lda $4210
  bpl -
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_brightness(unsigned char level)
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".brighttext" SUPERFREE
.accu 16
.index 16
.16bit
snes_brightness:
  sep #$20
  lda 4,s
  sta $2100
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_cgram(unsigned char index, unsigned color)
; [ret][index][color]
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".cgramtext" SUPERFREE
.accu 16
.index 16
.16bit
snes_cgram:
  sep #$20
  lda 4,s
  sta $2121
  lda 5,s
  sta $2122
  lda 6,s
  sta $2122
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_vram_addr(unsigned word_addr)
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".vramaddrtext" SUPERFREE
.accu 16
.index 16
.16bit
snes_vram_addr:
  lda 4,s
  sta $2116
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_mode7_begin(unsigned char m7sel)
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".m7begintext" SUPERFREE
.accu 16
.index 16
.16bit
snes_mode7_begin:
  sep #$20
  lda #7
  sta $2105
  lda 4,s
  sta $211A
  lda #$00
  sta $211B
  lda #$01
  sta $211B
  stz $211C
  stz $211C
  stz $211D
  stz $211D
  stz $211E
  lda #$01
  sta $211E
  lda #$80
  sta $211F
  stz $211F
  sta $2120
  stz $2120
  stz $210D
  stz $210D
  lda #$80
  sta $210E
  lda #$01
  sta $210E
  lda #$01
  sta $212C
  stz $212D
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_mode7_matrix(unsigned a, unsigned b, unsigned c, unsigned d)
; [ret][a][b][c][d]
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".m7matrixtext" SUPERFREE
.accu 16
.index 16
.16bit
snes_mode7_matrix:
  lda 4,s
  sep #$20
  sta $211B
  xba
  sta $211B
  rep #$20
  lda 6,s
  sep #$20
  sta $211C
  xba
  sta $211C
  rep #$20
  lda 8,s
  sep #$20
  sta $211D
  xba
  sta $211D
  rep #$20
  lda 10,s
  sep #$20
  sta $211E
  xba
  sta $211E
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_mode7_center / scroll
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".m7posText" SUPERFREE
.accu 16
.index 16
.16bit
snes_mode7_center:
  lda 4,s
  sep #$20
  sta $211F
  xba
  sta $211F
  rep #$20
  lda 6,s
  sep #$20
  sta $2120
  xba
  sta $2120
  rep #$20
  rtl

snes_mode7_scroll:
  lda 4,s
  sep #$20
  sta $210D
  xba
  sta $210D
  rep #$20
  lda 6,s
  sep #$20
  sta $210E
  xba
  sta $210E
  rep #$20
  rtl
.ENDS

;------------------------------------------------------------------------------
; void snes_mode7_rot(unsigned char angle, unsigned scale)
; [ret][angle][scale]  scale 8.8 (0x0100 = 1.0)
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".m7rottext" SUPERFREE
.accu 16
.index 16
.16bit
snes_mode7_rot:
  phb
  sep #$20
  lda #:snes_sin
  pha
  plb                       ; DB = bank of sin table
  lda 5,s                   ; angle (1 byte DB push + ret)
  clc
  adc #64
  tax
  lda snes_sin,x
  sta.b tcc__r0             ; cos8
  lda 5,s
  tax
  lda snes_sin,x
  sta.b tcc__r3             ; sin8
  plb                       ; DB restored (was pushed by phb... wait plb pulled our bank)
  ; After: phb pushed old DB; we pha/plb to set; then plb restores old DB. Good.
  ; Stack now: [ret][angle][scale] again — angle@4 scale@5

  rep #$20
  lda 5,s
  cmp #$0100
  bne _m7_slow

  lda.b tcc__r0
  and #$00FF
  bit #$0080
  beq +
  ora #$FF00
+
  asl a
  sta.b tcc__r1
  lda.b tcc__r3
  and #$00FF
  bit #$0080
  beq +
  ora #$FF00
+
  asl a
  sta.b tcc__r2
  bra _m7_commit

_m7_slow:
  lda 5,s
  sep #$20
  sta $211B
  xba
  sta $211B
  lda.b tcc__r0
  sta $211C
  jsr _m7_read_asr7
  sta.b tcc__r1
  rep #$20
  lda 5,s
  sep #$20
  sta $211B
  xba
  sta $211B
  lda.b tcc__r3
  sta $211C
  jsr _m7_read_asr7
  sta.b tcc__r2

_m7_commit:
  rep #$20
  lda.b tcc__r1
  sep #$20
  sta $211B
  xba
  sta $211B
  rep #$20
  lda.b tcc__r2
  sep #$20
  sta $211C
  xba
  sta $211C
  rep #$20
  lda.b tcc__r2
  eor #$FFFF
  inc a
  sep #$20
  sta $211D
  xba
  sta $211D
  rep #$20
  lda.b tcc__r1
  sep #$20
  sta $211E
  xba
  sta $211E
  rtl

_m7_read_asr7:
  rep #$20
  lda $2134
  ldx #7
-
  cmp #$8000
  ror a
  dex
  bne -
  rts
.ENDS

;------------------------------------------------------------------------------
; Mode7 DMA: map → VMDATAL, tiles → VMDATAH
; far src: [ret][off][bank][word_addr][size]
;------------------------------------------------------------------------------
.BANK 0
.SECTION ".m7loadtext" SUPERFREE
.accu 16
.index 16
.16bit
snes_mode7_load_map:
  sep #$20
  stz $2115
  rep #$20
  lda 8,s
  sta $2116
  sep #$20
  stz $4300
  lda #$18
  sta $4301
  rep #$20
  lda 4,s
  sta $4302
  sep #$20
  lda 6,s
  sta $4304
  rep #$20
  lda 10,s
  sta $4305
  sep #$20
  lda #$01
  sta $420B
  rep #$20
  rtl

snes_mode7_load_tiles:
  sep #$20
  lda #$80
  sta $2115
  rep #$20
  lda 8,s
  sta $2116
  sep #$20
  stz $4300
  lda #$19
  sta $4301
  rep #$20
  lda 4,s
  sta $4302
  sep #$20
  lda 6,s
  sta $4304
  rep #$20
  lda 10,s
  sta $4305
  sep #$20
  lda #$01
  sta $420B
  rep #$20
  rtl
.ENDS

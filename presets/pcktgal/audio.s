; Pocket Gal audio CPU — Pac-Man-ish (SSG-first)
; Music + SFX + ambient all on YM2203 SSG @ $0800.
;   A = music lead / one-shot whoops
;   B = music harmony / fright warble
;   C = siren / eyes whoop
; YM3812/MSM5205 left idle (FM sounded LCD-ish; ADPCM tonal chirps were worse).
; Latch $3000, ROM $8000-FFFF.

.include "audio_cmds.inc"

YM_ADDR = $0800
YM_DATA = $0801
LATCH   = $3000
BANK    = $2000

.zeropage
cmd:            .res 1
cmd_pending:    .res 1
tmp:            .res 1
tmp2:           .res 1
ptr:            .res 2
seq_id:         .res 1
seq_pos:        .res 1
seq_timer:      .res 1
seq_b_id:       .res 1
seq_b_pos:      .res 1
seq_b_timer:    .res 1
env_a:          .res 1          ; music note envelope
env_b:          .res 1
sfx_mode:       .res 1
sfx_t:          .res 1
sfx_per:        .res 2
sfx_vol:        .res 1
fright_on:      .res 1
fright_tick:    .res 1
fright_per:     .res 2
siren_mode:     .res 1
siren_per:      .res 2
siren_dir:      .res 1
delay_lo:       .res 1
delay_hi:       .res 1
step:           .res 1

.segment "CODE"

reset:
        sei
        cld
        ldx #$ff
        txs
        lda #0
        sta cmd_pending
        sta seq_id
        sta seq_b_id
        sta sfx_mode
        sta fright_on
        sta siren_mode
        sta env_a
        sta env_b
        lda #2
        sta BANK                ; MSM held in reset
        jsr ym_init
        jmp main

nmi:
        pha
        txa
        pha
        tya
        pha
        lda LATCH
        sta cmd
        lda #1
        sta cmd_pending
        pla
        tay
        pla
        tax
        pla
        rti

irq:
        rti

main:
        lda cmd_pending
        beq @tick
        lda #0
        sta cmd_pending
        jsr handle_cmd
@tick:
        jsr tick_seq
        jsr tick_seq_b
        jsr tick_env
        jsr tick_sfx
        jsr tick_fright
        jsr tick_siren
        jsr delay_frame
        jmp main

delay_frame:
        lda #26
        sta delay_hi
@d1:    lda #120
        sta delay_lo
@d0:    dec delay_lo
        bne @d0
        dec delay_hi
        bne @d1
        rts

; ---------- YM2203 SSG ----------
ym_write:
        sta YM_ADDR
        stx YM_DATA
        rts

ym_init:
        ldx #$38                ; tone ABC on
        lda #7
        jsr ym_write
        ldx #0
        lda #8
        jsr ym_write
        lda #9
        jsr ym_write
        lda #10
        jsr ym_write
        rts

set_per_a:
        lda #0
        ldx tmp
        jsr ym_write
        lda #1
        ldx tmp2
        jmp ym_write
set_per_b:
        lda #2
        ldx tmp
        jsr ym_write
        lda #3
        ldx tmp2
        jmp ym_write
set_per_c:
        lda #4
        ldx tmp
        jsr ym_write
        lda #5
        ldx tmp2
        jmp ym_write
set_vol_a:
        tax
        lda #8
        jmp ym_write
set_vol_b:
        tax
        lda #9
        jmp ym_write
set_vol_c:
        tax
        lda #10
        jmp ym_write

load_per:                       ; A = namco note 1..15
        asl
        tax
        lda per_tab,x
        sta tmp
        lda per_tab+1,x
        sta tmp2
        rts

ge_siren_ax:
        sta tmp
        stx tmp2
        lda siren_per
        cmp tmp
        lda siren_per+1
        sbc tmp2
        rts

silence_all:
        lda #0
        sta seq_id
        sta seq_b_id
        sta sfx_mode
        sta fright_on
        sta siren_mode
        sta env_a
        sta env_b
        jsr set_vol_a
        lda #0
        jsr set_vol_b
        lda #0
        jmp set_vol_c

; ---------- commands ----------
handle_cmd:
        lda cmd
        cmp #CMD_OFF
        bne @1
        jmp silence_all
@1:     cmp #CMD_WAKA1
        bne @2
        lda #1
        jmp start_sfx
@2:     cmp #CMD_WAKA2
        bne @3
        lda #2
        jmp start_sfx
@3:     cmp #CMD_EAT
        bne @4
        lda #0
        sta fright_on
        jsr set_vol_b
        lda #3
        jmp start_sfx
@4:     cmp #CMD_DEATH
        bne @5
        jsr silence_music_amb
        lda #4
        jmp start_sfx
@5:     cmp #CMD_COIN
        bne @6
        lda #5
        jmp start_sfx
@6:     cmp #CMD_POWER
        bne @7
        jmp start_fright
@7:     cmp #CMD_FRIGHT_OFF
        bne @8
        lda #0
        sta fright_on
        jmp set_vol_b
@8:     cmp #CMD_SIREN
        bne @9
        jmp start_siren
@9:     cmp #CMD_EYES
        bne @a
        jmp start_eyes
@a:     cmp #CMD_FRUIT
        bne @b
        lda #6
        jmp start_sfx
@b:     cmp #CMD_PRELUDE
        bne @c
        jmp start_prelude
@c:     cmp #CMD_INTERMISSION
        bne @done
        jmp start_intermission
@done:  rts

silence_music_amb:
        lda #0
        sta seq_id
        sta seq_b_id
        sta fright_on
        sta siren_mode
        sta env_a
        sta env_b
        jsr set_vol_b
        lda #0
        jmp set_vol_c

start_seq:
        sta seq_id
        lda #0
        sta seq_pos
        lda #1
        sta seq_timer
        rts
start_seq_b:
        sta seq_b_id
        lda #0
        sta seq_b_pos
        lda #1
        sta seq_b_timer
        rts

start_sfx:
        sta sfx_mode
        lda #0
        sta sfx_t
        ; duck music lead if somehow overlapping
        sta seq_id
        sta env_a
        rts

start_prelude:
        lda #0
        sta sfx_mode
        sta fright_on
        sta siren_mode
        jsr set_vol_c
        lda #7
        jsr start_seq
        lda #8
        jmp start_seq_b

start_intermission:
        lda #0
        sta sfx_mode
        sta fright_on
        sta siren_mode
        jsr set_vol_c
        lda #9
        jsr start_seq
        lda #10
        jmp start_seq_b

start_fright:
        lda #0
        sta seq_b_id
        sta env_b
        lda #1
        sta fright_on
        lda #0
        sta fright_tick
        sta siren_mode
        jsr set_vol_c
        lda #<110
        sta fright_per
        lda #>110
        sta fright_per+1
        lda #12
        jmp set_vol_b

start_siren:
        lda #0
        sta fright_on
        jsr set_vol_b
        lda #1
        sta siren_mode
        lda #<300
        sta siren_per
        lda #>300
        sta siren_per+1
        lda #0
        sta siren_dir
        lda #11
        jmp set_vol_c

start_eyes:
        lda #0
        sta fright_on
        jsr set_vol_b
        lda #2
        sta siren_mode
        lda #<140
        sta siren_per
        lda #>140
        sta siren_per+1
        lda #0
        sta siren_dir
        lda #13
        jmp set_vol_c

; ---------- music sequencers (SSG + envelope) ----------
tick_seq:
        lda seq_id
        bne @go
        rts
@go:    lda sfx_mode
        beq @ok
        rts                     ; SFX owns A
@ok:    dec seq_timer
        beq @step
        rts
@step:  jsr seq_ptr_a
        ldy #0
        lda (ptr),y
        cmp #$ff
        bne @notend
        lda #0
        sta seq_id
        sta seq_b_id            ; stop both together
        sta env_a
        sta env_b
        jsr set_vol_a
        lda #0
        jmp set_vol_b
@notend:
        cmp #$fe
        bne @note
        lda #0
        sta env_a
        jsr set_vol_a
        jmp @adv
@note:  jsr load_per
        jsr set_per_a
        ldy #2
        lda (ptr),y
        sta env_a
        jsr set_vol_a
@adv:   ldy #1
        lda (ptr),y
        sta seq_timer
        clc
        lda seq_pos
        adc #3
        sta seq_pos
        rts

seq_ptr_a:
        lda seq_id
        asl
        tax
        lda seq_table,x
        clc
        adc seq_pos
        sta ptr
        lda seq_table+1,x
        adc #0
        sta ptr+1
        rts

tick_seq_b:
        lda seq_b_id
        bne @go
        rts
@go:    lda fright_on
        beq @ok
        rts
@ok:    dec seq_b_timer
        beq @step
        rts
@step:  jsr seq_ptr_b
        ldy #0
        lda (ptr),y
        cmp #$ff
        bne @notend
        lda #0
        sta seq_b_id
        sta env_b
        jmp set_vol_b
@notend:
        cmp #$fe
        bne @note
        lda #0
        sta env_b
        jsr set_vol_b
        jmp @adv
@note:  jsr load_per
        jsr set_per_b
        ldy #2
        lda (ptr),y
        sta env_b
        jsr set_vol_b
@adv:   ldy #1
        lda (ptr),y
        sta seq_b_timer
        clc
        lda seq_b_pos
        adc #3
        sta seq_b_pos
        rts

seq_ptr_b:
        lda seq_b_id
        asl
        tax
        lda seq_table,x
        clc
        adc seq_b_pos
        sta ptr
        lda seq_table+1,x
        adc #0
        sta ptr+1
        rts

; Soften squares: decay toward sustain while note holds
tick_env:
        lda sfx_mode
        bne @b
        lda seq_id
        beq @b
        lda env_a
        cmp #7
        bcc @b
        dec env_a
        lda env_a
        jsr set_vol_a
@b:     lda fright_on
        bne @done
        lda seq_b_id
        beq @done
        lda env_b
        cmp #5
        bcc @done
        dec env_b
        lda env_b
        jsr set_vol_b
@done:  rts

; ---------- SFX whoops (same language as eyes/fright) ----------
tick_sfx:
        lda sfx_mode
        bne @go
        rts
@go:    cmp #1
        bne @2
        jmp sfx_waka1
@2:     cmp #2
        bne @3
        jmp sfx_waka2
@3:     cmp #3
        bne @4
        jmp sfx_eat
@4:     cmp #4
        bne @5
        jmp sfx_death
@5:     cmp #5
        bne @6
        jmp sfx_coin
@6:     cmp #6
        bne @x
        jmp sfx_fruit
@x:     lda #0
        sta sfx_mode
        rts

sfx_apply:
        lda sfx_per
        sta tmp
        lda sfx_per+1
        sta tmp2
        jsr set_per_a
        lda sfx_vol
        jmp set_vol_a

sfx_waka1:
        lda sfx_t
        bne @c
        lda #<200
        sta sfx_per
        lda #>200
        sta sfx_per+1
        lda #13
        sta sfx_vol
@c:     jsr sfx_apply
        lda sfx_per
        sec
        sbc #18
        sta sfx_per
        lda sfx_per+1
        sbc #0
        sta sfx_per+1
        lda sfx_t
        cmp #4
        bcc @n
        dec sfx_vol
        lda sfx_vol
        beq @e
@n:     inc sfx_t
        lda sfx_t
        cmp #8
        bcs @e
        rts
@e:     jmp sfx_end

sfx_waka2:
        lda sfx_t
        bne @c
        lda #<230
        sta sfx_per
        lda #>230
        sta sfx_per+1
        lda #13
        sta sfx_vol
@c:     jsr sfx_apply
        lda sfx_per
        sec
        sbc #16
        sta sfx_per
        lda sfx_per+1
        sbc #0
        sta sfx_per+1
        lda sfx_t
        cmp #4
        bcc @n
        dec sfx_vol
        lda sfx_vol
        beq @e
@n:     inc sfx_t
        lda sfx_t
        cmp #8
        bcs @e
        rts
@e:     jmp sfx_end

sfx_eat:
        lda sfx_t
        bne @c
        lda #<220
        sta sfx_per
        lda #>220
        sta sfx_per+1
        lda #14
        sta sfx_vol
@c:     jsr sfx_apply
        lda sfx_per
        sec
        sbc #14
        sta sfx_per
        lda sfx_per+1
        sbc #0
        sta sfx_per+1
        lda sfx_t
        cmp #10
        bcc @n
        lda sfx_vol
        sec
        sbc #2
        beq @e
        bcc @e
        sta sfx_vol
@n:     inc sfx_t
        lda sfx_t
        cmp #16
        bcs @e
        rts
@e:     jmp sfx_end

sfx_death:
        lda sfx_t
        bne @c
        lda #<90
        sta sfx_per
        lda #>90
        sta sfx_per+1
        lda #14
        sta sfx_vol
@c:     jsr sfx_apply
        lda sfx_per
        clc
        adc #7
        sta sfx_per
        lda sfx_per+1
        adc #0
        sta sfx_per+1
        lda sfx_t
        and #3
        bne @n
        lda sfx_vol
        beq @e
        dec sfx_vol
@n:     inc sfx_t
        lda sfx_t
        cmp #52
        bcs @e
        rts
@e:     jmp sfx_end

sfx_coin:
        lda sfx_t
        bne @c
        lda #<160
        sta sfx_per
        lda #>160
        sta sfx_per+1
        lda #14
        sta sfx_vol
@c:     lda sfx_t
        cmp #4
        bcs @dn
        lda sfx_per
        sec
        sbc #20
        sta sfx_per
        jmp @o
@dn:    lda sfx_per
        clc
        adc #12
        sta sfx_per
        lda sfx_vol
        sec
        sbc #2
        bcc @e
        sta sfx_vol
@o:     jsr sfx_apply
        inc sfx_t
        lda sfx_t
        cmp #12
        bcs @e
        rts
@e:     jmp sfx_end

sfx_fruit:
        lda sfx_t
        bne @c
        lda #<180
        sta sfx_per
        lda #>180
        sta sfx_per+1
        lda #13
        sta sfx_vol
@c:     jsr sfx_apply
        lda sfx_per
        sec
        sbc #12
        sta sfx_per
        lda sfx_t
        cmp #6
        bcc @n
        dec sfx_vol
        lda sfx_vol
        beq @e
@n:     inc sfx_t
        lda sfx_t
        cmp #12
        bcs @e
        rts
@e:     jmp sfx_end

sfx_end:
        lda #0
        sta sfx_mode
        jmp set_vol_a

; ---------- ambient ----------
tick_fright:
        lda fright_on
        bne @go
        rts
@go:    lda fright_tick
        and #7
        bne @add
        lda #<110
        sta fright_per
        lda #>110
        sta fright_per+1
        jmp @out
@add:   lda fright_per
        sec
        sbc #14
        sta fright_per
        lda fright_per+1
        sbc #0
        sta fright_per+1
@out:   inc fright_tick
        lda fright_per
        sta tmp
        lda fright_per+1
        sta tmp2
        jsr set_per_b
        lda #12
        jmp set_vol_b

tick_siren:
        lda siren_mode
        bne @go
        rts
@go:    cmp #2
        beq @eyes
        lda #4
        sta step
        jmp @sw
@eyes:  lda #11
        sta step
@sw:    lda siren_dir
        bne @dn
        lda siren_per
        sec
        sbc step
        sta siren_per
        lda siren_per+1
        sbc #0
        sta siren_per+1
        lda siren_mode
        cmp #2
        beq @ef
        lda #<260
        ldx #>260
        jsr ge_siren_ax
        bcs @set
        lda #1
        sta siren_dir
        jmp @set
@ef:    lda #85
        ldx #0
        jsr ge_siren_ax
        bcs @set
        lda #1
        sta siren_dir
        jmp @set
@dn:    lda siren_per
        clc
        adc step
        sta siren_per
        lda siren_per+1
        adc #0
        sta siren_per+1
        lda siren_mode
        cmp #2
        beq @ec
        lda #<420
        ldx #>420
        jsr ge_siren_ax
        bcc @set
        lda #0
        sta siren_dir
        jmp @set
@ec:    lda #<210
        ldx #>210
        jsr ge_siren_ax
        bcc @set
        lda #0
        sta siren_dir
@set:   lda siren_per
        sta tmp
        lda siren_per+1
        sta tmp2
        jsr set_per_c
        lda siren_mode
        cmp #2
        beq @ev
        lda #11
        jmp set_vol_c
@ev:    lda #13
        jmp set_vol_c

; ---------- tables ----------
; Namco note → AY period (+1 octave from arcade table)
per_tab:
        .word 0
        .word 184, 174, 165, 155, 147, 138, 130, 123
        .word 116, 110, 104, 98, 92, 87, 82

seq_table:
        .word 0
        .word 0, 0, 0, 0, 0, 0
        .word seq_intro
        .word seq_intro_b
        .word seq_inter
        .word seq_inter_b

; Round-start jingle CH1 (tables+0xa4)
seq_intro:
        .byte 2, 8, 13
        .byte $fe, 7, 0
        .byte 9, 6, 13
        .byte 2, 8, 13
        .byte $fe, 7, 0
        .byte 9, 6, 13
        .byte 3, 8, 13
        .byte $fe, 7, 0
        .byte 10, 6, 13
        .byte 3, 8, 13
        .byte $fe, 7, 0
        .byte 10, 6, 13
        .byte 2, 8, 13
        .byte $fe, 7, 0
        .byte 9, 6, 13
        .byte 2, 8, 13
        .byte $fe, 7, 0
        .byte 9, 6, 13
        .byte 9, 8, 13
        .byte 11, 8, 13
        .byte 13, 8, 13
        .byte 14, 8, 13
        .byte $ff

seq_intro_b:
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 11, 4, 9
        .byte $fe, 1, 0
        .byte 12, 4, 9
        .byte $fe, 1, 0
        .byte 13, 4, 9
        .byte $fe, 1, 0
        .byte 14, 4, 9
        .byte $fe, 1, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 11, 4, 9
        .byte $fe, 1, 0
        .byte 12, 4, 9
        .byte $fe, 1, 0
        .byte 13, 4, 9
        .byte $fe, 1, 0
        .byte 14, 4, 9
        .byte $fe, 1, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 7, 6, 9
        .byte $fe, 8, 0
        .byte 7, 4, 9
        .byte $fe, 3, 0
        .byte 11, 4, 9
        .byte $fe, 1, 0
        .byte 12, 4, 9
        .byte $fe, 1, 0
        .byte 13, 4, 9
        .byte $fe, 1, 0
        .byte 14, 4, 9
        .byte $fe, 1, 0
        .byte 7, 7, 9
        .byte $fe, 2, 0
        .byte 14, 4, 9
        .byte $fe, 1, 0
        .byte 13, 4, 9
        .byte $fe, 1, 0
        .byte 12, 4, 9
        .byte $fe, 1, 0
        .byte 10, 4, 9
        .byte $fe, 1, 0
        .byte 7, 4, 9
        .byte $fe, 1, 0
        .byte 6, 4, 9
        .byte $fe, 1, 0
        .byte 5, 6, 9
        .byte $fe, 3, 0
        .byte 6, 6, 9
        .byte $fe, 3, 0
        .byte 7, 6, 9
        .byte $fe, 11, 0
        .byte $ff

seq_inter:
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 3, 2, 13
        .byte 4, 4, 13
        .byte 2, 4, 13
        .byte 7, 4, 13
        .byte $fe, 3, 0
        .byte 7, 6, 13
        .byte 10, 2, 13
        .byte 11, 8, 13
        .byte $fe, 7, 0
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 3, 2, 13
        .byte 4, 4, 13
        .byte 2, 4, 13
        .byte 7, 4, 13
        .byte $fe, 3, 0
        .byte 7, 6, 13
        .byte 3, 2, 13
        .byte 4, 8, 13
        .byte $fe, 7, 0
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 6, 2, 13
        .byte 7, 6, 13
        .byte 3, 2, 13
        .byte 4, 4, 13
        .byte 2, 4, 13
        .byte 7, 4, 13
        .byte $fe, 3, 0
        .byte 7, 6, 13
        .byte 9, 2, 13
        .byte 10, 6, 13
        .byte 11, 2, 13
        .byte 12, 6, 13
        .byte $fe, 3, 0
        .byte 12, 2, 13
        .byte 13, 6, 13
        .byte $fe, 4, 0
        .byte 11, 2, 13
        .byte 12, 6, 13
        .byte 9, 2, 13
        .byte 10, 6, 13
        .byte 7, 6, 13
        .byte $fe, 2, 0
        .byte 9, 2, 13
        .byte 10, 6, 13
        .byte $fe, 4, 0
        .byte 6, 2, 13
        .byte 7, 8, 13
        .byte $fe, 7, 0
        .byte $ff

seq_inter_b:
        .byte 2, 4, 9
        .byte $fe, 5, 0
        .byte 14, 4, 9
        .byte $fe, 5, 0
        .byte 9, 4, 9
        .byte $fe, 5, 0
        .byte 6, 4, 9
        .byte $fe, 5, 0
        .byte 14, 4, 9
        .byte 9, 4, 9
        .byte $fe, 7, 0
        .byte 6, 6, 9
        .byte $fe, 7, 0
        .byte 3, 4, 9
        .byte $fe, 5, 0
        .byte 15, 4, 9
        .byte $fe, 5, 0
        .byte 10, 4, 9
        .byte $fe, 5, 0
        .byte 7, 4, 9
        .byte $fe, 5, 0
        .byte 15, 4, 9
        .byte 10, 4, 9
        .byte $fe, 7, 0
        .byte 7, 6, 9
        .byte $fe, 7, 0
        .byte 2, 4, 9
        .byte $fe, 5, 0
        .byte 14, 4, 9
        .byte $fe, 5, 0
        .byte 9, 4, 9
        .byte $fe, 5, 0
        .byte 6, 4, 9
        .byte $fe, 5, 0
        .byte 14, 4, 9
        .byte 9, 4, 9
        .byte $fe, 7, 0
        .byte 6, 6, 9
        .byte $fe, 7, 0
        .byte 5, 4, 9
        .byte 6, 4, 9
        .byte 7, 4, 9
        .byte $fe, 5, 0
        .byte 7, 4, 9
        .byte 8, 4, 9
        .byte 9, 4, 9
        .byte $fe, 5, 0
        .byte 9, 4, 9
        .byte 10, 4, 9
        .byte 11, 4, 9
        .byte $fe, 5, 0
        .byte 14, 6, 9
        .byte $ff

.segment "VECTORS"
        .word nmi
        .word reset
        .word irq

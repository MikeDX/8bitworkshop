; Tile CHR for hello.c (4bpp, ASCII-mapped font)
.include "hdr.asm"

.BANK 0
.SECTION ".hello_tiles" SUPERFREE
hello_tiles:
  .incbin "hello_tiles.chr"
hello_tiles_end:
.ENDS

-- Record Pocket Gal Pac-Man ADPCM cues via sound latch ($1A00).
--   mame pcktgal2 -rompath roms -window -skip_gameinfo -seconds_to_run 30 \
--     -wavwrite snap/pacman_adpcm_mame.wav \
--     -autoboot_delay 2 -autoboot_script scripts/pcktgal_sfx_record.lua

local sequence = {
  {cmd = 11, frames = 200}, -- prelude
  {cmd = 0,  frames = 40},
  {cmd = 1,  frames = 40},  -- waka1
  {cmd = 2,  frames = 40},  -- waka2
  {cmd = 3,  frames = 50},  -- eat
  {cmd = 5,  frames = 50},  -- coin
  {cmd = 10, frames = 40},  -- fruit
  {cmd = 6,  frames = 100}, -- fright
  {cmd = 7,  frames = 20},
  {cmd = 8,  frames = 100}, -- siren
  {cmd = 9,  frames = 80},  -- eyes
  {cmd = 4,  frames = 100}, -- death
  {cmd = 0,  frames = 30},
}

local idx = 1
local left = 90 -- boot settle
local need_fire = true

local function poke_latch(v)
  local cpu = manager.machine.devices[":maincpu"]
  cpu.spaces["program"]:write_u8(0x1a00, v)
end

local function on_frame()
  if idx > #sequence then
    return
  end
  if left > 0 then
    left = left - 1
    return
  end
  local step = sequence[idx]
  if need_fire then
    poke_latch(step.cmd)
    print(string.format("latch %d", step.cmd))
    need_fire = false
    left = step.frames
    return
  end
  idx = idx + 1
  need_fire = true
  left = 0
end

emu.register_frame_done(on_frame, "pcktgal_sfx_record")

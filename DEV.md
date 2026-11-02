# Developing frank-blood

The bench is frank-amiga's: see `/Volumes/1TB/Repositories/frank-amiga/DEV.md`
for the probes, consoles, captures and debugging. Bench facts are in
`tools/bench.conf`. This file covers what is specific to the game.

## Memory map (master)

| Where | What |
|---|---|
| SRAM (512 KB) | two 320x200 frame buffers (`display.c`); the renderer (`engine.c`, `a-c.c`); core 1's audio code (MultiVoc, MIDI, AL_MIDI, emu8950); hot game code (`actor`, `sectorfx`, `gameutil`, `seq`, `fx`); libc `mem*`; a few per-tick arrays (`.sram_hot`); the 16 KB NORMAL shade table; core 0's 32 KB stack at 0x20078000. About 15 KB stays free. |
| PSRAM (8 MB, 0x11000000) | the game's zero-initialised data (`.psram_bss`, ~2.5 MB), then the heap: tile cache 2.5 MB (`tile.cpp`), sound arena 1.25 MB (`sound_arena.c`), resources. The last 64 KB is `tools/sd_put.sh`'s staging area. |

All of this is set in `master/memmap_blood.ld`.

## Cores

- **Core 0:** the game.
- **Core 1:** the HSTX scanout interrupts. In its idle loop it mixes the effects (MultiVoc) and renders the music (emu8950) at 49,716 Hz into the I2S ring. Interrupts on core 1 must never wait for flash.

## Boards

Choose with `FRANK_BOARD`. Both boards share the pins: HDMI 12-19, I2S 9-11, SD SPI0 4-7, UART0 0/1.

| `FRANK_BOARD` | board | PSRAM |
|---|---|---|
| `CORE2` | FRANK Core 2 master, RP2350B | 126 MHz, derived read delay |
| `M2` | Murmulator / FRANK M2 | 168 MHz, read delay 6 (from frank-quake2) |

The PSRAM chip select is detected at run time from the package: GPIO 47 on an RP2350B, GPIO 8 on an RP2350A. The bench's M2 has an A.

The bench has one probe, the official Debug Probe in `bench.conf`'s `MASTER_PROBE`. It is now on the M2; `tools/flash.sh master ...` flashes whatever board it is on.

## Pitfalls found the hard way

- **PSRAM after another firmware:** the chip keeps QPI mode across a reset that does not cycle power. `psram_init` therefore first exits QPI and resets the chip (`psram_reset_to_spi`), and every software restart (`frank_reboot`) does the same. A chip left in QPI garbles the boot ROM's flash probe, which caused the "stuck in the boot ROM after a soft reset" hangs.

- **Per-core preinit:** SDK 2.2 runs plain `.preinit_array` entries on core 1 too, so `frank_preinit` returns early on core 1.
- **Locks:** the mixer's spinlocks must not mask interrupts (`spin_lock_unsafe_*`), or the scanout dies.
- **Flash speed:** flash runs at 100.8 MHz (/5) with QMI RXDELAY 3. Delays 2-4 read correctly; 1 does not. The old divisor-based rule (6) fails, and it also broke booting after a *soft* reset, because the timing survives the reset while clk_sys drops back. Check any change with `flashcrc` and with a SWD `reset run` boot.
- **SD speed:** the SD card runs at 31.5 MHz SPI (CRC-checked with `sdcrc`).
- **Card config:** `nblood.cfg`'s CacheSize is ignored on the board.
- **Recovering a wedged master:** `openocd ... -c "set RESCUE 1" -f target/rp2350.cfg -c init -c exit`, then flash again.

## Mac reference build

```sh
cd third_party/nblood-amiga
make PLATFORM=DARWIN NB_SMALL=1 NETCODE=0 WITHOUT_GTK=1 USE_POLYMOST=0 USE_OPENGL=0 USE_ASM=0
```

Run it headless, from a directory holding the data. Turn sound off in `nblood.cfg` (`FXDevice = -1`, `MusicDevice = -1`); a windowed run triggers macOS permission prompts.

```sh
FRANK_HEADLESS=1 FRANK_TICKS=4 FRANK_CRC=1 FRANK_DEMOMENU=0 FRANK_QUIT_AT=2600 ./nblood
```

Other variables:

| variable | does |
|---|---|
| `FRANK_DUMP=<dir>`, `FRANK_DUMP_EVERY=n` | write PPM frames |
| `FRANK_KEYS=frame:scancode:1\|0,...` | scripted keys |

A crash prints a backtrace instead of opening the crash reporter.

## Benchmark

```sh
tools/demo_bench.sh 330 log.txt
```

The script resets the master and prints each attract demo's fps and its microSD time. With `ticks=4` in `frank.cfg`, every pass renders exactly the same frames, so compare second-pass numbers; the first pass includes cache warm-up.

Profiling:
- `PROBE=E6616407E335BB29 SWD_KHZ=1000 python3 tools/profile.py <elf> 2500 0` samples core 0's PC without halting.
- `tools/sample_lr.py <elf> <function>` finds a function's callers.

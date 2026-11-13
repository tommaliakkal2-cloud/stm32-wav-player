# STM32F407 WAV Player

A bare-metal WAV player on the STM32F407 Discovery board. It reads `.wav` files off a FAT32 SD card and plays them through the board's headphone jack. The blue button starts playback and skips to the next track.

No HAL, no FatFs, no libraries: everything is register-level C, including the SD card driver and the FAT32 reader.

> Demo video: *(add link)*


## Features

- SD card over SPI (SDHC, init sequence + single-block reads)
- Hand-written read-only FAT32 reader: partition table, boot sector, root folder, FAT cluster chains
- WAV header check (44.1 kHz, 16-bit, stereo PCM)
- Audio through the onboard CS43L22 DAC, set up over I2C, samples sent over I2S3
- I2S3 fed by DMA1 with a two-half (ping-pong) buffer, so the CPU reads the card while audio plays
- Track list built from every WAV in the root folder
- Blue button: press to start, press to skip, plays the next track automatically when one ends
- `printf` debug output over UART

## Hardware

| Part | Notes |
| --- | --- |
| STM32F407G-DISC1 | MCU, CS43L22 DAC and headphone jack are onboard |
| MH SD card module | any SPI SD breakout should work |
| 32 GB SD card | formatted FAT32 |
| USB-TTL UART adapter | for debug output, 115200 baud |
| Headphones | |

### Wiring

| Board | Connects to |
| --- | --- |
| PA5 | SD SCK |
| PA6 | SD MISO |
| PA7 | SD MOSI |
| PB12 | SD CS |
| 3.3V / GND | SD 3.3V / GND |
| PA2 | UART adapter RX |
| PA3 | UART adapter TX |
| GND | UART adapter GND |

The audio side (PA4, PC7, PC10, PC12 for I2S3, PB6/PB9 for I2C1, PD4 for the DAC reset) is all wired on the board already. The SD chip select is on PB12 because PA4 is hardwired to the DAC.

## Preparing the SD card

1. Format the card as FAT32.
2. Export each song from Audacity as WAV, signed 16-bit PCM, stereo, 44100 Hz.
3. Clear all the metadata fields when exporting, so the file has the plain 44-byte header.
4. Name the files in 8.3 format, e.g. `TRACK01.WAV`, `TRACK02.WAV`, etc, and copy them to the root of the card, one at a time in order.

Files in any other format are skipped.

## Building

Built in STM32CubeIDE with `-O2`. At `-O0` the SD reads are too slow to keep up with the audio and playback stutters, so the optimisation setting matters (it's saved in `.cproject`).

## How it works

1. **Sector 0** holds the partition table. Bytes 454-457 give where the FAT32 partition starts.
2. **The boot sector** at that location gives the layout: sectors per cluster, reserved sectors, FAT size, root folder cluster. From those: `fat_start` and `data_start`.
3. **The root folder** is read 32 bytes at a time. Each entry with a `WAV` extension is saved with its first cluster.
4. **Playing a track:** the first sector is checked for `RIFF` / `WAVE` / `data` and the right format. Then the audio is streamed cluster by cluster, using the FAT to find the next cluster.
5. **DMA** loops over a 16 KB buffer, sending samples to I2S3. When it reaches the halfway point or the end, an interrupt sets a flag, and the main loop refills that half with the next part of the song.

## Things I learned

I started this project to learn the basics of STM32 and bare-metal programming from scratch, without HAL or libraries. So pretty much everything here I had to learn along the way:

**Microcontroller basics**
- Reading the reference manual and datasheet to find registers and bits
- Enabling peripheral clocks through RCC
- GPIO modes, alternate functions and pull-ups/pull-downs
- Using the CMSIS headers: peripheral structs like `SPI1->CR1` and bit masks like `SPI_CR1_SPE`
- Setting, clearing and checking bits with `|=`, `&= ~` and `&`

**Communication**
- UART: redirecting `printf` to it through `_write`
- SPI as a master, with manual chip select
- I2C, to write configuration registers on the CS43L22 DAC
- I2S for audio, including working out the PLLI2S clock settings for 44.1 kHz

**SD card and FAT32**
- The SD card SPI protocol: init sequence (CMD0, CMD8, ACMD41) and block reads (CMD17)
- Sectors vs clusters, and little-endian numbers
- How FAT32 is laid out: partition table (MBR), boot sector, FAT, data area
- Reading the root folder's directory entries and 8.3 filenames
- Following a file's cluster chain through the FAT

**Audio**
- The WAV format: RIFF chunks, the format fields, and finding the `data` chunk (and getting caught out by an extra `LIST` chunk)
- How 16-bit stereo samples are laid out and sent to a DAC

**DMA and interrupts**
- Setting up a DMA stream to feed a peripheral in circular mode
- Using half-transfer and transfer-complete interrupts for a ping-pong buffer
- Writing interrupt handlers, enabling them in the NVIC, and why shared flags need `volatile`
- Debouncing a button without blocking the main loop

**Debugging and tools**
- Finding a pin conflict (PA4 used by both the SD chip select and I2S3) and moving CS to PB12
- How the compiler optimisation level (`-O0` vs `-O2`) decided whether streaming worked
- Using git properly: commits, `.gitignore`, and keeping build files out of the repo

## Future improvements

- Move the FAT32 and player code out of `main.c` into their own files
- Long filename support
- Read root folders bigger than one sector, and subfolders (right now only the first 16 folder entries are read)
- Volume control, song seek, pause/resume etc
- Support other sample rates by changing the I2S clock per file

## AI use

The SD/SPI driver, the I2S/CS43L22 setup and the FAT32 reader are my own work. The DMA streaming and the player logic were written with help from Claude, which I also used as a tutor and code reviewer throughout the project. I also used Claude assistance for creating the README.

## Credits

Demo music: "Vivaldi – The Four Seasons" by John Harrison
Source: https://www.classicals.de · Licence: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

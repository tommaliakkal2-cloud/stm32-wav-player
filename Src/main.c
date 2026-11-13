/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Tom Maliakkal
 * @brief          : WAV player - reads WAV files from a FAT32 SD card and plays
 *                   them through the CS43L22 DAC using I2S3 + DMA.
 *                   Blue button (PA0): start, then skip to next track.
 ******************************************************************************
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__SOFT_FP__) && defined(__ARM_FP)
  #warning "FPU is not initialized, but the project is compiling for an FPU. Please initialize the FPU before use."
#endif

#define STM32F407xx
#include "stm32f4xx.h"
#include "spi.h"
#include "sd.h"
#include "audio.h"

uint8_t rx_buffer[512];          // sector 0 and boot sector
uint8_t root_dir_buffer[512];    // first sector of the root folder
uint8_t song_sector_buffer[512]; // first sector of a song, for checking its header
uint8_t FAT_sector_buffer[512];  // one sector of the FAT, for cluster lookups

#define AUDIO_BUF_SAMPLES 8192                 // 16KB buffer, 8192 x 16bit samples
#define HALF (AUDIO_BUF_SAMPLES / 2)           // each half is 4096 samples = 16 sectors
int16_t audio_buf[AUDIO_BUF_SAMPLES];          // the buffer dma keeps looping over

// set inside the interrupt, read in main loop -> needs volatile or compiler ignores changes
volatile uint8_t refill_first_half = 0;
volatile uint8_t refill_second_half = 0;

// runs when dma hits the halfway point or the end of audio_buf
// keep this short!! just set a flag, the actual refilling happens in main
void DMA1_Stream5_IRQHandler(void){
	if (DMA1->HISR & DMA_HISR_HTIF5){      // first half finished playing
		DMA1->HIFCR = DMA_HIFCR_CHTIF5;    // clear flag or this keeps firing forever
		refill_first_half = 1;
	}
	if (DMA1->HISR & DMA_HISR_TCIF5){      // second half finished, dma wraps to the top
		DMA1->HIFCR = DMA_HIFCR_CTCIF5;
		refill_second_half = 1;
	}
}

uint32_t fat_start;
uint32_t data_start;          // first sector of the data area, set in main
uint16_t sectors_per_cluster; // 32 on my card, set in main

// ---- track list, filled at boot from the root folder ----
#define MAX_TRACKS 16
uint32_t track_cluster[MAX_TRACKS];   // first cluster of each wav file
char     track_name[MAX_TRACKS][12];  // 8.3 name, eg "TRACK01 WAV" + '\0'
uint8_t  track_count = 0;
uint8_t  current_track = 0;


void USART2_SendChar(char c){
	while (!(USART2->SR & (1U <<7 )));
	USART2->DR = c;
}

char USART2_ReceiveChar(void){
	while (!(USART2->SR & (1U <<5 )));
	return (char)USART2->DR;
}

// Retarget stdio printf to USART2
int _write(int file, char *ptr, int len)
{
    for (int i = 0; i < len; i++)
    {
        USART2_SendChar(ptr[i]);
    }
    return len;
}

//read a 2-byte little-endian number starting at the given address
uint16_t read16(uint8_t *buffer){
	return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

//read a 4-byte little-endian number starting at the given address
uint32_t read32(uint8_t *buffer){
	return (uint32_t)buffer[0]
	     | ((uint32_t)buffer[1] << 8)
	     | ((uint32_t)buffer[2] << 16)
	     | ((uint32_t)buffer[3] << 24);
}

uint32_t fat_next_cluster(uint32_t cluster){

	uint32_t fat_sector = fat_start + cluster/128;//to find which sector of FAT the current cluster lies in, ie which page
	uint32_t sector_pos = (cluster % 128) * 4; //get remainder and then multiply with 4 bytes, cause each entry takes 4 bytes, ie which line in page

	 if (SD_Read_Single_Block(FAT_sector_buffer, fat_sector) != 0){
	        printf("FAT read error\r\n");
	        return 0x0FFFFFFF;          // treat an error as "end", so the loop stops
	    }
	    return read32(&FAT_sector_buffer[sector_pos]) & 0x0FFFFFFF;  // the next cluster is recorded here
	}

// ---- song reader: hands out the song's audio bytes in order ----
// hides all the sector/cluster/FAT stuff, the rest of the code just asks for bytes
uint8_t  stream_buf[512];      // the song sector we are currently reading from
uint32_t rd_cluster;           // which cluster of the song we're in
uint32_t rd_sector;            // which sector inside that cluster (0 to 31)
uint32_t rd_pos;               // where we are inside stream_buf (0 to 511)
uint32_t rd_bytes_left;        // audio bytes still to go, from the wav header

// point the reader at the start of a song
void audio_open(uint32_t first_cluster, uint32_t data_offset, uint32_t data_size){
	rd_cluster = first_cluster;
	rd_sector = 0;
	rd_pos = data_offset;          // skip the wav header (44 bytes for my files)
	rd_bytes_left = data_size;
	uint32_t lba = data_start + (rd_cluster - 2) * sectors_per_cluster;
	if (SD_Read_Single_Block(stream_buf, lba) != 0){
		printf("audio_open: read error\r\n");
		rd_bytes_left = 0;
	}
}

// copy up to n audio bytes into dst, returns how many it actually copied (0 = song over)
uint32_t audio_read(uint8_t *dst, uint32_t n){
	uint32_t copied = 0;
	while (copied < n && rd_bytes_left > 0){
		if (rd_pos == 512){                         // used up this sector, get the next one
			rd_pos = 0;
			rd_sector++;
			if (rd_sector == sectors_per_cluster){  // used up the whole cluster, ask the FAT
				rd_sector = 0;
				rd_cluster = fat_next_cluster(rd_cluster);
				if (rd_cluster >= 0x0FFFFFF8){      // chain ended early, shouldnt happen
					rd_bytes_left = 0;
					break;
				}
			}
			uint32_t lba = data_start + (rd_cluster - 2) * sectors_per_cluster + rd_sector;
			if (SD_Read_Single_Block(stream_buf, lba) != 0){
				printf("audio_read: read error\r\n");
				rd_bytes_left = 0;
				break;
			}
		}
		uint32_t chunk = 512 - rd_pos;                    // whats left in this sector
		if (chunk > n - copied) chunk = n - copied;       // dont copy more than asked
		if (chunk > rd_bytes_left) chunk = rd_bytes_left; // dont go past the end of the audio
		memcpy(dst + copied, &stream_buf[rd_pos], chunk);
		rd_pos += chunk;
		copied += chunk;
		rd_bytes_left -= chunk;
	}
	return copied;
}

uint8_t silent_halves = 0;     // how many halves we've filled with silence after the song ended

// fill one half of audio_buf with the next bit of the song
void fill_half(int16_t *dst, uint32_t count){
	uint32_t want = count * 2;                       // count is samples, audio_read wants bytes
	uint32_t got = audio_read((uint8_t *)dst, want);
	if (got < want){                                 // song ran out partway (or completely)
		memset((uint8_t *)dst + got, 0, want - got); // pad with silence so no junk plays
		if (got == 0) silent_halves++;
	}
}

// ---- track control ----

// stop dma and forget any pending refills
void stop_playback(void){
	DMA1_Stream5->CR &= ~DMA_SxCR_EN;
	while (DMA1_Stream5->CR & DMA_SxCR_EN);   // wait till its really stopped
	refill_first_half = 0;
	refill_second_half = 0;
}

// check a track's wav header, open it and start playing. returns 1 if ok, 0 if the file is no good
uint8_t start_track(uint8_t idx){
	stop_playback();
	uint32_t sector = data_start + (track_cluster[idx] - 2) * sectors_per_cluster;
	if (SD_Read_Single_Block(song_sector_buffer, sector) != 0){
		printf("%s: read error\r\n", track_name[idx]);
		return 0;
	}
	// only plain 44.1kHz / 16 bit / stereo pcm with the simple 44 byte header
	if (memcmp(&song_sector_buffer[0], "RIFF", 4) != 0 ||
	    memcmp(&song_sector_buffer[8], "WAVE", 4) != 0 ||
	    memcmp(&song_sector_buffer[36], "data", 4) != 0 ||
	    read16(&song_sector_buffer[20]) != 1 ||          // pcm
	    read16(&song_sector_buffer[22]) != 2 ||          // stereo
	    read32(&song_sector_buffer[24]) != 44100 ||      // 44.1kHz
	    read16(&song_sector_buffer[34]) != 16){          // 16 bit
		printf("%s: unsupported format, skipping\r\n", track_name[idx]);
		return 0;
	}
	audio_open(track_cluster[idx], 44, read32(&song_sector_buffer[40]));
	silent_halves = 0;
	fill_half(&audio_buf[0], HALF);        // fill both halves before starting
	fill_half(&audio_buf[HALF], HALF);
	Audio_DMA_Start(audio_buf, AUDIO_BUF_SAMPLES);
	printf("Playing %d/%d: %s\r\n", idx + 1, track_count, track_name[idx]);
	return 1;
}

// go to the next playable track, wrapping round after the last one
void next_track(void){
	for (uint8_t tries = 0; tries < track_count; tries++){
		current_track = (current_track + 1) % track_count;
		if (start_track(current_track)) return;
	}
	printf("No playable tracks\r\n");
	stop_playback();
}

// ---- blue user button on PA0 (reads 1 when pressed, board has its own pull-down) ----
uint8_t btn_prev = 0;

// returns 1 only on the moment the button goes from released to pressed
// called once per refill (~46ms apart) so contact bounce (a few ms) cant cause double presses
uint8_t button_pressed(void){
	uint8_t now = GPIOA->IDR & 1U;
	uint8_t pressed = (now && !btn_prev);
	btn_prev = now;
	return pressed;
}

// rough blocking delay, only used before playback starts
void delay_rough(uint32_t loops){
	for (volatile uint32_t i = 0; i < loops; i++);
}


int main(void)
{
	RCC->AHB1ENR |= (1U << 0); //enabling clock GPIOA (also used for the button on PA0)
	RCC->APB1ENR |= (1U << 17);//enabling  clock for USART2

	//clearing bits
	GPIOA->MODER &= ~((3U << 4) | (3U << 6));

	//setting mode to alternate function for USART2(PA2,3)
	GPIOA->MODER |= ((2U << 4) | (2U << 6));

	//selecting  AF as USART2
	GPIOA->AFR[0] &= ~((0xFU << 8) | (0xFU << 12));
	GPIOA->AFR[0] |= ((7U << 8) | (7U << 12));

	//set baud rate
	USART2->BRR = 0x8B;

	//enabling usart, transmitter and receiver
	USART2->CR1 |= ((1U << 2) | (1U << 3) | (1U << 13));

	GPIOA->MODER &= ~(3U << 0);   // PA0 as input for the button

	printf("System Boot OK! SystemCoreClocks: %lu Hz\r\n", 16000000UL);

	SPI1_Init();
	if (SD_Init() != 0){
		while(1);
	}

	SPI1_Set_Fast();
	I2S3_Init();

	// 1. sector 0 = partition table, find where the FAT32 partition starts
	if (SD_Read_Single_Block(rx_buffer, 0) != 0){
	    while(1);
	}
	uint32_t part_start = read32(&rx_buffer[454]);

	// 2. boot sector = layout of the partition
	if (SD_Read_Single_Block(rx_buffer, part_start) != 0){
	    while(1);
	}
	sectors_per_cluster       = rx_buffer[13];           //1 byte
	uint16_t reserved_sectors = read16(&rx_buffer[14]);  //bytes 14-15
	uint16_t num_fats         = rx_buffer[16];           //1 byte
	uint32_t sectors_per_fats = read32(&rx_buffer[36]);  //bytes 36-39
	uint32_t root_cluster     = read32(&rx_buffer[44]);  //bytes 44-47

	fat_start  = part_start + reserved_sectors;
	data_start = fat_start + (num_fats * sectors_per_fats);
	printf("FAT32: partition @ %lu, FAT @ %lu, data @ %lu\r\n", part_start, fat_start, data_start);

	// 3. root folder: collect every wav file into the track list
	uint32_t root_sector = data_start + (root_cluster - 2) * sectors_per_cluster;
	if (SD_Read_Single_Block(root_dir_buffer, root_sector) != 0){
	    while(1);
	}

	for (int i = 0; i < 16 && track_count < MAX_TRACKS; i++){
	    uint8_t *card = &root_dir_buffer[32 * i];

	    if (card[0] == 0x00) break;             // empty card = end of the list
	    if (card[0] == 0xE5) continue;          // deleted file
	    if (card[11] == 0x0F) continue;         // long filename piece
	    if (card[11] & 0x18) continue;          // volume label (0x08) or folder (0x10)
	    if (memcmp(&card[8], "WAV", 3) != 0) continue;   // not a wav

	    uint16_t hi = read16(&card[20]);        // high half of first cluster
	    uint16_t lo = read16(&card[26]);        // low half of first cluster
	    track_cluster[track_count] = ((uint32_t)hi << 16) | lo;
	    memcpy(track_name[track_count], card, 11);
	    track_name[track_count][11] = '\0';
	    printf("Found track %d: %s\r\n", track_count + 1, track_name[track_count]);
	    track_count++;
	}

	if (track_count == 0){
		printf("No WAV files found\r\n");
		while(1);
	}

	// 4. wait for the first button press before playing anything
	printf("Press the blue button to start\r\n");
	while (!(GPIOA->IDR & 1U));      // wait for press
	delay_rough(200000);             // let the bounce settle
	while (GPIOA->IDR & 1U);         // wait for release
	delay_rough(200000);
	btn_prev = 0;

	current_track = track_count - 1; // so next_track() lands on the first one
	next_track();

	while (1){

		if (silent_halves >= 2){                     // last audio has played, both halves silent
			printf("Track finished\r\n");
			next_track();                            // carry on with the next one
		}
		if (refill_first_half){            // dma is now playing 2nd half, safe to refill 1st
			refill_first_half = 0;
			fill_half(&audio_buf[0], HALF);
			if (button_pressed()) next_track();
		}
		if (refill_second_half){           // dma wrapped back to 1st half, refill 2nd
			refill_second_half = 0;
			fill_half(&audio_buf[HALF], HALF);
			if (button_pressed()) next_track();
		}

	}

}

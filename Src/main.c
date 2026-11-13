/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Tom Maliakkal
 * @brief          : WAV player - plays wav files off a FAT32 sd card through
 *                   the CS43L22 using I2S3 + DMA. blue button = start / next track
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

uint8_t rx_buffer[512];
uint8_t root_dir_buffer[512];
uint8_t song_sector_buffer[512];
uint8_t FAT_sector_buffer[512];

#define AUDIO_BUF_SAMPLES 8192 //16KB
#define HALF (AUDIO_BUF_SAMPLES / 2)
int16_t audio_buf[AUDIO_BUF_SAMPLES];

//changed inside the interrupt so needs volatile
volatile uint8_t refill_first_half = 0;
volatile uint8_t refill_second_half = 0;

//fires at half way and at the end of audio_buf, just set a flag and refill in main
void DMA1_Stream5_IRQHandler(void){
	if (DMA1->HISR & DMA_HISR_HTIF5){
		DMA1->HIFCR = DMA_HIFCR_CHTIF5;//clear it or it keeps firing
		refill_first_half = 1;
	}
	if (DMA1->HISR & DMA_HISR_TCIF5){
		DMA1->HIFCR = DMA_HIFCR_CTCIF5;
		refill_second_half = 1;
	}
}

uint32_t fat_start;
uint32_t data_start;
uint16_t sectors_per_cluster;

#define MAX_TRACKS 16
uint32_t track_cluster[MAX_TRACKS];
char     track_name[MAX_TRACKS][12];
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
	        return 0x0FFFFFFF;//treat error as end of chain
	    }
	    return read32(&FAT_sector_buffer[sector_pos]) & 0x0FFFFFFF;  // the next cluster is recorded here
	}

//keeps track of where we are in the song so audio_read can just hand out bytes
uint8_t  stream_buf[512];
uint32_t rd_cluster;
uint32_t rd_sector;//sector inside the cluster, 0-31
uint32_t rd_pos;//position inside stream_buf, 0-511
uint32_t rd_bytes_left;

void audio_open(uint32_t first_cluster, uint32_t data_offset, uint32_t data_size){
	rd_cluster = first_cluster;
	rd_sector = 0;
	rd_pos = data_offset;//skip the 44 byte wav header
	rd_bytes_left = data_size;
	uint32_t lba = data_start + (rd_cluster - 2) * sectors_per_cluster;
	if (SD_Read_Single_Block(stream_buf, lba) != 0){
		printf("audio_open: read error\r\n");
		rd_bytes_left = 0;
	}
}

//copies up to n bytes of the song into dst, returns how many it got, 0 means song is over
uint32_t audio_read(uint8_t *dst, uint32_t n){
	uint32_t copied = 0;
	while (copied < n && rd_bytes_left > 0){
		if (rd_pos == 512){//finished this sector, load the next one
			rd_pos = 0;
			rd_sector++;
			if (rd_sector == sectors_per_cluster){//end of cluster, ask FAT where to go next
				rd_sector = 0;
				rd_cluster = fat_next_cluster(rd_cluster);
				if (rd_cluster >= 0x0FFFFFF8){
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
		uint32_t chunk = 512 - rd_pos;
		if (chunk > n - copied) chunk = n - copied;
		if (chunk > rd_bytes_left) chunk = rd_bytes_left;//so we dont play the junk after the audio
		memcpy(dst + copied, &stream_buf[rd_pos], chunk);
		rd_pos += chunk;
		copied += chunk;
		rd_bytes_left -= chunk;
	}
	return copied;
}

uint8_t silent_halves = 0;

void fill_half(int16_t *dst, uint32_t count){
	uint32_t want = count * 2;//samples to bytes
	uint32_t got = audio_read((uint8_t *)dst, want);
	if (got < want){//song ended, fill the rest with silence
		memset((uint8_t *)dst + got, 0, want - got);
		if (got == 0) silent_halves++;
	}
}

void stop_playback(void){
	DMA1_Stream5->CR &= ~DMA_SxCR_EN;
	while (DMA1_Stream5->CR & DMA_SxCR_EN);
	refill_first_half = 0;
	refill_second_half = 0;
}

//checks the wav header and starts playing, returns 0 if the file isnt supported
uint8_t start_track(uint8_t idx){
	stop_playback();
	uint32_t sector = data_start + (track_cluster[idx] - 2) * sectors_per_cluster;
	if (SD_Read_Single_Block(song_sector_buffer, sector) != 0){
		printf("%s: read error\r\n", track_name[idx]);
		return 0;
	}
	//only 44.1kHz, 16 bit, stereo pcm with plain 44 byte header for now
	if (memcmp(&song_sector_buffer[0], "RIFF", 4) != 0 ||
	    memcmp(&song_sector_buffer[8], "WAVE", 4) != 0 ||
	    memcmp(&song_sector_buffer[36], "data", 4) != 0 ||
	    read16(&song_sector_buffer[20]) != 1 ||
	    read16(&song_sector_buffer[22]) != 2 ||
	    read32(&song_sector_buffer[24]) != 44100 ||
	    read16(&song_sector_buffer[34]) != 16){
		printf("%s: unsupported format, skipping\r\n", track_name[idx]);
		return 0;
	}
	audio_open(track_cluster[idx], 44, read32(&song_sector_buffer[40]));
	silent_halves = 0;
	fill_half(&audio_buf[0], HALF);
	fill_half(&audio_buf[HALF], HALF);
	Audio_DMA_Start(audio_buf, AUDIO_BUF_SAMPLES);
	printf("Playing %d/%d: %s\r\n", idx + 1, track_count, track_name[idx]);
	return 1;
}

void next_track(void){
	for (uint8_t tries = 0; tries < track_count; tries++){
		current_track = (current_track + 1) % track_count;//wraps back to first
		if (start_track(current_track)) return;
	}
	printf("No playable tracks\r\n");
	stop_playback();
}

uint8_t btn_prev = 0;

//only true when button goes from not pressed to pressed
//checked once per refill (~46ms) so button bounce doesnt count as 2 presses
uint8_t button_pressed(void){
	uint8_t now = GPIOA->IDR & 1U;
	uint8_t pressed = (now && !btn_prev);
	btn_prev = now;
	return pressed;
}

void delay_rough(uint32_t loops){
	for (volatile uint32_t i = 0; i < loops; i++);
}


int main(void)
{
	RCC->AHB1ENR |= (1U << 0); //enabling clock GPIOA
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

	GPIOA->MODER &= ~(3U << 0);//PA0 input for blue button

	SPI1_Init();
	if (SD_Init() != 0){
		while(1);
	}

	SPI1_Set_Fast();
	I2S3_Init();

	//sector 0 has the partition table, bytes 454-457 is where the partition starts
	if (SD_Read_Single_Block(rx_buffer, 0) != 0){
	    while(1);
	}
	uint32_t part_start = read32(&rx_buffer[454]);

	//boot sector, layout of the partition
	if (SD_Read_Single_Block(rx_buffer, part_start) != 0){
	    while(1);
	}
	sectors_per_cluster       = rx_buffer[13];
	uint16_t reserved_sectors = read16(&rx_buffer[14]);
	uint16_t num_fats         = rx_buffer[16];
	uint32_t sectors_per_fats = read32(&rx_buffer[36]);
	uint32_t root_cluster     = read32(&rx_buffer[44]);

	fat_start  = part_start + reserved_sectors;
	data_start = fat_start + (num_fats * sectors_per_fats);

	//go through the root folder and save every wav file
	uint32_t root_sector = data_start + (root_cluster - 2) * sectors_per_cluster;
	if (SD_Read_Single_Block(root_dir_buffer, root_sector) != 0){
	    while(1);
	}

	for (int i = 0; i < 16 && track_count < MAX_TRACKS; i++){
	    uint8_t *card = &root_dir_buffer[32 * i];

	    if (card[0] == 0x00) break;//empty card, nothing after this
	    if (card[0] == 0xE5) continue;//deleted
	    if (card[11] == 0x0F) continue;//long filename entry
	    if (card[11] & 0x18) continue;//volume label or folder
	    if (memcmp(&card[8], "WAV", 3) != 0) continue;

	    uint16_t hi = read16(&card[20]);
	    uint16_t lo = read16(&card[26]);
	    track_cluster[track_count] = ((uint32_t)hi << 16) | lo;//first cluster is split in 2 halves
	    memcpy(track_name[track_count], card, 11);
	    track_name[track_count][11] = '\0';
	    printf("Found track %d: %s\r\n", track_count + 1, track_name[track_count]);
	    track_count++;
	}

	if (track_count == 0){
		printf("No WAV files found\r\n");
		while(1);
	}

	printf("Press the blue button to start\r\n");
	while (!(GPIOA->IDR & 1U));
	delay_rough(200000);
	while (GPIOA->IDR & 1U);
	delay_rough(200000);
	btn_prev = 0;

	current_track = track_count - 1;//so next_track starts at track 1
	next_track();

	while (1){

		if (silent_halves >= 2){//song done, go next
			printf("Track finished\r\n");
			next_track();
		}
		if (refill_first_half){
			refill_first_half = 0;
			fill_half(&audio_buf[0], HALF);
			if (button_pressed()) next_track();
		}
		if (refill_second_half){
			refill_second_half = 0;
			fill_half(&audio_buf[HALF], HALF);
			if (button_pressed()) next_track();
		}

	}

}

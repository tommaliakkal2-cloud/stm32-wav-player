/*
 * sd.c
 *
 *  Created on: 6 Aug 2026
 *      Author: Tom Maliakkal
 */
#include <stdio.h>
#include "spi.h"
#include "sd.h"

uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc){

	uint8_t response;//to store SD card response
	uint8_t tries= 0;//to count number of tries of communication

	SD_NSS_LOW();//signal to SPI device its being talked to
	//6 btye command sending
	SPI1_Transfer(cmd);
	SPI1_Transfer((uint8_t)(arg >> 24));
	SPI1_Transfer((uint8_t)(arg >> 16));
	SPI1_Transfer((uint8_t)(arg >> 8));
	SPI1_Transfer((uint8_t)arg);
	SPI1_Transfer(crc);

	do {
		response = SPI1_Transfer(0xFF); //dummy byte to get back response
		tries++;
	}while((response & 0x80) && (tries < 0xFF ) );//exits when reponse bti starts with 0 and tries less that 255

	SD_NSS_HIGH();//CS pulled back high to signal command over

	SPI1_Transfer(0xFF);//to clear MISO line

	return (uint8_t)response;
}

uint8_t SD_Init(void){

	uint8_t response;//to store SD card response
	uint8_t tries= 0;//to count number of tries of communication
	printf("Initializing SD Card...\r\n");

//wake up with 80+ pulses with cs high
	SD_NSS_HIGH();
	for(int i = 0; i<10; i++){
		SPI1_Transfer(0xFF);
	}
	//reset and SPI mode command
	response = SD_SendCommand(CMD0,0,0x95);
	printf("CMD0 Response : 0x%02X\r\n", response);
	if (response != 0x01){
		printf("Error: CMD0 failed\r\n");
		return 1;
	}

	//voltage check
	response = SD_SendCommand(CMD8,0x000001AA,0x87);

	if (response == 0x01){
		SD_NSS_LOW();
		uint8_t b1= SPI1_Transfer(0xFF);
		uint8_t b2= SPI1_Transfer(0xFF);
		uint8_t b3= SPI1_Transfer(0xFF);
		uint8_t b4= SPI1_Transfer(0xFF);
		SD_NSS_HIGH();
		SPI1_Transfer(0XFF);

		printf("Response is %02X %02X %02X %02X %02X \r\n", response, b1,b2,b3,b4);
	}

	//

	do{
		SD_SendCommand(CMD55,0,0xFF);
		response = SD_SendCommand(ACMD41,0x40000000,0xFF);
		tries++;
	}while((response !=0x00) && tries < 1000);

	if (response == 0x00){
		printf("3. SD Card Initialized Succesfully!\r\n");
		return 0;
	}
	else{
		printf("3. SD Card Initialization failed!\r\n\r\n");
		return 1;
	}

}


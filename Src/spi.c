/*
 * spi.c
 *
 *  Created on: 4 Aug 2026
 *      Author: Tom Maliakkal
 */


#include "spi.h"
#include <stdint.h>
#include<stdio.h>

void SPI1_Init(void){


	printf("Initializing Audio Player...\r\n");

	RCC->AHB1ENR |= (1U << 0); //enabling clock GPIOA, redundancy
	RCC->APB2ENR |= (1U << 12);//enabling clock SP1

	//SD CS moved from PA4 to PB12 (PA4 is hardwired to CS43L22 I2S3_WS)
	RCC->AHB1ENR |= (1U << 1);//enabling clock GPIOB
	GPIOB->MODER &= ~(3U << (12 * 2));
	GPIOB->MODER |= (1U << (12 * 2)); //setting PB12 as GP output, we need to manually control NSS/CS
	GPIOB->BSRR = (1U << 12);//start with CS high (card not selected)

	GPIOA->MODER &= ~((3U << (5 * 2)) | (3U<< (6 * 2)) | (3U<< (7 * 2)));//cleared and set pins5,6,7 as AF
	GPIOA->MODER |= ((2U << (5 * 2)) | (2U<< (6 * 2)) | (2U<< (7 * 2)));

	GPIOA->AFR[0] &= ~((0xFU << 5*4) | (0xFU << 6*4) | (0xFU << 7*4));
	GPIOA->AFR[0] |= ((5U << 5*4) | (5U << 6*4) | (5U << 7*4));

	SPI1->CR1 = 0; //resetting CR for safety
	SPI1->CR1 |= (1U << 2);//master mode
	SPI1->CR1 |= (7U << 3);//set clock prescaler to sysclock/256
	SPI1->CR1 |= ((1U << 8) | (1U << 9));//set SSI and SSM to 1

	//remaining settings on default

	SPI1->CR1 |= (1U << 6);//enable spi
	//printf("2. SPI1 Initialized Successfully!\r\n");

}

uint8_t SPI1_Transfer(uint8_t data){
	while (!(SPI1->SR & (1U << 1)));//wait till tx buffer empty
	SPI1->DR = data;
	while (!(SPI1->SR & (1U << 0)));//wait till rx buffer full
	return (uint8_t)SPI1->DR;

}

//after initialise we need to put it to hiigh speed for data transfer
void SPI1_Set_Fast(void){
	while (SPI1->SR & (1U << 7));
	SPI1->CR1 &= ~SPI_CR1_SPE;
	SPI1->CR1 &= ~SPI_CR1_BR;
	SPI1->CR1 |= SPI_CR1_BR_0;
	SPI1->CR1 |= SPI_CR1_SPE;

}

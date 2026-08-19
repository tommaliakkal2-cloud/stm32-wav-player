/*
 * audio.c
 *
 *  Created on: 19 Aug 2026
 *      Author: Tom Maliakkal
 */

#include "audio.h"

const int16_t SINE_TABLE_1KHZ_48K[96] = {
    0, 0,
    1305, 1305,
    2588, 2588,
    3826, 3826,
    5000, 5000,
    6087, 6087,
    7071, 7071,
    7933, 7933,
    8660, 8660,
    9238, 9238,
    9659, 9659,
    9914, 9914,
    10000, 10000,
    9914, 9914,
    9659, 9659,
    9238, 9238,
    8660, 8660,
    7933, 7933,
    7071, 7071,
    6087, 6087,
    5000, 5000,
    3826, 3826,
    2588, 2588,
    1305, 1305,
    0, 0,
    -1305, -1305,
    -2588, -2588,
    -3826, -3826,
    -5000, -5000,
    -6087, -6087,
    -7071, -7071,
    -7933, -7933,
    -8660, -8660,
    -9238, -9238,
    -9659, -9659,
    -9914, -9914,
    -10000, -10000,
    -9914, -9914,
    -9659, -9659,
    -9238, -9238,
    -8660, -8660,
    -7933, -7933,
    -7071, -7071,
    -6087, -6087,
    -5000, -5000,
    -3826, -3826,
    -2588, -2588,
    -1305, -1305
};


void I2C1_Init(){
    //I2C1 hardwired to DAC, specifically B6 and 9
	//enabling clock for GPIOD,B
	RCC->AHB1ENR |= (1U << 3);
	RCC->AHB1ENR |= (1U << 1);
	RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;//alternate way to enable registers


	//setting PD4 as GPO
	GPIOD->MODER &= ~(3U << (4 *2));
	GPIOD->MODER |= (1U << (4 *2));

	//resetting the DAC
	GPIOD->ODR &= ~(1U << 4);
	for(volatile int i=0; i<50000; i++);
	GPIOD->ODR |= (1U << 4);


	//setting Pb6,9 as AF4
	GPIOB->MODER &= ~(3U << (6*2));
	GPIOB->MODER &= ~(3U << (9*2));

	GPIOB->MODER |= (2U << (6*2));
	GPIOB->MODER |= (2U << (9*2));

	GPIOB->AFR[0] &= ~(0xFU << (6*4));
	GPIOB->AFR[1] &= ~(0xFU << 4);
	GPIOB->AFR[0] |= (4U << (6*4));
	GPIOB->AFR[1] |= (4U << (4));

	//setting PB6,9 as opendrain and pull up mode for I2C
	GPIOB->OTYPER |= ((1U << 6) | (1U << 9));

	GPIOB->PUPDR &= ~((3U << (6*2)) | (3U << (9*2)));
	GPIOB->PUPDR |= ((1U << (6*2)) | (1U << (9*2)));

	//initializing I2C1
	I2C1->CR1 |= I2C_CR1_SWRST;
	I2C1->CR1 &= ~(I2C_CR1_SWRST);

	//values are calculated using some equations
	I2C1->CR2 |= 16;//peripheral clock freq(16Mhz)
	I2C1->CCR |= 80;//scl clock
	I2C1->TRISE |= 17;//max rise time

	I2C1->CR1 |= I2C_CR1_PE ;//peripheral clock freq(16Mhz)

}

void I2C1_Write(uint8_t reg_addr, uint8_t data){
	while(I2C1->SR2 & I2C_SR2_BUSY);
	I2C1->CR1 |= I2C_CR1_START;
	while(!(I2C1->SR1 & I2C_SR1_SB));
	I2C1->DR = 0x94;
	while(!(I2C1->SR1 & I2C_SR1_ADDR));
	(void)I2C1->SR1;
	(void)I2C1->SR2;
	while(!(I2C1->SR1 & I2C_SR1_TXE));
	I2C1->DR = reg_addr;
	while(!(I2C1->SR1 & I2C_SR1_TXE));
	I2C1->DR = data;
	while(!(I2C1->SR1 & I2C_SR1_BTF));
	I2C1->CR1 |= I2C_CR1_STOP;
}

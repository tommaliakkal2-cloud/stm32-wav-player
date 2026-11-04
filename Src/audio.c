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
	I2C1->CR2 = 16;//peripheral clock freq(16Mhz)
	I2C1->CCR = 80;//scl clock
	I2C1->TRISE = 17;//max rise time

	I2C1->CR1 |= I2C_CR1_PE ;//peripheral enable

}

void I2C1_Write(uint8_t reg_addr, uint8_t data){
	while(I2C1->SR2 & I2C_SR2_BUSY);//wait till not busy
	I2C1->CR1 |= I2C_CR1_START;//start condition
	while(!(I2C1->SR1 & I2C_SR1_SB));//wait till start bit 1
	I2C1->DR = 0x94;//address of peripheral
	while(!(I2C1->SR1 & I2C_SR1_ADDR));//wait till address match bit
	(void)I2C1->SR1;//read SR1 and 2, part of sequence, required
	(void)I2C1->SR2;
	while(!(I2C1->SR1 & I2C_SR1_TXE));//when transmit empty, send addr, then data
	I2C1->DR = reg_addr;
	while(!(I2C1->SR1 & I2C_SR1_TXE));
	I2C1->DR = data;
	while(!(I2C1->SR1 & I2C_SR1_BTF));//wait till data transfer complere then stop condition
	I2C1->CR1 |= I2C_CR1_STOP;
}

void CS43L22_Init(){

	I2C1_Init();
	I2C1_Write(0x02,0x01);//keep dac powered down while configuring
	I2C1_Write(0x04,0xAF);//headphone on, speaker off.
	I2C1_Write(0x05,0x80);//setting clock control to Auto
	I2C1_Write(0x06,0x07);//settign mode as std I2S, 16bit length

	//start up sequence
	I2C1_Write(0x00, 0x99);
	I2C1_Write(0x47, 0x80);
	I2C1_Write(0x32, 0xBB);
    I2C1_Write(0x32, 0x3B);
	I2C1_Write(0x00, 0x00);

	I2C1_Write(0x20, 0x00); // Master Volume A 0db
	I2C1_Write(0x21, 0x00); // Master Volume B

	I2C1_Write(0x22, 0xE0); // Headphone A Volume, very low for testing i dont want my eardrums to bleed.
	I2C1_Write(0x23, 0xE0); // Headphone B Volume

	I2C1_Write(0x02,0x9E);//power on DAC

}

void CS43L22_PinConfig(){
	RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;//i2s shares block with spi
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;//enabling gpioa again redundancy
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

	GPIOA->MODER &= ~(3U << (4 * 2));
	GPIOC->MODER &= ~((3U << (7 * 2)) | (3U << (10 * 2)) | (3U << (12 * 2)));

	GPIOA->MODER |= (2U << (4 * 2));
	GPIOC->MODER |= ((2U << (7 * 2)) | (2U << (10 * 2)) | (2U << (12 * 2)));

	GPIOA->OSPEEDR |= (2U << (4 * 2));//speed high as clocks are high
	GPIOC->OSPEEDR |= ((2U << (7 * 2)) | (2U << (10 * 2)) | (2U << (12 * 2)));

	GPIOA->AFR[0] &= ~(0xFU << (4 * 4));
	GPIOC->AFR[0] &= ~(0xFU << (7 * 4));
	GPIOC->AFR[1] &= ~((0xFU << (2 * 4)) | (0xFU << (4 * 4)));

	GPIOA->AFR[0] |= (6U << (4 * 4));
	GPIOC->AFR[0] |= (6U << (7 * 4));
	GPIOC->AFR[1] |= ((6U << (2 * 4)) | (6U << (4 * 4)));
}

void I2S3_Init(){
	CS43L22_PinConfig();
	RCC->CR &= ~RCC_CR_PLLI2SON;
	RCC->PLLI2SCFGR &= ~((0x1FFU << 6) | (7U << 28));
	RCC->PLLI2SCFGR |= ((258U << 6) | (3U << 28));
	RCC->CR |= RCC_CR_PLLI2SON;
	while(!(RCC->CR & RCC_CR_PLLI2SRDY));
	SPI3->I2SPR = (SPI_I2SPR_MCKOE | SPI_I2SPR_ODD | (3U << SPI_I2SPR_I2SDIV_Pos));
	SPI3->I2SCFGR = 0;
	SPI3->I2SCFGR |= (SPI_I2SCFGR_I2SMOD | (2U <<SPI_I2SCFGR_I2SCFG_Pos));
	SPI3->I2SCFGR |= SPI_I2SCFGR_I2SE;
	CS43L22_Init();

}




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


void I2S3_Init(){

	//enabling clock for GPIOD,B
	RCC->AHB1ENR |= (1U << 3);
	RCC->AHB1ENR |= (1U << 1);

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


}

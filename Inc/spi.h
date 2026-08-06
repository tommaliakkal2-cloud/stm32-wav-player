/*
 * spi.h
 *
 *  Created on: 4 Aug 2026
 *      Author: Tom Maliakkal
 */

#ifndef SPI_H_
#define SPI_H_

#ifndef STM32F407xx
#define STM32F407xx
#endif
#include "stm32f4xx.h"

#define SD_NSS_LOW() (GPIOA->BSRR = (1U << 4 + 16)) //set CS low to start data transfer
#define SD_NSS_HIGH() (GPIOA->BSRR = (1U << 4 )) //set CS high to stop

void SPI1_Init(void);
uint8_t SPI1_Transfer(uint8_t data);

#endif /* SPI_H_ */

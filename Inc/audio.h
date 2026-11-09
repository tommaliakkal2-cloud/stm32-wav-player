/*
 * audio.h
 *
 *  Created on: 19 Aug 2026
 *      Author: Tom Maliakkal
 */

#ifndef AUDIO_H_
#define AUDIO_H_

#ifndef STM32F407xx
#define STM32F407xx
#endif
#include "stm32f4xx.h"

extern const int16_t SINE_TABLE_1KHZ_48K[96];
void I2C1_Init();
void I2C1_Write(uint8_t reg_addr, uint8_t data);
void CS43L22_Init();
void CS43L22_PinConfig();
void I2S3_Init();
void Audio_DMA_Start(int16_t *buf, uint16_t count);

#endif /* AUDIO_H_ */

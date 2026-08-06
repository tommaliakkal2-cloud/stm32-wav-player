/*
 * sd.h
 *
 *  Created on: 6 Aug 2026
 *      Author: Tom Maliakkal
 */

#ifndef SD_H_
#define SD_H_

#include "spi.h"
#include <stdint.h>

#define CMD0   (0x40 + 0)
#define CMD8   (0x40 + 8)
#define CMD55  (0x40 + 55)
#define ACMD41 (0x40 + 41)

uint8_t SD_Init(void);
uint8_t SD_SendCommand(uint8_t cmd,uint32_t arg,uint8_t crc);

#endif /* SD_H_ */

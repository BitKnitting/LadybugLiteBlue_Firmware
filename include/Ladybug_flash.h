/*
 * Ladybug_flash.h
 *
 *  Created on: Nov 13, 2015
 *      Author: margaret
 */

#ifndef INCLUDE_LADYBUG_FLASH_H_
#define INCLUDE_LADYBUG_FLASH_H_
#include "pstorage.h"
#include "Ladybug_Hydro.h"

void flash_read(flash_rw_t data_type_to_read,uint8_t *p_bytes);

#endif /* INCLUDE_LADYBUG_FLASH_H_ */

/**
 * \file		Ladybug_Flash.h
 * \author	Margaret Johnson
 * \date		Jan 4, 2016
 * \sa		Ladybug_Flash.c
 */

#ifndef INCLUDE_LADYBUG_FLASH_H_
#define INCLUDE_LADYBUG_FLASH_H_
#include <stdint.h>
#include "pstorage.h"
/**
 * \brief The amount of bytes assigned to a Flash block handle.  32 is used because it is the bigger of the size of bytes
 * 	  needed between plant_info_data_t,pH_data_t, and m_device_name
 */
#define BLOCK_SIZE		32
/**
 * \brief This enum lets the function know which data structure to read from or write to flash
 * */
typedef enum {
  hydroValues,
  plantInfo,
  deviceName,
  calibrationValues
}flash_rw_t;
void ladybug_flash_init(void);
void ladybug_flash_read(flash_rw_t data_to_read,uint8_t *p_bytes_to_read);
void ladybug_flash_write(flash_rw_t what_data_to_write, uint8_t *p_bytes_to_write,pstorage_size_t num_bytes_to_write);
void ladybug_flash_handler(pstorage_handle_t  * handle,
				uint8_t              op_code,
				uint32_t             result,
				uint8_t            * p_data,
				uint32_t             data_len);
#endif /* INCLUDE_LADYBUG_FLASH_H_ */

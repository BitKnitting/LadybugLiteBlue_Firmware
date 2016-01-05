/**
 * \file		Ladybug_Flash.c
 * \author	Margaret Johnson
 * \version	1.0
 * \brief	Functions that read/write to/from the nRF51822's Flash.
 * \date		Jan 4, 2016
 */
#include "Ladybug_Flash.h"
#include "Ladybug_error.h"
#include "SEGGER_RTT.h"
#include "Ladybug_Hydro.h"
static pstorage_handle_t			m_block_calibration_store_handle;
static pstorage_handle_t			m_block_plantInfo_store_handle;
static pstorage_handle_t			m_block_device_name_store_handle;
static uint8_t 				m_mypstorage_wait_flag;
/**
 * \callgraph
 * \brief This is the callback function that pstorage calls back to when a Flash event has completed for the block handle
 * 	  a request was made for.  The callback function is set in ladybug_flash_init() in the pstorage_param.cb parameter
 * \sa	  ladybug_flash_init()
 * @param handle
 * @param op_code
 * @param result
 * @param p_data
 * @param data_len
 */
void ladybug_flash_handler(pstorage_handle_t  * handle,
				uint8_t              op_code,
				uint32_t             result,
				uint8_t            * p_data,
				uint32_t             data_len)
{
  SEGGER_RTT_WriteString(0,"---> in flash_handler\n");
  //check if there is an error.  If so, the code will go to the error handler
  APP_ERROR_CHECK(result);
  //now that the error has been checked, set the waiting flag to "off" since
  //this is a way to let the code that made the flash action request that the
  //request is finished.
  //since i am using only one block, i don't need to associate with a block id...
  m_mypstorage_wait_flag = 0;
  //for testing purposes, see what op code was being handled...
  switch (op_code) {
    case PSTORAGE_LOAD_OP_CODE:
      SEGGER_RTT_WriteString(0,".... READ flash\n");
      break;
    case PSTORAGE_STORE_OP_CODE:
      SEGGER_RTT_WriteString(0,".... WRITE flash\n");
      break;
    case PSTORAGE_UPDATE_OP_CODE:
      SEGGER_RTT_WriteString(0,".... UPDATE flash\n");
      break;
    case PSTORAGE_CLEAR_OP_CODE:
      SEGGER_RTT_WriteString(0,".... CLEAR flash\n");
      break;


    default:
      break;
  }
}
/**
 * \callgraph
 *\brief 	Initialize the blocks of flash memory used for reading/writing.
 *\details	Uses Nordic's pstorage software abstraction/API to cleanly access flash when the SoftDevice (for BLE)
 *		is also running.
 */
void ladybug_flash_init() {
  pstorage_module_param_t pstorage_param;   //Used when registering with pstorage
  pstorage_handle_t	  handle;	    //used to access the chunk-o-flash requested when registering
  //First thing is to initialize pstorage
  uint32_t err_code = pstorage_init();
  //if initialization was not successful, the error routine will be called and the code won't proceed.
  APP_ERROR_CHECK(err_code);
  //Next is to register amount of flash needed.  The smallest amount that can be requested is 16 bytes.  I'm requesting
  //32 bytes because this is the number of bytes needed for plant_info. Two blocks are used...one for plant info and one for hydro values.  I must make a
  //block request for the larger amount of bytes
  pstorage_param.block_size = BLOCK_SIZE;
  //request three blocks - one will be for pH, one for plant_info, and one for device name
  pstorage_param.block_count = 3;
  //assign a callback so know when a command has finished.
  pstorage_param.cb = ladybug_flash_handler;
  err_code = pstorage_register(&pstorage_param, &handle);
  APP_ERROR_CHECK(err_code);
  //Then get the handles to the blocks of flash
  pstorage_block_identifier_get(&handle, 0, &m_block_calibration_store_handle);
  pstorage_block_identifier_get(&handle,1,&m_block_plantInfo_store_handle);
  pstorage_block_identifier_get(&handle,2,&m_block_device_name_store_handle);

  APP_ERROR_CHECK(err_code);
}

/***
 * The Ladybug stores info that is maintained across restarts of the device.  This info includes the device name, plant info (type of plant and growth stage), as well as
 * calibration values.  This function figures out what block handle has been initialized based on what info has been requested to read, reads the info from flash, then writes
 * the info to the p_bytes_to_read memory buffer.
 * @param data_to_read  		let the function know what type of data to read
 * @param p_bytes_to_read	give the function a buffer to write the data after reading from flash
 * @sa	LADYBUG_ERROR_UNSURE_WHAT_DATA_TO_READ
 */
void ladybug_flash_read(flash_rw_t data_to_read,uint8_t *p_bytes_to_read){
  m_mypstorage_wait_flag = 1;
  pstorage_handle_t * p_handle;
  //set the block handle to the block of flash set aside in flash_init for the data type to read
  //point the bytes to the location holding the bytes in memory.
  switch (data_to_read) {
    case plantInfo:
      p_handle = &m_block_plantInfo_store_handle;
      break;
    case calibrationValues:
      p_handle = &m_block_calibration_store_handle;
      break;
    case deviceName:
      p_handle = &m_block_device_name_store_handle;
      break;
    default:
      //this is an error case.  The function doesn't know what to read.
      APP_ERROR_CHECK(LADYBUG_ERROR_UNSURE_WHAT_DATA_TO_READ);
      break;
  }
  uint32_t err_code = pstorage_load(p_bytes_to_read, p_handle, BLOCK_SIZE, 0);
  APP_ERROR_CHECK(err_code);
  while(m_mypstorage_wait_flag) { }
}
/**
 * \callgraph
 * \brief	When the Ladybug needs to store info, it calls the flash_write routine.
 * \details	This routine assumes the flash storage to be used has been initialized by a call to flash_init.  Match the handle to
 * 		flash storage to the info the caller wants to write.
 * \note		As directed by the nRF51822 documentation, the flash storage is first cleared before a write to flash happens.
 * @param what_data_to_write	Whether to write out plant info, calibration values, or the device name.
 * @param p_bytes_to_write	A pointer to the bytes to be written to flash.
 * @param num_bytes_to_write	The number of bytes to write to flash
 */
void ladybug_flash_write(flash_rw_t what_data_to_write, uint8_t *p_bytes_to_write,pstorage_size_t num_bytes_to_write){
  //must clear before write (or get unpredictable results)
  m_mypstorage_wait_flag = 1;
  pstorage_handle_t *p_handle;
  switch (what_data_to_write) {
    case plantInfo:
      p_handle = &m_block_plantInfo_store_handle;
      break;
    case calibrationValues:
      p_handle = &m_block_calibration_store_handle;
      break;
    case deviceName:
      p_handle = &m_block_device_name_store_handle;
      break;
    default:
      APP_ERROR_CHECK(LADYBUG_ERROR_UNSURE_WHAT_DATA_TO_WRITE);
      break;
  }
  //clearing the pstorage/flash sets the bytes to 0xFF
  uint32_t err_code = pstorage_clear(p_handle, BLOCK_SIZE);
  APP_ERROR_CHECK(err_code);
  while(m_mypstorage_wait_flag) {  }
  m_mypstorage_wait_flag = 1;
  err_code = pstorage_store(p_handle, p_bytes_to_write, num_bytes_to_write, 0);
  APP_ERROR_CHECK(err_code);
  while(m_mypstorage_wait_flag) {  }
}



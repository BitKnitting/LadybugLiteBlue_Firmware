/**
 * \file		Ladybug_Flash.c
 * \author	Margaret Johnson
 * \version	1.0
 * \brief	Functions that read/write to/from the nRF51822's Flash.
 * \date		Jan 4, 2016
 */
#define	DEBUG	///< Used in app_error.h to give line / function name input.

#include "Ladybug_Flash.h"
#include "app_timer.h"
#include "app_error.h"
#include "Ladybug_Error.h"
#include "SEGGER_RTT.h"
#include "Ladybug_Hydro.h"
static pstorage_handle_t			m_block_calibration_store_handle;
static pstorage_handle_t			m_block_plantInfo_store_handle;
static pstorage_handle_t			m_block_device_name_store_handle;
static uint8_t 				m_mypstorage_wait_flag;
static app_timer_id_t                   m_timer_id;   /**< identifies this timer in the timer queue (only one in queue so...) */
/*!
 * \brief
 *  m_return: 	pointer to the call back function passed into ladybug_flash_write().
 */
void (*m_flash_return)(uint32_t);
/**
 * \callgraph
 * \brief Called by the system when the timer goes off.  If the timer goes off, this means the Flash activity didn't complete.  The caller
 * is notified by calling the caller's callback with an error code.
 * @param p_context
 */
static void timeout_handler(void * p_context)
{
  SEGGER_RTT_WriteString (0, "--> in timeout handler\n");
  //  UNUSED_PARAMETER(p_context);

  // Call back the caller with an error
  uint32_t err_code = LADYBUG_ERROR_FLASH_ACTION_NOT_COMPLETED;
  m_flash_return(err_code);
}
/**
 * \callgraph
 * \brief create a timer to be used before a call to write/read to flash in case the flash action doesn't happen it won't hang the system.
 */
void create_timer() {
  SEGGER_RTT_WriteString (0, "--> in create_timer\n");
  uint32_t err_code = app_timer_create(&m_timer_id,APP_TIMER_MODE_SINGLE_SHOT, timeout_handler);
  APP_ERROR_CHECK(err_code);
}
/**
 * \callgraph
 * \brief Start the timer before calling into pstorage asking for a flash event.
 */
void start_timer() {
  SEGGER_RTT_WriteString (0, "--> in start_timer\n");
  uint32_t err_code;
  static const uint32_t wait_time_for_flash_event_to_complete_ms = 5000; ///< Wait 5 seconds before timing out from waiting for a flash event to complete
  static const uint32_t app_timer_prescaler = 0; ///< Counter overflows after 512s when prescaler = 0
  uint32_t timer_ticks = APP_TIMER_TICKS(wait_time_for_flash_event_to_complete_ms, app_timer_prescaler);
  // Start application timers.
  err_code = app_timer_start(m_timer_id, timer_ticks, NULL);
  APP_ERROR_CHECK(err_code);
}
/**
 * \callgraph
 * \brief Called to stop the timer from firing.
 */
void stop_timer() {
  SEGGER_RTT_WriteString (0, "--> in stop_timer\n");
  uint32_t err_code = app_timer_stop(m_timer_id);
  APP_ERROR_CHECK(err_code);
}

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
  SEGGER_RTT_WriteString(0,"==> IN ladybug_flash_init\n");
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
  // Create the timer.  This will be called before a Flash activity is requested to avoid forever hanging.
  create_timer();

}

/***
 * The Ladybug stores info that is maintained across restarts of the device.  This info includes the device name, plant info (type of plant and growth stage), as well as
 * calibration values.  This function figures out what block handle has been initialized based on what info has been requested to read, reads the info from flash, then writes
 * the info to the p_bytes_to_read memory buffer.
 * @param data_to_read  		let the function know what type of data to read
 * @param p_bytes_to_read	give the function a buffer to write the data after reading from flash
 * @sa	LADYBUG_ERROR_UNSURE_WHAT_DATA_TO_READ
 */
void ladybug_flash_read(flash_rw_t data_to_read,uint8_t *p_bytes_to_read,void(*did_flash_action)(uint32_t err_code)){
  SEGGER_RTT_WriteString(0,"==> IN ladybug_flash_read\n");
  pstorage_handle_t * p_handle;
  m_flash_return = did_flash_action;
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
      APP_ERROR_CHECK(LADYBUG_ERROR_FLASH_UNSURE_WHAT_DATA_TO_READ);
      break;
  }
  // There is a chance the Flash activity doesn't happen so we use a timer to prevent waiting forever.
  m_mypstorage_wait_flag = 1;
  start_timer();
  uint32_t err_code = pstorage_load(p_bytes_to_read, p_handle, BLOCK_SIZE, 0);
  while(m_mypstorage_wait_flag) { }
  APP_ERROR_CHECK(err_code);
  // Flash activity completed so stop timer.
  stop_timer();
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
 * @param did_flash_action	Function caller passes in to be informed of the outcome of the Flash request.  The pointer to the function must be valid.
 */
void ladybug_flash_write(flash_rw_t what_data_to_write, uint8_t *p_bytes_to_write,pstorage_size_t num_bytes_to_write,void(*did_flash_action)(uint32_t err_code)){
  SEGGER_RTT_WriteString(0,"==> IN ladybug_flash_write\n");
  // Check parameters.  The what_data_to_write param is checked below in the switch statement.
  if (p_bytes_to_write == NULL || (did_flash_action == NULL)){
      APP_ERROR_HANDLER(LADYBUG_ERROR_NULL_POINTER);
  }
  if (num_bytes_to_write <= 0){
      APP_ERROR_HANDLER(LADYBUG_ERROR_NUM_BYTES_TO_WRITE);
  }
  // Set the static variable used to hold the location of the callback function to the pointer passed in by the caller.
  m_flash_return = did_flash_action;
  // Must clear the Flash block before write (or get unpredictable results).

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
      APP_ERROR_HANDLER(LADYBUG_ERROR_INVALID_COMMAND);
      break;
  }
  //clearing the pstorage/flash sets the bytes to 0xFF
  // m_mypstorage_wait_flag is set to 0 within ladybug_flash_handler().  This way the system lets us know the Flash activity happened.
  m_mypstorage_wait_flag = 1;
  // There is a chance the Flash activity doesn't happen so we use a timer to prevent waiting forever.
  start_timer();
  uint32_t err_code = pstorage_clear(p_handle, BLOCK_SIZE);
  while(m_mypstorage_wait_flag) {  }
  APP_ERROR_CHECK(err_code);
  // The flash action happened, so turn off the timer before it goes off.
  stop_timer();
  // Start the timer up again to timeout if writing to flash doesn't happen
  start_timer();
  m_mypstorage_wait_flag = 1;
  err_code = pstorage_store(p_handle, p_bytes_to_write, num_bytes_to_write, 0);
  while(m_mypstorage_wait_flag) {  }
  APP_ERROR_CHECK(err_code);
  stop_timer();
}



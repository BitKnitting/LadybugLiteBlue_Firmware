/**
 * \file main.c
 * \author Margaret Johnson
 * \version 0.1
 * \brief Starting point for the Ladybug Blue Lite firmware.
 * The LBL reads the pH and EC levels in a nutrient bath.  It then
 sets characteristics within a BLE Service so a smartphone BLE app (or others that "speak" to BLE services) can retrieve the values over BLE.  The battery level can also be detected.  The
 initial "core" of the app came from following Nordic's nAN-36v1.1.pdf creating Bluetooth Low Energy Applications Using nRF51822
 \copyright October, 2015
 \note	    Used Nordic's nRF51 SDK v9
 \note	    Used gcc-arm-none-eabi-4_9-2015q1
 \note	    Used Eclipse Luna
 \todo TBD: Use the LED to give feedback on the state of the Ladybug.
 */

#include <stdint.h>
#include "nordic_common.h"
#include "softdevice_handler.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "app_timer.h"
#include "Ladybug_BLE.h"
#include "Ladybug_Flash.h"
#include "Ladybug_Hydro.h"
#include "SEGGER_RTT.h"

/**
 *   \brief if 0, the characteristics of a service can't be added/removed/modified.  The central can figure out if a service characteristic's have changed.
 *
 *      if 1, the service can change characteristics
 */
#define IS_SRVC_CHANGED_CHARACT_PRESENT 0
/**
 * \brief the interval between the LBL sending out advertising packets.  According to the nAN-36v1.1.pdf, the interval can range from 20 ms to 10.24s
 * \note I don't have enough experience with BLE peripheral firmware at this point to question battery consumption because of BLE advertising.  So the current setting is my best guess.  There is a blog post on the Nordic devzone about "typical" current consumption:
 * https://devzone.nordicsemi.com/blogs/679/nrf51-current-consumption-for-common-scenarios/   ...given the info in this blog post, I decided to set the advertising interval to 2 seconds. This can be fine tuned, but my take was to reduce current consumption by minimizing advertising.
 * \note the interval is in units of 0.625ms.  A value of 3,200 = 3,200*.625 -> 2,000 ms = 2 seconds
 */
#define APP_ADV_INTERVAL                3200
/**
 * \brief the value for advertising time out can be between 1 and 0x3FFF seconds.
 * \note setting this value to 0 says "don't time out"...so i'm doing this because i want the LBL to always be discoverable (another reason why the adv interval should be large)
 */
#define APP_ADV_TIMEOUT_IN_SECONDS      0
#define APP_TIMER_PRESCALER             0                                           /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_MAX_TIMERS            4                                           /**< BLE uses at least two timers... I set it to 4 (arbitrary) to give room...Bummer that all app timers need to be initialized here...A bit of a black art */
#define APP_TIMER_OP_QUEUE_SIZE         4                                           /**< Size of timer operation queues. (copied from SDK examples) */
static ble_gap_sec_params_t             m_sec_params;                               /**< Security requirements for this application. (copied from SDK examples)*/
static uint16_t                         m_conn_handle = BLE_CONN_HANDLE_INVALID;    /**< Handle of the current connection. (copied from SDK examples)*/
static ble_lbl_t                        m_lbl;					    /**< Instance of the BLE Hydro service */
#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(500, UNIT_1_25_MS)            /**< Minimum acceptable connection interval (0.5 seconds). (copied from SDK examples)*/
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(1000, UNIT_1_25_MS)           /**< Maximum acceptable connection interval (1 second). (copied from SDK examples)*/
#define SLAVE_LATENCY                   0                                           /**< Slave latency. (..gee great description :-) ! copied from SDK examples)*/
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds). (copied from SDK examples)*/
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(20000, APP_TIMER_PRESCALER) /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (15 seconds). (copied from SDK examples)*/
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)  /**< Time between each call to sd_ble_gap_conn_param_update after the first call (5 seconds).(copied from SDK examples) */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. (copied from SDK examples)*/
#define SEC_PARAM_TIMEOUT               30                                          /**< Timeout for Pairing Request or Security Request (in seconds). (copied from SDK examples)*/
#define SEC_PARAM_BOND                  1                                           /**< Perform bonding. (copied from SDK examples)*/
#define SEC_PARAM_MITM                  0                                           /**< Man In The Middle protection not required. (copied from SDK examples)*/
#define SEC_PARAM_IO_CAPABILITIES       BLE_GAP_IO_CAPS_NONE                        /**< No I/O capabilities. (copied from SDK examples)*/
#define SEC_PARAM_OOB                   0                                           /**< Out Of Band data not available. (copied from SDK examples)*/
#define SEC_PARAM_MIN_KEY_SIZE          7                                           /**< Minimum encryption key size. (copied from SDK examples)*/
#define SEC_PARAM_MAX_KEY_SIZE          16                                          /**< Maximum encryption key size. (copied from SDK examples)*/
#define DEAD_BEEF                       0xDEADBEEF                                  /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */



/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num   Line number of the failing ASSERT call.
 * @param[in] file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
  app_error_handler(DEAD_BEEF, line_num, p_file_name);
}
/**@brief Function for error handling, which is called when an error has occurred.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of error.
 * \note I decided to use the SEGGER_RTT printing functionality.  My understanding is the SEGGER_RTT calls can be left in code with no effect
 * when there is no terminal to output.  The con of this approach is I can't hook up a UART enabled terminal session and see what is going on without the debugger present.
 *
 * @param[in] error_code  Error code supplied to the handler.
 * @param[in] line_num    Line number where the handler is called.
 * @param[in] p_file_name Pointer to the file name.
 */
void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name)
{
  // This call can be used for debug purposes during application development.
  // The SEGGER_RTT APIs will not cause a problem in production so I'm leaving them in.
  SEGGER_RTT_WriteString(0,"--->>>BUMMER!! In app_error_handler\n");
  SEGGER_RTT_printf(0,"error code: %d (or 0X%X if assert..base for BLE = 0x3000) Line number: %d file name: ",error_code,error_code,line_num);
  SEGGER_RTT_WriteString(0,p_file_name);
  SEGGER_RTT_WriteString(0,"\n");
  //  ble_debug_assert_handler(error_code, line_num, p_file_name);
}
/**@brief Function for initializing security parameters.
 * \callgraph
 */
static void sec_params_init(void)
{

  m_sec_params.bond         = SEC_PARAM_BOND;
  m_sec_params.mitm         = SEC_PARAM_MITM;
  m_sec_params.io_caps      = SEC_PARAM_IO_CAPABILITIES;
  m_sec_params.oob          = SEC_PARAM_OOB;
  m_sec_params.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
  m_sec_params.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
}
/**@brief Function for the Power manager.
 * \note notice how sd_app_evt_wait() is used instead of using the ARM instruction of either WFI or WFE (to go to low-power standby state).  This is because
 * the app shares space with the softdevice.  The softdevice handles events and then calls us when there is an event for us.
 * \callgraph
 */
static void power_manage(void)
{
  uint32_t err_code = sd_app_evt_wait();
  APP_ERROR_CHECK(err_code);
}
/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 *          are passed to the application.
 *          @note All this function does is to disconnect. This could have been done by simply
 *                setting the disconnect_on_fail config parameter, but instead we use the event
 *                handler mechanism to demonstrate its use.
 *
 * @param[in]   p_evt   Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
  uint32_t err_code;

  if(p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
      err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
      APP_ERROR_CHECK(err_code);
    }
}
/**
 * \callgraph
 * @brief Function for handling a Connection Parameters error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
  APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 * \callgraph
 */
static void conn_params_init(void)
{
  uint32_t               err_code;
  ble_conn_params_init_t cp_init;

  memset(&cp_init, 0, sizeof(cp_init));

  cp_init.p_conn_params                  = NULL;
  cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
  cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
  cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
  cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
  cp_init.disconnect_on_fail             = false;
  cp_init.evt_handler                    = on_conn_params_evt;
  cp_init.error_handler                  = conn_params_error_handler;

  err_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(err_code);
}
/**
 * \callgraph
 * @brief initialize the Ladybug Lite's BLE service /characteristics.  A "global handle" - m_lbl is used to reference the structure containing all the goo needed when responding to
 * an event directed towards the LBL service.
 */
static void service_init(void)
{
  uint32_t err_code;

  err_code = ladybug_BLE_init(&m_lbl);
  APP_ERROR_CHECK(err_code);
}
/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
  uint32_t err_code;

  switch (ble_adv_evt)
  {
    case BLE_ADV_EVT_FAST:
      break;
    case BLE_ADV_EVT_IDLE:
      power_manage();
      break;
    default:
      break;
  }
}
/**@brief Function for initializing the Advertising functionality.
 */
void advertising_init(void)
{
  uint32_t      err_code;
  ble_advdata_t advdata;
  ble_uuid_t adv_uuids[] = {{LBL_UUID_SERVICE,m_lbl.uuid_type}};
  // Build and set advertising data
  memset(&advdata, 0, sizeof(advdata));

  advdata.name_type               = BLE_ADVDATA_FULL_NAME;
  advdata.include_appearance      = false; //samples typically set this to true.  I don't see why adv bytes should be wasted on this...
  advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
  advdata.uuids_complete.uuid_cnt = sizeof(adv_uuids)/sizeof(adv_uuids[0]);
  advdata.uuids_complete.p_uuids  = adv_uuids;
  //I'm setting these to the same as in the ble_app_template example
  ble_adv_modes_config_t options = {0};
  options.ble_adv_fast_enabled  = BLE_ADV_FAST_ENABLED;
  options.ble_adv_fast_interval = APP_ADV_INTERVAL;
  options.ble_adv_fast_timeout  = APP_ADV_TIMEOUT_IN_SECONDS;
  err_code = ble_advertising_init(&advdata, NULL, &options, on_adv_evt, NULL);
  APP_ERROR_CHECK(err_code);
}
/**@brief Function for the GAP initialization.
 *2
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
  uint32_t                err_code=0;
  ble_gap_conn_params_t   gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
  char *p_deviceName;
  ladybug_get_device_name(&p_deviceName);
  SEGGER_RTT_printf(0,"Device name: %s \n",p_deviceName);
  SEGGER_RTT_printf(0,"String length: %d\n",strlen(p_deviceName));
  err_code = sd_ble_gap_device_name_set(&sec_mode,
					(const uint8_t *)p_deviceName,
					strlen(p_deviceName));
  APP_ERROR_CHECK(err_code);

  /* YOUR_JOB: Use an appearance value matching the application's use case.
    err_code = sd_ble_gap_appearance_set(BLE_APPEARANCE_);
    APP_ERROR_CHECK(err_code); */

  memset(&gap_conn_params, 0, sizeof(gap_conn_params));

  gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
  gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
  gap_conn_params.slave_latency     = SLAVE_LATENCY;
  gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

  err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(err_code);
}
/**@brief Function for starting advertising.
 * \callgraph
 */
void advertising_start(void)
{
  SEGGER_RTT_WriteString(0,"--->>>advertising_start\n");
  uint32_t err_code =   ble_advertising_start(BLE_ADV_MODE_FAST);
  APP_ERROR_CHECK(err_code);
}
/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt)
{
  uint32_t                         err_code;
  static ble_gap_evt_auth_status_t m_auth_status;
  static ble_gap_master_id_t p_master_id;
  static ble_gap_sec_keyset_t keys_exchanged;
  SEGGER_RTT_WriteString(0,"\n--> IN on_ble_evt (a BLE event has come into main.c\n");

  switch (p_ble_evt->header.evt_id)
  {
    case BLE_GAP_EVT_CONNECTED:
      SEGGER_RTT_WriteString(0,"\n... BLE_GAP_EVT_CONNECTED\n");
      m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;

      break;

    case BLE_GAP_EVT_DISCONNECTED:
      SEGGER_RTT_WriteString(0,"\n... BLE_GAP_EVT_DISCONNECTED\n");
      m_conn_handle = BLE_CONN_HANDLE_INVALID;
      advertising_start();
      break;

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
      SEGGER_RTT_WriteString(0,"\n... BLE_GAP_EVT_SEC_PARAMS_REQUEST\n");
      err_code = sd_ble_gap_sec_params_reply(m_conn_handle,
					     BLE_GAP_SEC_STATUS_SUCCESS,
					     &m_sec_params,&keys_exchanged);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
      SEGGER_RTT_WriteString(0,"\n... BLE_GATTS_EVT_SYS_ATTR_MISSING\n");
      err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0,BLE_GATTS_SYS_ATTR_FLAG_USR_SRVCS);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GAP_EVT_AUTH_STATUS:
      SEGGER_RTT_WriteString(0,"\n... BLE_GAP_EVT_AUTH_STATUS\n");
      m_auth_status = p_ble_evt->evt.gap_evt.params.auth_status;
      break;

    case BLE_GAP_EVT_SEC_INFO_REQUEST:
      //p_enc_info = keys_exchanged.keys_central.p_enc_key
      SEGGER_RTT_WriteString(0,"\n... BLE_GAP_EVT_SEC_INFO_REQUEST\n");
      if (p_master_id.ediv == p_ble_evt->evt.gap_evt.params.sec_info_request.master_id.ediv)
	{
	  err_code = sd_ble_gap_sec_info_reply(m_conn_handle, &keys_exchanged.keys_central.p_enc_key->enc_info, &keys_exchanged.keys_central.p_id_key->id_info, NULL);
	  APP_ERROR_CHECK(err_code);
	  p_master_id.ediv = p_ble_evt->evt.gap_evt.params.sec_info_request.master_id.ediv;
	}
      else
	{
	  // No keys found for this device
	  err_code = sd_ble_gap_sec_info_reply(m_conn_handle, NULL, NULL,NULL);
	  APP_ERROR_CHECK(err_code);
	}
      break;

    case BLE_GAP_EVT_TIMEOUT:
      SEGGER_RTT_WriteString(0,"\n... BLE_GAP_EVT_TIMEOUT\n");
      if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_ADVERTISING)
	{
	  // Go to system-off mode (this function will not return; wakeup will cause a reset)
	  err_code = sd_power_system_off();
	  APP_ERROR_CHECK(err_code);
	}
      break;

    default:
      SEGGER_RTT_WriteString(0,"\n... Not handling a BLE event here.\n");
      // No implementation needed.
      break;
  }
}

/**@brief Function for dispatching a system event to interested modules.
 * \callgraph
 * \brief ...devzone posts noted if pstorage events are not in main.c, the pstorage event handler
 * should be included in sys_evt_dispatch().  i don't find this obvious.
 * \note the pstorage_sys_event_handler() function is coded up within the nRF51 SDK (see pstorage.c)
 *
 * @param[in]   sys_evt   System stack event.
 */
static void sys_evt_dispatch(uint32_t sys_evt)
{
  pstorage_sys_event_handler(sys_evt);
}
/**@brief Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the scheduler in the main loop after a BLE stack
 *          event has been received.
 *
 * @param[in] p_ble_evt  Bluetooth stack event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
  SEGGER_RTT_WriteString(0,"\n--> IN ble_evt_dispatch\n");
  SEGGER_RTT_printf(0,"connection handle: %d\n",p_ble_evt->evt.gap_evt.conn_handle);
  on_ble_evt(p_ble_evt);
  ble_conn_params_on_ble_evt(p_ble_evt);
  ladybug_BLE_on_ble_evt(&m_lbl, p_ble_evt);

}
/**@brief Timers are used all over the place - say for BLE...
 * \note some timers need to be initialized or the code won't work, returning an error code of "invalid state".
 * \callgraph
 */
static void timers_init(void){
  APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_MAX_TIMERS, APP_TIMER_OP_QUEUE_SIZE, false);
}
/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
  uint32_t err_code;

  // Initialize the SoftDevice handler module.
  SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_SYNTH_250_PPM, NULL);

  // Enable BLE stack
  ble_enable_params_t ble_enable_params;
  memset(&ble_enable_params, 0, sizeof(ble_enable_params));
  ble_enable_params.gatts_enable_params.service_changed = IS_SRVC_CHANGED_CHARACT_PRESENT;
  err_code = sd_ble_enable(&ble_enable_params);
  APP_ERROR_CHECK(err_code);
  // Register with the SoftDevice handler module for BLE events.
  err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
  APP_ERROR_CHECK(err_code);

  // Register with the SoftDevice handler module for BLE events.
  err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
  APP_ERROR_CHECK(err_code);
}
/**
 * \callgraph
 * \brief This is a helper function to easily print out the hex value of each byte within an array of bytes
 * @param dest_bytes	the array of bytes to print out.
 * @param num_bytes	the number of bytes in the array.
 */
void display_bytes(uint8_t *dest_bytes,int num_bytes){
  SEGGER_RTT_WriteString(0,"****BYTES****\n");
  for (int i = 0; i < num_bytes; i++)
    {
      if (i > 0) SEGGER_RTT_WriteString(0,":");
      SEGGER_RTT_printf(0,"%02X",*dest_bytes);
      dest_bytes++;
    }
  SEGGER_RTT_WriteString(0,"\n");
}
/**
 * \brief I think of main as providing initialization and BLE event handling.  One of the event handlers is used by the LBL's service and is defined
 *  in the service_init() function.  service_init() calls into the LBL's BLE service's initialization code which identifies the UUIDs of the LBL service.
 *  \note Call service_init BEFORE advertising_init()

 *  The BLE event dispatcher (ble_evt_dispatch()) calls the LBL's BLE event dispatcher.  Communications with a client is through the client read/writing to characteristics.
 *  \note I assume the power_manage() function does a "good job" interacting with the SoftDevice stack to minimize the amount of power used up by the LBL.  I don't know how to fine tune
 *  power management at this time.  I  use examples from the nRF51 SDK as a crutch, assuming the code for most of main functionality is "cookie cutter" across examples and the Nordic engineers that
 *  write the examples realizes example code would be copy/pasted into our stuff.
 * @return while main is defined as an int, nothing is really returned.
 */
int main(void)
{
  //call flash_init() before initializing service.. the ble_lbl_service uses flash to access pH4 and 7 calibration info.... (wow - too many dependencies!)
  //initialize pstorage() - the way i'll read/write from flash.  POR is to use flash to store the calibration info for pH 4 and pH 7..
  //Note in the S110 Softdevice documentation for pstorage, there is a note:
  //  For implementation of interface included in the example, SoftDevice should be enabled and scheduler (if used) should be initialized prior to initializing this module.
  ladybug_flash_init();
  //a good part of this is "cookie cutter" from the nRF51 SDK...my-o-my there is a lot of code for BLE (within SoftDevice) and once SoftDevice is initialized - unfortunately - there is no longer source code debugging.
  ble_stack_init();
  timers_init();
  gap_params_init();

  //call service_init() before calling advertising_init()...service_init() calls into the LBL's BLE initialization code (where the LBL peripheral service and characteristics are defined and flash reead/write are initialized)
  service_init();
  advertising_init();
  conn_params_init();
  sec_params_init();
  advertising_start();
  // Enter main loop
  for (;;)
    {
      //Lazy write of values stored in flash
      //writing can get messed up if it is done inline with other BLE/sensing activity, and there is no rush.
      storeCalibrationValues_t *p_storeCalibrationValues;
      if (true == ladybug_there_are_calibration_values_to_write(&p_storeCalibrationValues)){
	  SEGGER_RTT_printf(0,"Writing calibration values to flash.  Number of bytes: %d\n",sizeof(storeCalibrationValues_t));
	  ladybug_flash_write(calibrationValues,(uint8_t *)p_storeCalibrationValues,sizeof(storeCalibrationValues_t));
      }
      storePlantInfo_t *p_storePlantInfo;
      if (true == ladybug_there_are_plantInfo_values_to_write(&p_storePlantInfo)){
	  SEGGER_RTT_WriteString(0,"...Writing plantInfo values to flash\n");
	  ladybug_flash_write(plantInfo,(uint8_t *)p_storePlantInfo,sizeof(storePlantInfo_t));
      }
      char *p_deviceName;
      if (true == ladybug_the_device_name_has_been_updated(&p_deviceName)){
	  SEGGER_RTT_WriteString(0,"Writing device name to flash\n");
	  ladybug_flash_write(deviceName,(uint8_t *)p_deviceName,DEVNAME_MAX_LEN);
      }
      power_manage();
    }

}

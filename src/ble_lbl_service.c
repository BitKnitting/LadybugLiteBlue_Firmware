/**
 * \file 	ble_lbl_service.c
 * \author	Margaret Johnson
 * \version	1.0
 * \brief	Establishes the BLE characteristics and handles BLE requests from the client (AKA Central).
 * \details	The "original guts" came from the nrf51-ble-app-lbs-master example on Nordic's GitHub.
 */
#include <ble_lbl_service.h>
#include <string.h>
#include "nordic_common.h"
#include "ble_srv_common.h"
#include "ble_advertising.h"
#include "app_util.h"
#include "Ladybug_ADC.h"
#include "Ladybug_Hydro.h"
#include "Ladybug_error.h"
#include "SEGGER_RTT.h"


//#define CONTROL_UPDATE_HYDRO_READINGS		0
//#define CONTROL_UPDATE_BATTERY_LEVEL		1
//#define CONTROL_UPDATE_DEVICE_NAME		2
//#define CONTROL_CALIBRATE_PH4			3
//#define CONTROL_CALIBRATE_PH7			4

extern ADC_interface adc;

#define	battery_level_AIN	2

extern void display_bytes(uint8_t *dest_bytes,int num_bytes); ///<code is in main.c

/**@brief Function for handling the Connect event.
 *\callgraph
 * @param[in]   p_lbl       LBL Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_connect(ble_lbl_t * p_lbl, ble_evt_t * p_ble_evt)
{
  p_lbl->conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
}
/**@brief Function for handling the Disconnect event.
 *\callgraph
 * @param[in]   p_lbl       LBL Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_disconnect(ble_lbl_t * p_lbl, ble_evt_t * p_ble_evt)
{
  UNUSED_PARAMETER(p_ble_evt);
  p_lbl->conn_handle = BLE_CONN_HANDLE_INVALID;
}
/**
 * \callgraph
 * \brief changes the Ladybug's device name.  What changes on the iOS side is the kCBAdvDataLocalName value within the
 * advertisement data returned for a peripheral within the didDiscoverPeripheral call back.
 * \details takes in a pointer to a char[] that contains the new name for the device.  E.g.: if the new name was "new" the char[] =
 * {'n','e','w',0}.
 * The call to sd_ble_gap_device_name_set() assumes the const char * being passed does not include the null terminator.  So while a
 * sizeof (char test[]) in this case would return 4, the length passed is 3.
 * \note on sizeof.  Example of the difference between sizeof(character array) and sizeof(pointer to a character array):
 * char s[] = "test";   <-s is a character array (i.e.: bytes making up a string)
 * char *t = s;	 <-t is a pointer to a character array (a memory location that contains a pointer to the string)
 * sizeof(s) returns # characters + the terminating null(0) = 5
 * sizeof(t) returns 4 (assuming this CPU has addresses that are 4 bytes in length)
 */
static void update_device_name(char *p_device_name,uint8_t len){
  SEGGER_RTT_WriteString(0,"---> in update_device_name\n");
  ble_gap_conn_sec_mode_t sec_mode;
  //a pointer to a string that contains the new device name is passed in.  Copy the device name into a char array on the stack.
  char copy_of_device_name[len];
  for (int i=0;i<len;i++){
      copy_of_device_name[i] = *(p_device_name+i);
  }
  //then get the string into the memory where constants are stored.
  const char *p_const_device_name = copy_of_device_name;
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
//tell the softdevice to change the device name.  The length passed in is one less than len - Nordic notes the device name is non null-terminated
  uint32_t err_code = sd_ble_gap_device_name_set(&sec_mode,
                                        (const uint8_t *)p_const_device_name,
                                        len-1);
  APP_ERROR_CHECK(err_code);
  //let BLE GAP advertising gunk know of the change in the device name.
  //To do this, the advdata structure needs to be built up
  ble_advdata_t advdata;
  ble_uuid_t adv_uuids[] = {{LBL_UUID_SERVICE,BLE_UUID_TYPE_BLE}};
  memset(&advdata, 0, sizeof(advdata));

  advdata.name_type               = BLE_ADVDATA_FULL_NAME;
  advdata.include_appearance      = true;
  advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
  advdata.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
  advdata.uuids_complete.p_uuids  = adv_uuids;
  //now that the advdata structure is ready, call ble_advdata_set()
  err_code = ble_advdata_set(&advdata, NULL);
//  SEGGER_RTT_WriteString(0,"--->>>advertising_start\n");
//  err_code =   ble_advertising_start(BLE_ADV_MODE_FAST);
//  APP_ERROR_CHECK(err_code);
  //the device name is stored in flash to maintain the name across restarts of the device.
  ladybug_write_device_name(p_device_name,len);
}
static void update_measurement_characteristic(ble_lbl_t * p_lbl) {
  SEGGER_RTT_WriteString(0,"---> IN update_measurement_characteristic\n");
  ble_gatts_hvx_params_t params;
  uint16_t len = sizeof(measurements_t);
  memset(&params, 0, sizeof(params));
  params.type = BLE_GATT_HVX_NOTIFICATION;
  params.handle = p_lbl->measurement_char_handles.value_handle;
  measurements_t *p_measurements;
  ladybug_get_measurements(&p_measurements);
  params.p_data = (uint8_t *)p_measurements;
  params.p_len = &len;
  //The characteristic is updated and then a didUpdate is sent to the client.  NOTE: max 20 bytes can be returned in a NOTIFY
  uint32_t err_code =  sd_ble_gatts_hvx(p_lbl->conn_handle, &params);
  APP_ERROR_CHECK(err_code);
}
static void update_battery_level_characteristic(ble_lbl_t * p_lbl) {
  SEGGER_RTT_WriteString(0,"---> IN update_battery_level_characteristic\n");
  ble_gatts_hvx_params_t params;
  uint16_t len = sizeof(uint16_t);
  memset(&params, 0, sizeof(params));
  params.type = BLE_GATT_HVX_NOTIFICATION;
  params.handle = p_lbl->batt_char_handles.value_handle;
  //read the Battery level AIN
  uint32_t battery_mV = adc.read(battery_level_AIN);
  params.p_data = (uint8_t *)&battery_mV;
  params.p_len = &len;
  //The characteristic is updated and then a didUpdate is sent to the client.  NOTE: max 20 bytes can be returned in a NOTIFY
  uint32_t err_code =  sd_ble_gatts_hvx(p_lbl->conn_handle, &params);
  APP_ERROR_CHECK(err_code);
}
/**
 * \brief This function will update the calibration characteristic with the calibration data currently stored
 * in ladybug's calibrationValues_t structure.  It is called after this structure has been filled with updated
 * values.  The calibration characteristic will notify the client of the update.  An iOS client receives a
 * didUpdateValue...callback.
 * @param p_lbl
 */
static void update_calibration_characteristic(ble_lbl_t * p_lbl){
  SEGGER_RTT_WriteString(0,"--> in update_calibration_characteristic\n");
  ble_gatts_hvx_params_t params;
  uint16_t len = sizeof(calibrationValues_t);
  memset(&params, 0, sizeof(params));
  params.type = BLE_GATT_HVX_NOTIFICATION;
  params.handle = p_lbl->calibration_char_handles.value_handle;
  //get the location where the calibration data is stored
  calibrationValues_t *p_calibrationValues;
  ladybug_get_calibration_values_memory_location(&p_calibrationValues);
  params.p_data = (uint8_t *)p_calibrationValues;
  params.p_len = &len;
  //The characteristic is updated and then a didUpdate is sent to the client.  NOTE: max 20 bytes can be returned in a NOTIFY
  uint32_t err_code =  sd_ble_gatts_hvx(p_lbl->conn_handle, &params);
  APP_ERROR_CHECK(err_code);
}

/**
 * \callgraph
 * \brief function called when the client asks LBL to perform an action - like update the hydro readings or battery level check.
 * @param p_lbl
 * @param p_ble_evt
 */
static void on_write(ble_lbl_t * p_lbl, ble_evt_t * p_ble_evt)
{
  SEGGER_RTT_WriteString(0,"***---> IN ON-WRITE, COMMAND EVENT \n");

  ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
  SEGGER_RTT_printf(0,"...command integer value: %d\n",p_evt_write->data[0]);
  int calValue ;
  if (p_evt_write->handle == p_lbl->control_char_handles.value_handle) {
      switch (p_evt_write->data[0]) {
	case resetPHcalValues:
	case resetECcalValues:
	  ladybug_reset_calibration_values(p_evt_write->data[0]);
	  update_calibration_characteristic(p_lbl);
	  break;
	  //ask the ladybug to read the probe's value.  The value is then written to flash
	case calibratePH4:
	case calibratepH7:
	  ladybug_update_calibration_value(p_evt_write->data[0],0);  //the ECvalue is not needed so sending in a 0
	  update_calibration_characteristic(p_lbl);
	  break;
	case calibrateEC1:
	  SEGGER_RTT_WriteString(0,"...calibrate EC1\n");
	  calValue = p_evt_write->data[2] << 8 | p_evt_write->data[1];
	  ladybug_update_calibration_value(p_evt_write->data[0],calValue);
	  update_calibration_characteristic(p_lbl);
	  SEGGER_RTT_printf(0,"...EC1 calibration solution value: %d\n",	  calValue);
	  break;
	case calibrateEC2:
	  SEGGER_RTT_WriteString(0,"...calibrate EC2\n");
	  //get the calibration solution value typed in by the user.   The units are µS/cm.  The data type is Int16
	  calValue = p_evt_write->data[2] << 8 | p_evt_write->data[1];
	  SEGGER_RTT_printf(0,"...EC2 calibration solution value: %d\n",calValue);
	  ladybug_update_calibration_value(p_evt_write->data[0],calValue);
	  update_calibration_characteristic(p_lbl);
	  break;
	case undoPH4:
	case undoPH7:
	  SEGGER_RTT_WriteString(0,"...undo either pH4, pH7\n");
	  int16_t calValue = p_evt_write->data[1] | p_evt_write->data[2] << 8;
	  SEGGER_RTT_printf(0,"pH calibration value: %d\n",calValue);
	  ladybug_undo_pH_calibration(p_evt_write->data[0],calValue);
	  update_calibration_characteristic(p_lbl);
	  break;
	case undoEC1:
	case undoEC2:
	  SEGGER_RTT_WriteString(0,"...undo either EC1 (Vin and Vout), or EC2 (Vin and Vout)\n");
	  int16_t EC_Vin = p_evt_write->data[1] | p_evt_write->data[2] << 8;
	  int16_t EC_Vout = p_evt_write->data[3] | p_evt_write->data[4] << 8;
	  ladybug_undo_EC_calibration(p_evt_write->data[0], EC_Vin, EC_Vout);

	  update_calibration_characteristic(p_lbl);
	  break;
	case updatePHandEC:
	  SEGGER_RTT_WriteString(0,"update pH and EC\n");
	  update_measurement_characteristic(p_lbl);
	  break;
	case updateBatteryLevel:
	  SEGGER_RTT_WriteString(0,"update battery level\n");
	  update_battery_level_characteristic(p_lbl);
	  break;
	case updateDeviceName:
	  SEGGER_RTT_WriteString(0,"update device name\n");
	  display_bytes(&p_evt_write->data[1],p_evt_write->len);
	  update_device_name(&p_evt_write->data[1],p_evt_write->len);
	  break;
	default:
	  SEGGER_RTT_WriteString(0,"Unknown control\n");
	  break;
      }
  }
}
/**
 * \brief the BLE event dispatcher in main.c calls this function so the LBL service can take action on BLE events - like those on characteristics (e.g.: write to a characteristic)
 * @param p_lbl
 * @param p_ble_evt
 */
void ble_lbl_on_ble_evt(ble_lbl_t * p_lbl, ble_evt_t * p_ble_evt)
{
  SEGGER_RTT_WriteString(0,"\n--> IN ble_lbl_on_ble_evt the BLE Event dispatcher for Ladybug\n");
  switch (p_ble_evt->header.evt_id)
  {
    case BLE_GAP_EVT_CONNECTED:
      SEGGER_RTT_WriteString(0,"\n...BLE_GAP_EVT_CONNECTED\n");
      on_connect(p_lbl, p_ble_evt);
      break;

    case BLE_GAP_EVT_DISCONNECTED:
      SEGGER_RTT_WriteString(0,"\n...BLE_GAP_EVT_DISCONNECTED\n");
      on_disconnect(p_lbl, p_ble_evt);
      break;
    case BLE_GAP_EVT_CONN_PARAM_UPDATE:
      SEGGER_RTT_WriteString(0,"\n...BLE_GAP_EVT_CONN_PARAM_UPDATE\n");
      break;
    case BLE_GATTS_EVT_WRITE:
      SEGGER_RTT_WriteString(0,"\n...BLE_GATTS_EVT_WRITE\n");
      on_write(p_lbl, p_ble_evt);
      break;

    default:
      SEGGER_RTT_WriteString(0,"\n...not handling a BLE event here\n");
      break;
  }
}

/**@brief Function for control - ie: asking the LBL to go off and do a task.  I identified the tasks that can be done in the description of the brief of the ble_lbl_init() function.
 *\callgraph
 */
static uint32_t control_char_add(ble_lbl_t * p_lbl)
{
  SEGGER_RTT_WriteString(0,"---> in control_char_add\n");
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_t    attr_char_value;
  ble_uuid_t          ble_uuid;
  ble_gatts_attr_md_t attr_md;
  memset(&char_md, 0, sizeof(char_md));
  /************************************
   * the control characteristic is write only
   *************************************/
  char_md.char_props.write  = 1;
  char_md.p_char_user_desc  = NULL;
  char_md.p_char_pf         = NULL;
  char_md.p_user_desc_md    = NULL;
  char_md.p_cccd_md         = NULL;
  char_md.p_sccd_md         = NULL;

  ble_uuid.type = p_lbl->uuid_type;  /**< reference to the LBL's UUID */
  ble_uuid.uuid = LBL_UUID_CONTROL_CHAR;  /**< control characteristic's 16 bit UUID */
  // the attr_md structure tells the SoftDevice that this characteristic
  memset(&attr_md, 0, sizeof(attr_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
  BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.read_perm);
  // I'm leaving this as they are in the example code.  I don't have a grasp on what they inform/do
  attr_md.vloc       = BLE_GATTS_VLOC_STACK;
  attr_md.rd_auth    = 0;
  attr_md.wr_auth    = 0;
  attr_md.vlen       = 0;

  memset(&attr_char_value, 0, sizeof(attr_char_value));

  attr_char_value.p_uuid       = &ble_uuid;  /**< ble_uuid was set earlier to be the control characteristic */
  attr_char_value.p_attr_md    = &attr_md;
  attr_char_value.init_len     = sizeof(uint8_t)*BLE_GAP_DEVNAME_MAX_LEN + 1;  //Used for both EC solution values and device name.  Set to max the device name can be + 1 for the command byte
  attr_char_value.init_offs    = 0;
  attr_char_value.max_len      = sizeof(uint8_t)*BLE_GAP_DEVNAME_MAX_LEN + 1;
  attr_char_value.p_value      = NULL;

  return sd_ble_gatts_characteristic_add(p_lbl->service_handle, &char_md,
					 &attr_char_value,
					 &p_lbl->control_char_handles);
}
/**@brief let the client know what the power level is at.  the current plan is to use a coin cell battery.  At a low power level, the battery
 * will need to be replaced.  The battery level characteristic is implemented as a NOTIFY characteristic.  This way, the client does not need
 * to send a read characteristic request as is done for plantInfo and hydro values.  The battery level values are < 20 bytes (the maximum size of bytes a NOTIFY characteristic
 * will return).
 *\callgraph
 */
static uint32_t batt_char_add(ble_lbl_t * p_lbl)
{
  SEGGER_RTT_WriteString(0,"---> in batt_char_add\n");
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_md_t cccd_md;
  ble_gatts_attr_t    attr_char_value;
  ble_uuid_t          ble_uuid;
  ble_gatts_attr_md_t attr_md;
//setting up the cccd_md is needed for a notify characteristic but not for just a read characteristic
  memset(&cccd_md, 0, sizeof(cccd_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
  cccd_md.vloc = BLE_GATTS_VLOC_STACK;

  memset(&char_md, 0, sizeof(char_md));

  char_md.char_props.read   = 1;
  char_md.char_props.notify = 1;
  char_md.p_char_user_desc  = NULL;
  char_md.p_char_pf         = NULL;
  char_md.p_user_desc_md    = NULL;
  char_md.p_cccd_md         = &cccd_md;
  char_md.p_sccd_md         = NULL;

  ble_uuid.type = p_lbl->uuid_type;
  ble_uuid.uuid = LBL_UUID_BATT_CHAR;  /**< battery service's 16 bit UUID */

  memset(&attr_md, 0, sizeof(attr_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
  attr_md.vloc       = BLE_GATTS_VLOC_STACK;
  attr_md.rd_auth    = 0;
  attr_md.wr_auth    = 0;
  attr_md.vlen       = 0;

  memset(&attr_char_value, 0, sizeof(attr_char_value));
  /************************************
   * get battery level from battery AIN
   *************************************/
  //read the Battery level AIN
  uint32_t battery_mV = adc.read(battery_level_AIN);
  attr_char_value.p_uuid       = &ble_uuid;  //a bit earlier in this function this was set to the batt characteristic
  attr_char_value.p_attr_md    = &attr_md;
  attr_char_value.init_len     = sizeof(uint16_t);
  attr_char_value.init_offs    = 0;
  attr_char_value.max_len      = sizeof(uint16_t);
  attr_char_value.p_value      = (uint8_t *)&battery_mV;

  return sd_ble_gatts_characteristic_add(p_lbl->service_handle, &char_md,
					 &attr_char_value,
					 &p_lbl->batt_char_handles);
}
/**
 * \brief The characteristic that contains the pH(mV) , EC_VIN(mV), and EC_VOUT(mV) readings.
 * \details This characteristic will Notify the client when the values have been updated.  Measurements are updated
 * when the client sends the command to update the measurement.  Do it this way saves constantly updating readings by the device.
 * @param p_lbl
 * @return
 */
static uint32_t measurement_char_add(ble_lbl_t * p_lbl)
{
  SEGGER_RTT_WriteString(0,"---> in measurement_char_add\n");
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_md_t cccd_md;
  ble_gatts_attr_t    attr_char_value;
  ble_uuid_t          ble_uuid;
  ble_gatts_attr_md_t attr_md;
//setting up the cccd_md is needed for a notify characteristic but not for just a read characteristic
  memset(&cccd_md, 0, sizeof(cccd_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
  cccd_md.vloc = BLE_GATTS_VLOC_STACK;

  memset(&char_md, 0, sizeof(char_md));

  char_md.char_props.read   = 1;
  char_md.char_props.notify = 1;
  char_md.p_char_user_desc  = NULL;
  char_md.p_char_pf         = NULL;
  char_md.p_user_desc_md    = NULL;
  char_md.p_cccd_md         = &cccd_md;
  char_md.p_sccd_md         = NULL;

  ble_uuid.type = p_lbl->uuid_type;
  ble_uuid.uuid = LBL_UUID_MEASUREMENT_CHAR;

  memset(&attr_md, 0, sizeof(attr_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
  attr_md.vloc       = BLE_GATTS_VLOC_STACK;
  attr_md.rd_auth    = 0;
  attr_md.wr_auth    = 0;
  attr_md.vlen       = 0;

  memset(&attr_char_value, 0, sizeof(attr_char_value));
  /************************************
   * get pH(mV), EC_VIN(mV), and EC_VOUT(mV) readings
   *************************************/
  measurements_t *p_measurements;
  ladybug_get_measurements(&p_measurements);
  attr_char_value.p_uuid       = &ble_uuid;  //a bit earlier in this function this was set to the batt characteristic
  attr_char_value.p_attr_md    = &attr_md;
  attr_char_value.init_len     = sizeof(measurements_t);
  attr_char_value.init_offs    = 0;
  attr_char_value.max_len      = sizeof(measurements_t);
  attr_char_value.p_value      = (uint8_t *)p_measurements;

  return sd_ble_gatts_characteristic_add(p_lbl->service_handle, &char_md,
					 &attr_char_value,
					 &p_lbl->measurement_char_handles);
}
/**@brief  This is a read/notify characteristic.  Contains the pH,EC, and pH calibration info.  The code logic is for the client to first send a command to update a calibration
 * value - for example, if the probe is in the pH4 calibration solution, the update is to update pH4.  The calibration characteristic
 * also contains the mV values for pH4, EC1, and EC2 (i.e.: pH calibration requires two points.  EC calibration can be either one point
 * or two points.  There is an in-depth explanation of this in the client code).
 *
 * hydro values and then follow this with a read on the hydro characteristic. This way, the only time the hydro values are updated is when the
 * client requests a new reading.  There is no need to initialize the characteristic with values since there should always be an update
 * request prior to a read of the hydro characteristic.
 * \param[in]   p_lbl	Pointer to structure that holds the handles and state info for the LBL service.  This is needed to associate the characteristic with the service.
 *
 *\callgraph
 */
static uint32_t calibration_char_add(ble_lbl_t * p_lbl)
{
  SEGGER_RTT_WriteString(0,"---> in calibration_char_add\n");
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_md_t cccd_md;
  ble_gatts_attr_t    attr_char_value;
  ble_uuid_t          ble_uuid;
  ble_gatts_attr_md_t attr_md;
//setting up the cccd_md is needed for a notify characteristic but not for just a read characteristic
  memset(&cccd_md, 0, sizeof(cccd_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
  cccd_md.vloc = BLE_GATTS_VLOC_STACK;

  memset(&char_md, 0, sizeof(char_md));

  char_md.char_props.read   = 1;
  char_md.char_props.notify = 1;
  char_md.p_char_user_desc  = NULL;
  char_md.p_char_pf         = NULL;
  char_md.p_user_desc_md    = NULL;
  char_md.p_cccd_md         = &cccd_md;
  char_md.p_sccd_md         = NULL;

  ble_uuid.type = p_lbl->uuid_type;
  ble_uuid.uuid = LBL_UUID_CALIBRATION_CHAR;

  memset(&attr_md, 0, sizeof(attr_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
  attr_md.vloc       = BLE_GATTS_VLOC_STACK;
  attr_md.rd_auth    = 0;
  attr_md.wr_auth    = 0;
  attr_md.vlen       = 0;

  memset(&attr_char_value, 0, sizeof(attr_char_value));
  /************************************
   * get calibration readings
   *************************************/
  calibrationValues_t *p_calibrationValues;
  ladybug_get_calibrationValues(&p_calibrationValues);
  attr_char_value.p_uuid       = &ble_uuid;  //a bit earlier in this function this was set to the batt characteristic
  attr_char_value.p_attr_md    = &attr_md;
  attr_char_value.init_len     = sizeof(calibrationValues_t);
  attr_char_value.init_offs    = 0;
  attr_char_value.max_len      = sizeof(calibrationValues_t);
  attr_char_value.p_value      = (uint8_t *)p_calibrationValues;

  return sd_ble_gatts_characteristic_add(p_lbl->service_handle, &char_md,
					 &attr_char_value,
					 &p_lbl->calibration_char_handles);
}
/**
 * \callback
 * \brief The plant Info is the plant type - like tomato, cucumber, lettuce... and the growth stage - either seedling, youth or mature.
 * These are strings that are set by the grower.  The client has a database with rows which have plant type, growth stage, pH value, EC value.
 * The values can be used to adjust the EC and pH values.  If the grower hasn't set these yet, the values will be set to something like ???????
 *
 * @param p_lbl
 * @return
 */
static uint32_t plantInfo_char_add(ble_lbl_t * p_lbl) {
  SEGGER_RTT_WriteString(0,"\n---> in plantInfo_char_add\n");
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_t    attr_char_value;
  ble_uuid_t          ble_uuid;
  ble_gatts_attr_md_t attr_md;

  memset(&char_md, 0, sizeof(char_md));
  /************************************
   * the plant info characteristic is read/write
   *************************************/
  char_md.char_props.read   = 1;
  char_md.char_props.write  = 1;
  char_md.p_char_user_desc  = NULL;
  char_md.p_char_pf         = NULL;
  char_md.p_user_desc_md    = NULL;
  char_md.p_cccd_md         = NULL;
  char_md.p_sccd_md         = NULL;

  ble_uuid.type = p_lbl->uuid_type;  /**< reference to the LBL's UUID */
  ble_uuid.uuid = LBL_UUID_PLANTINFO_CHAR;  /**< plant info characteristic's 16 bit UUID */
  // the attr_md structure tells the SoftDevice that this characteristic
  memset(&attr_md, 0, sizeof(attr_md));
  // everyone can read this attribute
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
  // everyone can write
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
  // I'm leaving this as they are in the example code.  I don't have a grasp on what they inform/do
  attr_md.vloc       = BLE_GATTS_VLOC_STACK;
  attr_md.rd_auth    = 0;
  attr_md.wr_auth    = 0;
  attr_md.vlen       = 0;
  memset(&attr_char_value, 0, sizeof(attr_char_value));
  /************************************
    * get the plantInfo values
    *************************************/
  plantInfo_t *p_plantInfo;
  ladybug_get_plantInfo(&p_plantInfo);
  attr_char_value.p_uuid       = &ble_uuid;  /**< ble_uuid was set earlier to be the plantInfo characteristic */
  attr_char_value.p_attr_md    = &attr_md;
  attr_char_value.init_len     = sizeof(plantInfo_t);
  attr_char_value.init_offs    = 0;
  attr_char_value.max_len      = sizeof(plantInfo_t);
  SEGGER_RTT_printf(0,"Size of plant info characteristic: %d\n",attr_char_value.max_len);
  attr_char_value.p_value      = (uint8_t *)p_plantInfo;
  return sd_ble_gatts_characteristic_add(p_lbl->service_handle, &char_md,
					 &attr_char_value,
					 &p_lbl->plantInfo_char_handles);
}

/**
 * \callgraph
 * \note The LBL's BLE initialization function is called within main.c to initialize the LBL's BLE service and characteristics...as well as initialize flash read/write variables used to store the pH calibration info (info goes into one of the characteristics)
 * \brief Get the S110 SoftDevice to know about the LBL Service and then add in the characteristics.
 *

 *
 * @param p_lbl		The Ladybug Lite service structure defined to hold handles and other state information to the LBL service and it's characteristics.
 * @return		either success or an error code
 */
uint32_t ble_lbl_init(ble_lbl_t * p_lbl)
{
  SEGGER_RTT_WriteString(0,"---> in ble_lbl_init");
  uint32_t   err_code=NRF_SUCCESS;
  ble_uuid_t ble_uuid;

  p_lbl->conn_handle       = BLE_CONN_HANDLE_INVALID;
  /************************************
   * Add the LBL BLE Service to the SoftDevice stack
   *************************************/
  // Adds the LBL's UUID to the SoftDevice stack and then returns a "24-bit ble_uuit_t formatted "blob" subsequently used
  // to reference the LBL Service when registering LBL service(s) and characteristics.
  ble_uuid128_t base_uuid = LBL_UUID_BASE;
  err_code = sd_ble_uuid_vs_add(&base_uuid, &p_lbl->uuid_type);
  APP_ERROR_CHECK(err_code);
  //Now that we have ble_uuid_t's uuid_type referenced to the LBL's UUID Server, register the LBL Service.
  ble_uuid.type = p_lbl->uuid_type;
  ble_uuid.uuid = LBL_UUID_SERVICE;
  err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &ble_uuid, &p_lbl->service_handle);
  APP_ERROR_CHECK(err_code);
  /************************************
   * Add the plantInfo characteristic to the LBL Service
   *************************************/
  err_code = plantInfo_char_add(p_lbl);
  APP_ERROR_CHECK(err_code);
  /************************************
   * Add the (pH and EC) measurement characteristic to the LBL Service
   *************************************/
  err_code = measurement_char_add(p_lbl);
  APP_ERROR_CHECK(err_code);
  /************************************
   * Add the calibration characteristic to the LBL Service
   *************************************/
  err_code = calibration_char_add(p_lbl);
  APP_ERROR_CHECK(err_code);
  /************************************
   * Add the battery level characteristic to the LBL Service
   *************************************/
  err_code = batt_char_add(p_lbl);
  APP_ERROR_CHECK(err_code);
  /************************************
   * Add the control characteristic to the LBL Service
   *************************************/
  err_code = control_char_add(p_lbl);
  APP_ERROR_CHECK(err_code);


  return err_code;  //if got this far, err_code = NRF_SUCCESS since all others are followed by a APP_ERROR_CHECK() which kinda ends the whole show...
}


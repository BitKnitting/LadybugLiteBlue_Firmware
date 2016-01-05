/**
 * \file 	Ladybug_Hydro.c
 * \author	Margaret Johnson
 * \version	1.0
 * \brief	Handles pH and EC mV readings from the AIN and to/from Flash as needed by the client.
 * \todo		Take multiple measurements (e.g.: 2 mins) when reading calibration measurements, until readings converge.
 */
#include "string.h"
#include "nrf_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include "app_error.h"
#include "softdevice_handler.h"
#include "ble_advertising.h"
#include "Ladybug_Flash.h"
#include "Ladybug_ADC.h"
#include "Ladybug_Hydro.h"
#include "SEGGER_RTT.h"


extern ADC_interface adc;
static uint8_t			 m_write_calibration_values = false; ///<flag to let main know to write the hydro structure to flash because calibration values have been updated.
static uint8_t			 m_write_plantInfo_values = false;
static uint8_t			 m_write_device_name = false;
static storePlantInfo_t		 m_storePlantInfo;
static storeCalibrationValues_t	 m_storeCalibrationValues;
static measurements_t		 m_measurements;
static char 			 m_device_name[DEVNAME_MAX_LEN]; ///<The length of the device name cannot be greater than BLE_GAP_DEVNAME_MAX_LEN.  See [this blog post](https://devzone.nordicsemi.com/question/24669/feedback-ble_gap_devname_max_len-is-too-short/)

/**
 * \brief mapping the FET pins to the schematic
 */
#define EC_VIN_FET	0
#define EC_VOUT_FET	7

static void print_out_calibration_values() {
  SEGGER_RTT_WriteString(0,"***** CALIBRATION VALUES *****\n");
  SEGGER_RTT_printf(0,"size of m_storeCalibrationValues: %d\n",sizeof(m_storeCalibrationValues));
  SEGGER_RTT_printf(0,"write_check: 0X%x\n",m_storeCalibrationValues.write_check);

  SEGGER_RTT_printf(0,"pH4_mV: %d, pH7_mV: %d\n",m_storeCalibrationValues.calValues.pH4_mV,m_storeCalibrationValues.calValues.pH7_mV);
  SEGGER_RTT_printf(0,"EC1_mV[0]: %d, EC1_mV[1]: %d, EC2_mV[0]: %d, EC2[1]\n",
		    m_storeCalibrationValues.calValues.EC1_mV[0],m_storeCalibrationValues.calValues.EC1_mV[1],
		    m_storeCalibrationValues.calValues.EC2_mV[0],m_storeCalibrationValues.calValues.EC2_mV[1]);
  SEGGER_RTT_printf(0,"EC1solution: %d, EC2solution: %d\n", m_storeCalibrationValues.calValues.EC1solution,m_storeCalibrationValues.calValues.EC2solution);
}
/**
 * \brief The rectifier circuit for both EC VIN and VOUT have a FET attached to drain the cap as the cap will inevitably discharge causing voltage readings to be higher than they
 * actually are.  This is a standard "thing" for a rectifier circuit.  As I understand it, many rectifiers use a resistor but I felt using a FET would get a better ADC reading.
 * @param which_AIN
 */
static void discharge (uint8_t which_AIN)
{
  uint32_t pin_number;
  //set the GPIO based on whether the current ADC reading is for the VIN or VOUT
  if (EC_VIN == which_AIN) {
      pin_number = EC_VIN_FET;
  }
  else {
      pin_number = EC_VOUT_FET;
  }
  //configure the GPIO for output
  nrf_gpio_cfg_output(pin_number);
  //discharge the cap by setting the GPIO to HIG
  nrf_gpio_pin_set(pin_number);
  //open the FET's gate so the ADC picks up an accurate measurement
  nrf_gpio_pin_clear(pin_number);
}
static int16_t get_pH_reading() {
  int16_t VGND = adc.read(pH_VGND);
  int16_t AIN = adc.read(pH_AIN);
  int16_t pH = AIN - VGND;
  SEGGER_RTT_printf(0,"PH_VGND: %d , PH AIN: %d, pH_mV = AIN-VGND = %d\n",VGND,AIN,pH);
  return (pH);
}
/**
 * \brief It is assumed the EC probe is in some type of water so the EC can be measured.  This might be a calibration solution or a nutrient bath.  This function
 * gets readings from the EC's VGND, VIN, VOUT AINs and returns VIN and VOUT without VGND.  These are requested by the client through a BLE Hydro characteristic
 * read request.  Going from VIN/VOUT measurements to an EC value is handled on the client.
 * The first element of the returned array is VIN. The second is VOUT.
 * @param p_EC		A pointer to two int16_t values.  The first will store the VIN reading.  The second will store the VOUNT reading
 */
static void get_EC_reading(int16_t *p_EC) {
  SEGGER_RTT_WriteString(0,"---> IN get_EC_reading\n");
  int16_t VGND = adc.read(EC_VGND);
  SEGGER_RTT_printf(0,"EC_VGND: %d  0X%x\n",VGND,VGND);
  //EC VIN and EC VOUT have a rectifier step in which there is a FET that stabilizes the rectification by discharging the cap to prevent an upward drift..
  //I wrote some blog posts on this...there are FET pins assigned for both so, I added a discharge() function...
  discharge(EC_VIN);
  int16_t AIN = adc.read(EC_VIN);
  //The first element in the array is EC VIN
  *p_EC = AIN-VGND;
  SEGGER_RTT_printf(0,"EC_VIN after subtracting VGND: %d 0X%x\n",*p_EC,*p_EC);
  discharge(EC_VOUT);
  AIN = adc.read(EC_VOUT);
  //the second element is EC VOUT
  *(p_EC+1) = AIN-VGND;
  SEGGER_RTT_printf(0,"EC_VOUT after subtracting VGND: %d 0X%x\n", *(p_EC+1),*(p_EC+1));
}

/**
 * \callgraph
 * \brief Read the plant type and stage stored in flash.  If a write check has not been written, then set the plant type and stage to
 * default strings.
 */
void ladybug_get_plantInfo(plantInfo_t **p_plantInfo)
{
  SEGGER_RTT_WriteString(0,"\n***--->>> in ladybug_get_plantInfo_values\n");
  *p_plantInfo = &m_storePlantInfo.plantChar.plantInfo;
  ladybug_flash_read(plantInfo,m_storePlantInfo.plantChar.bytes);
  if (m_storePlantInfo.write_check != WRITE_CHECK){
      //set plant type and stage to ??
      SEGGER_RTT_WriteString(0,"...setting plantStore bytes\n");
      //the flash storage for plant info is 32 bytes - which is the BLOCK_SIZE
      memset(&m_storePlantInfo, '?', BLOCK_SIZE);
      m_storePlantInfo.write_check = WRITE_CHECK;
      m_write_plantInfo_values = true;
  }
}

/**
 * \brief the central has requested calibrating either the pH or EC probe.  First decide what calibration solution the probe is in.  This
 * could be a pH4, pH7, EC1, or EC2 calibration solution.  Calculating the pH and EC happens on the client.  In this function the mV readings
 * for the AINs associated with what is being calibrated is read and stored into flash.
 *  EC readings will also store the calibration solution value used that would be the reading if the EC probe was "ideal"
 */
void ladybug_update_calibration_value(control_enum_t command, int solutionValue){
  SEGGER_RTT_printf(0,"---> in ladybug_update_calibration_value.  command: %d, solution value: %d\n",command,solutionValue);
  //pH calibration comes from a simple reading of the pH AIN
  //there is no additional calibration values that need to be stored since calibration is fixed on using the two points: pH4 and pH7.
  if (command == calibratePH4 || command == calibratepH7) {
      int16_t pH_value = get_pH_reading();
      if (command == calibratePH4){
	  m_storeCalibrationValues.calValues.pH4_mV = pH_value;
	  SEGGER_RTT_WriteString(0,"Calibrated pH4\n");
      }else {
	  m_storeCalibrationValues.calValues.pH7_mV = pH_value;
	  SEGGER_RTT_WriteString(0,"Calibrated pH7\n");
      }
  }else {
      //EC calibration can use one or two points. The calibration's solution values are stored in EC1solution and EC2solution.  The readings from the probe
      //taken when the probe is submerged in either the EC1solution or EC2solution is EC1[] or EC2[].
      int16_t EC_VIN_and_VOUT_mV[2];
      // Read the AIN values assigned for the EC Vin and EC Vout values.  Which is read depends on which calibration solution the
      // probe is in.  The user of the client has chosen either EC1 or EC2.  What comes over is the EC 1 or 2 calibration solution
      // value.  This will be stored as well as the probe values that are read from which the EC can be calculated on the client.
      get_EC_reading(EC_VIN_and_VOUT_mV);  //the first element is VIN, the second is VOUT.  EC calculation happens on the client
      if (command == calibrateEC1){
	  SEGGER_RTT_WriteString(0,"...setting EC1 values...\n");
	  m_storeCalibrationValues.calValues.EC1solution = solutionValue;
	  for (int i=0;i<2;i++) {
	      m_storeCalibrationValues.calValues.EC1_mV[i] = EC_VIN_and_VOUT_mV[i];
	  }
      }else {  //calibrate EC2
	  SEGGER_RTT_WriteString(0,"...setting EC2 values...\n");
	  m_storeCalibrationValues.calValues.EC2solution = solutionValue;
	  for (int i=0;i<2;i++) {
	      m_storeCalibrationValues.calValues.EC2_mV[i] = EC_VIN_and_VOUT_mV[i];
	  }
      }
      print_out_calibration_values();
  }
  //write the reading (and the rest that in the hydro data) to flash.
  m_write_calibration_values = true;
}
/**
 * \callgraph
 * \brief OOps!  The central wants the last value stored for a pH calibration measurement
 */
void ladybug_undo_pH_calibration(control_enum_t command, int16_t pHCalValue) {
  SEGGER_RTT_printf(0,"---> in ladybug_undo_pH_calibration.  pHCalValue: %d\n",pHCalValue);
  if (command == undoPH4) {
      m_storeCalibrationValues.calValues.pH4_mV = pHCalValue;
  } else {
      m_storeCalibrationValues.calValues.pH7_mV = pHCalValue;
  }
  print_out_calibration_values();
  m_write_calibration_values = true;
}
  /**
   * \callgraph
   * \brief OOps!  The central wants the last value stored for either EC1 or EC2 calibration measurement
   */
  void ladybug_undo_EC_calibration(control_enum_t command, int16_t EC_Vin, int16_t EC_Vout) {
    if (command == undoEC1) {
	m_storeCalibrationValues.calValues.EC1_mV[0] = EC_Vin;
	m_storeCalibrationValues.calValues.EC1_mV[1] = EC_Vout;
    }else {
	m_storeCalibrationValues.calValues.EC2_mV[0] = EC_Vin;
	m_storeCalibrationValues.calValues.EC2_mV[1] = EC_Vout;
    }
    print_out_calibration_values();
    m_write_calibration_values = true;
  }
  /**
   * \callgraph
   * \brief A simple function to reset the pH4 and pH7 mV values to "ideal".  pH4 = 178mV and pH7 = 0mV.
   */
  static void reset_pH_calibration_values(){
    SEGGER_RTT_WriteString(0,"...RESETTIING pH Calibration values\n");
    m_storeCalibrationValues.calValues.pH4_mV = 178;
    m_storeCalibrationValues.calValues.pH7_mV = 0;
  }
  /**
   * \callgraph
   * \brief A simple function to reset the EC calibration values.
   */
  static void reset_EC_calibration_values(){
    SEGGER_RTT_WriteString(0,"...RESETTIING EC Calibration values\n");
    for (int i=0;i<2;i++){  //reset the values read from the probe, but not the solution values entered by the user.
	m_storeCalibrationValues.calValues.EC1_mV[i] = 0;
	m_storeCalibrationValues.calValues.EC2_mV[i] = 0;
    }
    m_storeCalibrationValues.calValues.EC1solution = 0;
    m_storeCalibrationValues.calValues.EC2solution = 0;
  }
  /**
   * \callgraph
   * \brief resets all calibration values to "ideal"...this happens when m_storeCalibrationValues.write_check != WRITE_CHECK
   * i.e.: the flash reserved for storing the calibration values has not yet been written to.
   */
  static void reset_all_calibration_values() {
    reset_pH_calibration_values();
    reset_EC_calibration_values();
    m_write_calibration_values = true;
  }
  /**
   * \callgraph
   * \brief a request has come in from the client to reset either the pH or EC calibration values
   */
  void ladybug_reset_calibration_values(control_enum_t command)
  {
    if (command == resetPHcalValues){
	reset_pH_calibration_values();
    }else {
	reset_EC_calibration_values();
    }
    //lazy write the calibration values to flash so stuff doesn't get screwed up/freeze...hmmm.....
    m_write_calibration_values = true;
  }
  /**
   * read the calibration measurements for mV for pH4, pH7, EC1[2], and EC2[2], EC1solution, EC2solution from flash
   * @param p_calibrationValues
   */
  void ladybug_get_calibrationValues(calibrationValues_t **p_calibrationValues) {
    SEGGER_RTT_WriteString(0,"--> IN ladybug_get_calibrationValues\n");
    *p_calibrationValues = &m_storeCalibrationValues.calValues;
    ladybug_flash_read(calibrationValues,(uint8_t *)&m_storeCalibrationValues.calValues);
    if (m_storeCalibrationValues.write_check != WRITE_CHECK){
	//calibration values have not been stored
	m_storeCalibrationValues.write_check = WRITE_CHECK;
	reset_all_calibration_values();
    }
  }
  /**
   * \callgraph
   * \brief get pH and EC readings and return a pointer to the variable holding the measurements.
   * @param p_measurements		used to return a pointer to the variable holding the measurements.
   */
  void ladybug_get_measurements(measurements_t **p_measurements) {
    SEGGER_RTT_WriteString(0,"\n***--->>> in ladybug_get_measurements\n");
    *p_measurements = &m_measurements;
    m_measurements.pH_mV = get_pH_reading();
    get_EC_reading(m_measurements.EC_mV);
  }
  /**
   * \callgraph
   * \brief Function provides the memory location where the calibration values are stored.  This way, using a
   * global calibrationValues_t struct is avoided.
   * @param p_calibrationValues
   */
  void ladybug_get_calibration_values_memory_location(calibrationValues_t **p_calibrationValues) {
    *p_calibrationValues = &m_storeCalibrationValues.calValues;
  }
  /***
   * \callgraph
   * \brief Masks the global variable flag that requests a flash write of the calibration values
   */
  bool ladybug_there_are_calibration_values_to_write(storeCalibrationValues_t **p_storeCalibrationValues){
    if (true == m_write_calibration_values){
	m_write_calibration_values = false;
	*p_storeCalibrationValues = &m_storeCalibrationValues;
	return true;
    }
    return false;
  }
  /***
   * \callgraph
   * \brief Masks the global variable flag that requests a flash write of the plant info values
   */
  bool ladybug_there_are_plantInfo_values_to_write(storePlantInfo_t **p_storePlantInfo){
    if (true == m_write_plantInfo_values){
	m_write_plantInfo_values = false;
	*p_storePlantInfo = &m_storePlantInfo;
	return true;
    }
    return false;
  }
  /**
   * \callgraph
   * \brief Hides a flag that says the device name has been changed by the client and it needs to be stored in Flash.
   * @return	true if the device name has been  updated over BLE.  Otherwise, false
   */
  bool ladybug_the_device_name_has_been_updated(char ** p_deviceName){
    if (true == m_write_device_name){
	m_write_device_name = false;
	*p_deviceName = m_device_name;
	return true;
    }
    return false;
  }
  void ladybug_get_device_name(char **p_deviceName) {
    SEGGER_RTT_WriteString(0,"---> IN ladybug_get_device_name");
    char device_name_in_storage_block[BLOCK_SIZE];
    memset(&device_name_in_storage_block, 0, BLOCK_SIZE);
    ladybug_flash_read(deviceName,(uint8_t *)&device_name_in_storage_block);
    uint8_t first_char_of_device_name = device_name_in_storage_block[0];
    if (0xFF == first_char_of_device_name){
	memcpy(&device_name_in_storage_block,DEFAULT_DEVICE_NAME,sizeof(DEFAULT_DEVICE_NAME));
	m_write_device_name = true;
    }
    *p_deviceName = m_device_name;
    SEGGER_RTT_printf(0,"maximum length for the device name: %d\n",DEVNAME_MAX_LEN);
    memcpy(m_device_name,device_name_in_storage_block,DEVNAME_MAX_LEN);
  }
  /**
   * \brief write the device name the client has sent to flash.  This could happen within the ble service code.  For now I'm keeping
   * flash writes for the Ladybug all within the ladybug code.
   * @param p_device_name	a pointer to the device name string of characters
   * @param len		the length of the device name string. The length cannot be greater than the max bytes allowed for the name.
   */
  void ladybug_write_device_name(char *p_device_name,uint16_t len){
    len > DEVNAME_MAX_LEN ? DEVNAME_MAX_LEN : len;
    for (int i=0;i<len;i++){
	m_device_name[i] = *(p_device_name+i);
    }
    m_write_device_name =true;
  }

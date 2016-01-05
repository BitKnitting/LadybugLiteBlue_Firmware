/**
 * \file Ladybug_Hydro.h
 * \Author Margaret Johnson
 * \Version 0.1
 * \Copyright October, 2015
 * \brief identifies the typedefs macros used for reading and packing hydro values (i.e.: the mV from the AINs as well as the mV representing calibration mV that are stored for pH 4 and 7)
 */
#ifndef LADYBUG_HYDRO_H
#define LADYBUG_HYDRO_H
#include "pstorage.h"
#include "ble_advdata.h"


//The enum of control operations corresponds to an equivalent enum on the client
//this way, the client and Ladybug are on the same page as to what command is being sent
typedef enum {
  resetPHcalValues,
  resetECcalValues,
  calibratePH4,
  calibratepH7,
  updatePHandEC,
  calibrateEC1,
  calibrateEC2,
  undoPH4,
  undoPH7,
  undoEC1,
  undoEC2,
  updateBatteryLevel,
  updateDeviceName
}control_enum_t;

// Subtract 2 (ADV_DATA_OFFSET in ble_advdata.c) .
// Subtract 3 to accommodate the bytes use for the flag info in advertisement packet.
// Subtract sizeof(uint16_le_t) to accommodate the addition for the UUID (SEE uuid_list_sized_encode() )

#define 		DEVNAME_MAX_LEN 		BLE_GAP_DEVNAME_MAX_LEN - 2 - 3 - sizeof(uint16_le_t)

/**
 * \brief This structure is set up to hold the mV values read from the AINs used in measuring either the pH or EC.  EC measurements use
 * both an EC_Vin, and EC_Vout.  This is why there are 2 int16's holding EC mV values.  There is an int16 unused so an instance of
 * this structure is word aligned.
 */
typedef struct {
  int16_t	EC_mV[2];   ///< EC_mV[0] is the AIN reading of EC_VIN.  EC_mV[1] is the EC_VOUT reading.
  int16_t	pH_mV;
  int16_t	unused;  ///< so the stucture is word (4 bytes) aligned
}measurements_t;
/**
 * \brief This struct sets up the mV readings measured for calibrating pH, EC1, or EC2.  There is also room to store the values the
 * user entered for the amount of µS/cm the calibration solution was made at (as stated on the label).
 * \note: make sure to be word aligned.
 */
typedef struct {
  int16_t 	pH4_mV; ///<pH4 should be ~ 178mV
  int16_t 	pH7_mV; ///<pH7 shoud be ~ 0mV
  uint16_t 	EC1solution; ///<the calibration solution's calibrated value in µS/cm for the first point of calibration.  Typically printed on the bottle
  uint16_t 	EC2solution; ///<same as EC1solution but for the second calibration point
  uint16_t 	EC1_mV[2];  ///<first byte = EC_VIN_mV for EC1 and second byte = EC_VOUT_mV
  uint16_t 	EC2_mV[2];  ///<two bytes for the same reason there are two bytes with EC1
}calibrationValues_t;
typedef struct {
 uint32_t 			write_check;
 calibrationValues_t		calValues;
}storeCalibrationValues_t;
/**
 * \brief The plant type might be tomato.  The growth type might be Seedling.  Since a flash block = 32 bytes, the plantInfo_t must be <= 28 bytes
 * so that there are 4 bytes for the write_check.  The most characters of a growth stage is 8 for Seedling.  This is why stage can be a max of 8 characters, which
 * leaves a max of 20 characters for the type.
 */
typedef struct {
  char     type[20];
  char     stage[8];
}plantInfo_t;
typedef union {
  plantInfo_t	plantInfo;
  uint8_t 	bytes[sizeof(plantInfo_t)];
}plantInfo_u;
typedef struct {
 uint32_t 			write_check;
 plantInfo_u		 	plantChar;
}storePlantInfo_t;
/**
 * \brief Used to determine if the pH calibration values or plant info has been written to storage (or are corrupt)
 */
#define WRITE_CHECK		0x01020304

/**
 * \brief The default device name is used when the code detects a device name has not been entered by the client.
 * The name must be <  DEVNAME_MAX_LEN
 * \sa DEVNAME_MAX_LEN
 * \sa ladybug_get_device_name()
 */
#define 	DEFAULT_DEVICE_NAME	"LBL"

void ladybug_get_measurements(measurements_t **p_measurements);
void ladybug_get_plantInfo(plantInfo_t **p_plantInfo);
void ladybug_get_calibrationValues(calibrationValues_t **p_calibrationValues);
void ladybug_get_calibration_values_memory_location(calibrationValues_t **p_calibrationValues);
void ladybug_get_device_name(char **p_deviceName);
void ladybug_update_calibration_value(uint8_t which_to_calibrate,int calValue);
void ladybug_undo_pH_calibration(control_enum_t command, int16_t pHCalValue);
void ladybug_undo_EC_calibration(control_enum_t command, int16_t EC_Vin, int16_t EC_Vout);
void ladybug_reset_calibration_values(control_enum_t command);
void ladybug_write_device_name(char *p_device_name,uint16_t len);
bool ladybug_there_are_calibration_values_to_write(storeCalibrationValues_t **p_storeCalibrationValues);
bool ladybug_there_are_plantInfo_values_to_write(storePlantInfo_t **p_storePlantInfo);
bool ladybug_the_device_name_has_been_updated(char **p_deviceName);

//ERROR CODES
#define		LADYBUG_ERROR_UNSURE_WHAT_DATA_TO_READ	101 ///<the function reading flash does not know what flash to read because the caller hasn't made this clear.
#define		LADYBUG_ERROR_UNSURE_WHAT_DATA_TO_WRITE	102 ///<the function write to flash does not know what flash block to write tobecause the caller hasn't made this clear.
#endif

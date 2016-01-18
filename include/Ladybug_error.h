/**
 * \file ladybug_error.h
 * \brief Identifies the error codes that are specific to the Ladybug.
 * \author  margaret johnson
 */

//#ifndef INCLUDE_LADYBUG_ERROR_H_
//#define INCLUDE_LADYBUG_ERROR_H_
//ERROR CODES
#define		LADYBUG_ERROR_FLASH_UNSURE_WHAT_DATA_TO_READ	101 ///<The function reading flash does not know what flash to read because the caller hasn't made this clear.
#define		LADYBUG_ERROR_NUM_BYTES_TO_WRITE			102 ///<The caller has told the function an incorrect number of bytes to write (i.e.: <= 0).
#define		LADYBUG_ERROR_NULL_POINTER			103 ///<A null pointer was passed into a function most likely to have it stuffed with a value.
#define		LADYBUG_ERROR_INVALID_COMMAND			104 ///<A function was called passing in a command that was invalid for that function.
#define		LADYBUG_ERROR_FLASH_ACTION_NOT_COMPLETED		105 ///<A call was made to a flash function in pstorage, but it did not finish before a timer went off.
//#endif

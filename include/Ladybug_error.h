/*
 * ladybug_error.h
 *
 *  Created on: Nov 12, 2015
 *      Author: margaret
 */

#ifndef INCLUDE_LADYBUG_ERROR_H_
#define INCLUDE_LADYBUG_ERROR_H_
#define APP_ERROR_HANDLER(ERR_CODE)                         \
    do                                                      \
    {                                                       \
        app_error_handler((ERR_CODE), __LINE__, (uint8_t*) __FILE__);  \
    } while (0)
//#else
//#define APP_ERROR_HANDLER(ERR_CODE)                         \
//    do                                                      \
//    {                                                       \
//        app_error_handler((ERR_CODE), 0, 0);  \
//    } while (0)
//#endif
/**@brief Macro for calling error handler function if supplied error code any other than NRF_SUCCESS.
 *
 * @param[in] ERR_CODE Error code supplied to the error handler.
 */
#define APP_ERROR_CHECK(ERR_CODE)                           \
    do                                                      \
    {                                                       \
        const uint32_t LOCAL_ERR_CODE = (ERR_CODE);         \
        if (LOCAL_ERR_CODE != NRF_SUCCESS)                  \
        {                                                   \
            APP_ERROR_HANDLER(LOCAL_ERR_CODE);              \
        }                                                   \
    } while (0)
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name);
void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name);
#endif /* INCLUDE_LADYBUG_ERROR_H_ */

/*
 * Ladybug_ADC.h
 *
 *  Created on: Oct 18, 2015
 *      Author: margaret
 */
#ifndef INCLUDE_LADYBUG_ADC_H_
#define INCLUDE_LADYBUG_ADC_H_

#include <stdint.h>
//Define the private ADC interface
typedef struct {
  int32_t (*read)(uint8_t which_ain);
}ADC_interface;

/**
 * \brief mapping of the AINs on the nRF51822 to a hydro mV reading.  The caller of adc.read() passes one of these
 * in when reading the ADC.  e.g.: adc.read(pH_AIN) reads the mV value for pH.
 * \note These could change with board revisions.
 */
#define pH_VGND 6
#define pH_AIN  7
#define	EC_VGND	3
#define EC_VIN  4
#define EC_VOUT 5

#endif /* INCLUDE_LADYBUG_ADC_H_ */

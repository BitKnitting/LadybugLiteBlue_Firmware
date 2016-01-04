/**
 * \file 	Ladybug_ADC.c
 * \author	Margaret Johnson
 * \version	1.0
 * \brief	Encapsulates the functions used to read an AIN on the nRF51822.
 * \details	The caller includes Ladybug_ADC.h and instantiates an extern variable e.g.: extern ADC_interface adc;
 * 		then read - say AIN1 with int32_t adc_result_mV = adc.read(1);  There is a Nordic white paper: "White paper
 * 		content - nrf51 ADC.pdf" available on the Nordic web site.
 */

#include <stdint.h>
#include "Ladybug_ADC.h"
#include "nrf_adc.h"
#include "nrf_soc.h"
#include "SEGGER_RTT.h"

/**
 * \brief macros used for pre-scaling and conversion from a LSB to mV.
 */
#define ADC_PRE_SCALING_COMPENSATION      3
#define ADC_REF_VOLTAGE_IN_MILLIVOLTS     1200
#define ADC_RESULT_IN_MILLI_VOLTS(ADC_VALUE)\
    (((ADC_VALUE) * ADC_REF_VOLTAGE_IN_MILLIVOLTS/1023) * ADC_PRE_SCALING_COMPENSATION)
/**
 * \brief return the ADC value in millivolts
 * @param which_AIN		A digit between 0 and 7 representing the AIN number
 * @return			The value read from the ADC in millivolts
 */
static int32_t read(uint8_t which_AIN){
  int32_t adc_value_in_mV = 0;
  /**
   * \callgraph
   * \brief The Nordic SDK has several (and slightly different :-) ) examples on how to get an ADC sample out of an AIN.  I decided to talk directly to
   * the registers because ultimately this gives me more control on what is going on.  The [nRF51_Series_Reference_manual v3.0.pdf](http://www.nordicsemi.com/eng/content/download/13233/212988/file/nRF51_Series_Reference_manual%20v3.0.pdf)
   * has an image of what is needed to configure the ADC peripheral for a reading:
   * \image html nRF51822_ADC.jpg
   *
   * \param	which_AIN:		these are in nRF51_bitfields.h.  I've defined some simple naming at the top of this source file.
   * @return  	adc_result:		the value read from the ADC configured for one of the 8 AIN pins.
   */
  /*!
   * nrf51_bitfields.h define macros like ADC_CONFIG_PSEL_AnalogInput0 (1UL) ... ADC_CONFIG_PSEL_AnalogInput7 (128UL)... so if the caller says "I want AIN 7"  the ADC_AnalogInput = 1 << 7 = 128
   */
  unsigned long ADC_AnalogInput = 0x00000000 | 1 << which_AIN;
  /*!
   * \brief *->set the bits in NRF_ADC->CONFIG to configure ADC sampling to use the internal 1.2V bandgap voltage, 10 bit resolution, 1/3 prescaling, and the AIN to sample from
   */
  NRF_ADC->CONFIG	= (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos) /* Not using an external reference for AREF */
  													| (ADC_AnalogInput << ADC_CONFIG_PSEL_Pos) /* Sets which AIN (0-7) to sample from */
													| (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos) /* use the internal 1.2V bandgap voltage as reference */
													| (ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) /* use 1/3 prescaling */
													| (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos);	/* use 10 bit resolution when sampling */

  /*!
   * \brief *->enable the ADC by setting the NRF_ADC->ENABLE register
   */
  NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Enabled;

  /*!
   * \brief *->an ADC sample starts when NRF_ADC->TASKS_START is set to 1
   */
  NRF_ADC->TASKS_START = 1;
  /*!
   * \brief *->as noted in the nRF51_Series_Reference_manual v3.0.pdf: "During sampling the ADC will enter a busy state.  The ADC busy/ready state can be monitored
   * via the BUSY register."
   * \note The "White paper content - nrf51 ADC.pdf" states: ..."multiple samples are made and the ADC output value is the mean value from the sample pool...the sample pool is created
   * during 20µS period for 8 bit sample, 36µS for 9 bit sampling, and 68µS for 10 bit sampling.  I originally was going to take multiple samples and average - but heck, looks like the
   * smart folks at Nordic took care of this! :-)
   */
  while (NRF_ADC->BUSY){
  }
  /*!
   * \brief *->the results are ready to be copied from the NRF_ADC->RESULT register.
   */
  int16_t adc_result = NRF_ADC->RESULT;
  /**
   * \brief *->while 31.1.6 of the nRF51_Series_Reference_manual v3.0.pdf poings out the ADC supports one-shot operation, the code seems to still have to tell the ADC to stop
   * using NRF_ADC->TASKS_STOP = 1;
   */
  NRF_ADC->TASKS_STOP = 1;
/*!
 * \brief *-> after the ADC has been used, might as well disable
 */
  NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Disabled;
  /*!
   * \brief *-> convert the value to millivolts
   */
  adc_result = ADC_RESULT_IN_MILLI_VOLTS(adc_result);
  return adc_result;
}
/**
 * \brief adc is an instance of the typedef'd structure that defines pointers to (static) functions.  This way, folks can use a public name but not be directly calling the internal functions for interacting
 * with the ADC.
 */
ADC_interface adc = {
    read
};

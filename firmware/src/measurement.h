/**
 *******************************************************************************
 * @file    measurement.h
 * @author  Bertrand Massot (bertrand.massot@insa-lyon.fr)
 * @date    2022-03-29
 * @brief   Measurement thread module header file
 *******************************************************************************
 */

#ifndef __MEASUREMENT_H__
#define __MEASUREMENT_H__

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * INCLUDES
 ******************************************************************************/

/*******************************************************************************
 * MACROS AND DEFINES
 ******************************************************************************/

/*******************************************************************************
 * TYPEDEFS
 ******************************************************************************/

/*******************************************************************************
 * EXPORTED VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * GLOBAL FUNCTION PROTOTYPES
 ******************************************************************************/

void MEAS_Init(void);

void MEAS_Enable(bool enable);

void MEAS_Read(void);

/**
 * @brief Thread to execute impedance measurement into a separate thread
 * which can be controlled / monitored
 * @param [in] p1 not used
 * @param [in] p2 not used
 * @param [in] p3 not used
 */
void MEAS_Thread(void * p1, void * p2, void * p3);

/**
 * @brief Initialize the calendar
 */

#ifdef __cplusplus
}
#endif

#endif /* __MEASUREMENT_H__ */
/*****************************************************************/ /**
* @file sim_demo.h
* @brief
* @author neo.jiang@quectel.com
* @date 2025-10-13
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description"
* <tr><td>2025-10-13 <td>1.0 <td>neo.jiang <td> Init
* </table>
**********************************************************************/
#ifndef __UNIRTOS_SIM_DEMO_H__
#define __UNIRTOS_SIM_DEMO_H__

#include "qosa_log.h"
#include "qosa_sim.h"

/*===========================================================================
 *  Macro Definition
 ===========================================================================*/
 
// Define these macros in sim_demo.h or the build command line to control the test content.
// When a macro is set to 1, the corresponding functional test is enabled.
#define TEST_BASIC_INFO  1  // Test basic information acquisition (SIM status, ICCID, IMSI, etc.)
#define TEST_PIN_OPS     0  // Test PIN code operations (setting up, verifying, unlocking) [Test with caution, may lock your card]
#define TEST_APDU_ACCESS 0  // Tested by directly accessing the SIM card via APDU
#define TEST_HOT_SWAP    0  // Test the hot-plug detection function (hardware support required).
#define TEST_DUAL_SIM    0  // Test dual SIM single standby functionality (requires dual SIM hardware)

/*===========================================================================
 *  Function Declaration
 ===========================================================================*/
void unir_sim_demo_init(void);

#endif  // __UNIRTOS_SIM_DEMO_H__
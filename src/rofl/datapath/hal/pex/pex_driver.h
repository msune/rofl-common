/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PEX_DRIVER_H
#define PEX_DRIVER_H

/**
* @file pex_driver.h
* @author Ivano Cerrato<ivano.cerrato (at) polito.it>
*
* @brief PEX related operations exposed to the CMM by the driver
* 
*/

#include "../hal.h"
#include "../hal_utils.h"
#include "../../pipeline/pex_connected.h"

//C++ extern C
HAL_BEGIN_DECLS

/**
 * @name hal_driver_pex_exists
 * @brief Checks if a PEX with the specified name exists
 *
 * @param pex_name	Name of the PEX to be checked
 */
bool hal_driver_pex_exists(const char *pex_name);

/**
* @brief   Retrieve the list of names of the available PEX of the platform 
* @ingroup pex_management
* @retval  List of available PEX names, which MUST be deleted using pex_name_list_destroy().
*/
pex_name_list_t* hal_driver_get_all_pex_names();

/**
 * @name    hal_result_t hal_driver_pex_create_pex
 * @brief   Instructs driver to create a new PEX 
 *
 * @param pex_name				Name of the PEX to be created
 * @param path					Path of the script to be used to run the PEX
 * @param core_mask				Core to which the PEX must be bound
 * @param num_memory_channels	Number of memory channels used by the PEX
 * @param lcore_id				Identifier needed to support multiple PEX on the same core

 */
hal_result_t hal_driver_pex_create_pex(const char *pex_name, const char *path, uint32_t core_mask, uint32_t num_memory_channels, uint32_t lcore_id);

/**
 * @name    hal_result_t hal_driver_pex_destroy_pex
 * @brief   Instructs driver to destroy a PEX 
 *
 * @param pex_name		Name of the PEX to be destroyed
 */
hal_result_t hal_driver_pex_destroy_pex(const char *pex_name);

/**
 * @name    hal_result_t hal_driver_pex_start_pex
 * @brief   Instructs driver to start a PEX (associated with an existing PEX port)
 *
 * @param pex_id				Identifier of the PEX to be created
 */
hal_result_t hal_driver_pex_start_pex(uint32_t pex_id);

/**
 * @name    hal_result_t hal_driver_pex_stop_pex
 * @brief   Instructs driver to stop a PEX 
 *
 * @param pex_id		Identifier of the PEX to be stopped
 */
hal_result_t hal_driver_pex_stop_pex(uint32_t pex_id);


// [+] Add more here..

//C++ extern C
HAL_END_DECLS

#endif /* PEX_DRIVER_H */


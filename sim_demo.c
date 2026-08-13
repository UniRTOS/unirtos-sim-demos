/*****************************************************************/ /**
* @file sim_demo.c
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
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_sys.h"
#include "sim_demo.h"
#include "qosa_dev1.h"
#include "qosa_event_notify.h"
#include "qosa_power.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG LOG_TAG_DEMO

/** sim task handle */
qosa_task_t g_sim_demo_task = QOSA_NULL;

//demo universal semaphore
qosa_sem_t g_sim_demo_sem = QOSA_NULL;

//Dedicated semaphores for demo cpin ready
qosa_sem_t g_sim_demo_cpin_ready_sem = QOSA_NULL;

//Dedicated semaphores for demo sim hot swap
qosa_sem_t g_sim_demo_hot_swap_sem = QOSA_NULL;

//demo asynchronous operation result message queue
qosa_msgq_t g_sim_demo_async_ret_msg = QOSA_NULL;

/*------------------------------------------
 * Event callback handling
 *------------------------------------------*/
/**
 * SIM card status event callback function
 * 
 * @param [in] user_argv
 *           - User-defined parameters, not used in this function.
 * @param [in] argv
 *           - The event parameter pointer points to the qosa_sim_status_event_t structure.
 * 
 * @return A return value of 0 indicates successful processing.
 */
static int sim_demo_sim_status_event_cb(void *user_argv, void *argv)
{
    qosa_sim_status_event_t *ev = (qosa_sim_status_event_t *)argv;
    QLOGI("[SIM EVENT] SIM%d Status Changed: %d", ev->simid, ev->status);

    // Check SIM card status, if it is ready state
    if (QOSA_SIM_STATUS_READY == ev->status)
    {
        QLOGI("[SIM EVENT] SIM%d is ready", ev->simid);
        // If the SIM card status is ready and g_sim_demo_cpin_ready_sem is not NULL, release the semaphore
        if (QOSA_NULL != g_sim_demo_cpin_ready_sem)
        {
            qosa_sem_release(g_sim_demo_cpin_ready_sem);
            QLOGI("[SIM EVENT] release ready sem");
        }
    }
    return 0;
}
#if TEST_HOT_SWAP
/**
 * @brief SIM card insert status event callback function
 * 
 * This function handles SIM card insert status change events, performs corresponding log recording
 * based on SIM card status, and releases semaphore when needed to notify other waiting threads.
 * 
 * @param [in] user_argv 
 *           - User-defined parameter, not used
 * @param [in] argv 
 *           - Event parameter pointer, pointing to qosa_sim_insert_status_event_t structure
 * 
 * @return int Returns 0 to indicate successful processing
 */
static int sim_demo_sim_insert_status_event_cb(void *user_argv, void *argv)
{
    qosa_sim_insert_status_event_t *ev = (qosa_sim_insert_status_event_t *)argv;
    if (ev->stat == QOSA_SIM_INSERT_STAT_REMOVED)
    {
        QLOGI("[SIM EVENT] SIM card is not inserted");
    }
    else if (ev->stat == QOSA_SIM_INSERT_STAT_INSERTED)
    {
        QLOGI("[SIM EVENT] SIM card insertion status");
    }
    else
    {
        QLOGI("[SIM EVENT] SIM card status unknown");
    }
    if (QOSA_NULL != g_sim_demo_hot_swap_sem)
    {
        qosa_sem_release(g_sim_demo_hot_swap_sem);
    }
    return 0;
}
#endif /* TEST_HOT_SWAP */

/*------------------------------------------
 * Asynchronous interface callback processing
 *------------------------------------------*/
/**
 * @brief SIM card get IMSI asynchronous callback function
 * 
 * This function is used to process the asynchronous response result of getting IMSI number from SIM card.
 * When the SIM card operation is completed, the system will call this callback function to notify the upper application of the operation result.
 * 
 * @param [in] ctx 
 *           - Context pointer, not used in this function
 * @param [in] argv 
 *           - Pointer to the callback parameter structure, containing IMSI acquisition result information
 * 
 * @return No return value
 */
static void sim_demo_get_imsi_cb(void *ctx, void *argv)
{
    qosa_sim_get_imsi_cnf_t *cnf = argv;
    QOSA_UNUSED(ctx);

    // Check get imsi result
    if (QOSA_SIM_ERR_OK == cnf->err_code)
    {
        // Operation successful, print the obtained IMSI number
        QLOGI("[async] get IMSI: %s", cnf->imsi.imsi);
    }
    else
    {
        // Operation failed, print error code
        QLOGI("[async] get IMSI fail: %x", cnf->err_code);
        ;
    }
    qosa_sem_release(g_sim_demo_sem);
}

/**
 * @brief SIM card get ICCID asynchronous callback function
 * 
 * @param [in] ctx 
 *           - Callback context parameter, not used in this function
 * @param [in] argv 
 *           - Callback parameter pointer, pointing to qosa_sim_get_iccid_cnf_t structure
 * 
 * @return No return value
 */
static void sim_demo_get_iccid_cb(void *ctx, void *argv)
{
    qosa_sim_get_iccid_cnf_t *cnf = argv;
    QOSA_UNUSED(ctx);

    // Check get iccid result
    if (QOSA_SIM_ERR_OK == cnf->err_code)
    {
        QLOGI("[async] get ICCID: %s", cnf->iccid.id);
    }
    else
    {
        QLOGI("[async] get ICCID fail: %x", cnf->err_code);
        ;
    }
    qosa_sem_release(g_sim_demo_sem);
}

/**
 * @brief SIM demo set CFUN callback function
 *
 * This function serves as the callback function for qosa_dev_set_cfun API, called after the API execution is completed.
 * The main function is to record the CFUN setting result and release the semaphore to allow the program to continue execution.
 *
 * @param[in] ctx
 *          - Context pointer
 *
 * @param[in] argv
 *          - Callback parameter pointer, pointing to qosa_dev_general_cnf_t structure, containing CFUN setting operation result information
 */
static void sim_demo_set_cfun_cb(void *ctx, void *argv)
{
    qosa_dev_general_cnf_t *cnf = argv;
    QLOGI("cfun result=%d", cnf->err_code);

    // Release CFUN semaphore to let the program continue running
    qosa_sem_release(g_sim_demo_sem);
}

/**
 * @brief SIM card function test function, sequentially execute CFUN=0 and CFUN=1 operations
 * 
 * @param [in] simid
 *           - SIM card identifier
 * 
 * @return Function execution result, 0 indicates success, other values indicate failure
 */
static int sim_demo_test_cfun_01(qosa_uint8_t simid)
{
    int ret = 0;

    // Set CFUN=0, disable SIM card function
    QLOGI("[CFUN]: 0");
    ret = qosa_dev_set_cfun(simid, 0, sim_demo_set_cfun_cb, QOSA_NULL);
    QLOGI("ret=%d,qosa_dev_set_cfun0", ret);
    // Wait for sim_demo_set_cfun_cb callback function to release semaphore
    qosa_sem_wait(g_sim_demo_sem, QOSA_WAIT_FOREVER);

    // Set CFUN=1, enable SIM card function
    QLOGI("[CFUN]: 1");
    ret = qosa_dev_set_cfun(simid, 1, sim_demo_set_cfun_cb, QOSA_NULL);
    QLOGI("ret=%d,qosa_dev_set_cfun1", ret);

    // Wait for sim_demo_set_cfun_cb callback function to release semaphore
    qosa_sem_wait(g_sim_demo_sem, QOSA_WAIT_FOREVER);

    return ret;
}

/**
 * @brief Check the basic information and status of the specified SIM card, including insertion status, initialization status, and readiness status,
 *        and read its IMSI and ICCID information (synchronous and asynchronous methods) when the SIM card is ready.
 *
 * @param [in] simid 
 *           - The identifier of the SIM card, used to specify which SIM card to operate on.
 *
 * @return 
 *     - QOSA_SIM_ERR_OK: Operation completed successfully;
 *     - Other negative values: Indicate errors occurred during the process, specific error codes are returned by related interfaces.
 *
 * @note This function will check in sequence whether the SIM card is inserted, whether initialization is complete, and whether it is in ready status.
 *       If the SIM card is ready, it will attempt to obtain IMSI and ICCID information through both synchronous and asynchronous methods.
 */
static int sim_demo_test_basic_info_sim_status_check(qosa_uint8_t simid)
{
    qosa_sim_status_e status = QOSA_SIM_STATUS_READY;
    qosa_sim_iccid_t  iccid = {0};
    ;
    qosa_sim_imsi_t imsi = {0};
    int             ret = 0;
    qosa_int32_t    qosa_err = 0;

#if TEST_HOT_SWAP
    // Check SIM insertion status
    ret = qosa_sim_read_insert_stat(simid);
    if (ret != QOSA_SIM_INSERT_STAT_INSERTED)
    {
        QLOGE("Read SIM insert status failed: 0x%x", ret);
        return ret;
    }
    QLOGI("SIM insert status: 0x%x", ret);
#endif /* TEST_HOT_SWAP */

    // Check SIM status
    ret = qosa_sim_read_status(simid, &status);
    if (ret != QOSA_SIM_ERR_OK)
    {
        QLOGE("Read SIM status failed: 0x%x", ret);
        return ret;
    }
    QLOGI("SIM Status: 0x%x", status);

    // Check init status
    ret = qosa_sim_read_init_stat(simid);
    QLOGI("SIM init status: 0x%x", ret);

    // If card is ready, read ICCID and IMSI
    if (status == QOSA_SIM_STATUS_READY)
    {
        // Synchronously read IMSI
        ret = qosa_sim_read_imsi(simid, &imsi);
        if (ret == QOSA_SIM_ERR_OK)
        {
            QLOGI("[sync] read IMSI: %s", imsi.imsi);
        }
        else
        {
            QLOGE("[sync] Read IMSI failed: 0x%x", ret);
        }
        // Synchronously read ICCID
        ret = qosa_sim_read_iccid(simid, &iccid);
        if (ret == QOSA_SIM_ERR_OK)
        {
            QLOGI("[sync] read ICCID: %s", iccid.id);
        }
        else
        {
            QLOGE("[sync] Read ICCID failed: 0x%x", ret);
        }
        // Asynchronously get IMSI and wait for result
        qosa_err = qosa_sim_get_imsi(simid, sim_demo_get_imsi_cb, QOSA_NULL);
        if (QOSA_SIM_ERR_OK != qosa_err)
        {
            QLOGE("qosa_sim_get_imsi failed: 0x%x", qosa_err);
        }
        // Wait for asynchronous interface to return result
        ret = qosa_sem_wait(g_sim_demo_sem, 5000);
        if (ret != QOSA_OK)
        {
            QLOGE("wait async IMSI fail: 0x%x", ret);
            return ret;
        }

        // Asynchronously get ICCID and wait for result
        qosa_err = qosa_sim_get_iccid(simid, sim_demo_get_iccid_cb, QOSA_NULL);
        if (QOSA_SIM_ERR_OK != qosa_err)
        {
            QLOGE("qosa_sim_get_iccid failed: 0x%x", qosa_err);
        }
        // Wait for asynchronous interface to return result
        ret = qosa_sem_wait(g_sim_demo_sem, 5000);
        if (ret != QOSA_OK)
        {
            QLOGE("wait async ICCID fail: 0x%x", ret);
            return ret;
        }
    }
    else
    {
        ret = QOSA_ERROR_GENERAL;
        QLOGE("sim not ready");
    }
    return ret;
}

#if TEST_HOT_SWAP
/**
 * @brief Enable SIM card hot swap function test
 * 
 * This function is used to configure and enable the hot swap detection function for the specified SIM card,
 * set the insertion level to low level trigger, and use the default GPIO pin.
 * 
 * @param [in] simid 
 *           - SIM card identifier, used to specify which SIM card slot to configure
 * 
 * @return Returns the operation result, QOSA_SIM_ERR_OK indicates success, other values indicate failure
 */
static int sim_demo_test_hot_swap_enable(qosa_uint8_t simid)
{
    qosa_sim_hot_swap_cfg_t hot_swap_cfg = {0};
    int                     ret = 0;

    /* Configure hot swap parameters: enable function, set insertion level to high level, use default GPIO */
    hot_swap_cfg.enable = 1;
    hot_swap_cfg.insert_level = 1;
    hot_swap_cfg.gpio = QOSA_SIM_HOT_SWAP_UNSPECIFIED_GPIO;

    /* Call the underlying interface to set SIM card hot swap configuration */
    ret = qosa_sim_set_sim_hot_swap(simid, &hot_swap_cfg);
    if (QOSA_SIM_ERR_OK != ret)
    {
        QLOGE("set sim hot swap error: 0x%x", ret);
    }
    return ret;
}

/**
 * @brief Used to check if hot swap detection function is enabled
 * 
 * 
 * @param [in] simid 
 *           - SIM card identifier, used to specify which SIM card slot to configure
 * 
 * @return Returns the enable result, QOSA_TRUE indicates enabled; QOSA_FALSE indicates not enabled
 */
static qosa_bool_t sim_demo_test_hot_swap_enabled_check(qosa_uint8_t simid)
{
    qosa_sim_hot_swap_cfg_t hot_swap_cfg = {0};
    int                     ret = 0;

    /* Call the underlying interface to set SIM card hot swap configuration */
    ret = qosa_sim_get_sim_hot_swap(simid, &hot_swap_cfg);
    if (QOSA_SIM_ERR_OK != ret)
    {
        QLOGE("get sim hot swap error: 0x%x", ret);
        return QOSA_FALSE;
    }
    QLOGI("hot swap enable:%d, insert_level:%d", hot_swap_cfg.enable, hot_swap_cfg.insert_level);
    if (hot_swap_cfg.enable == 1)
    {
        return QOSA_TRUE;
    }
    else
    {
        return QOSA_FALSE;
    }
}
#endif /* TEST_HOT_SWAP */

/*------------------------------------------
 * Basic Information Acquisition Test (TEST_BASIC_INFO)
 *------------------------------------------*/
#if TEST_BASIC_INFO
/**
 * @brief SIM card basic information test function
 *  This function is used to test the basic information of the SIM card, including SIM card status check, CPIN status waiting, CFUN mode switching and other operations
 * 
 * @param [in] simid 
 *           - SIM card ID identifier
 * 
 * @return QOSA_OK indicates that the test was successful, other values indicate that the test failed
 */
static int sim_demo_test_basic_info(qosa_uint8_t simid)
{
    int ret = 0;

    // Create cpin ready semaphore, used to wait for CPIN ready
    qosa_sem_create(&g_sim_demo_cpin_ready_sem, 0);

    do {
        QLOGI("=== Testing Basic Info for SIM%d ===", simid);

        // Check SIM card status: whether inserted, check current status, get init status
        ret = sim_demo_test_basic_info_sim_status_check(simid);
        if (ret != QOSA_OK)
        {
            QLOGE("Sim status first check failed: 0x%x", ret);
            break;
        }

        if (ret != QOSA_OK)
        {
            QLOGE("Create cpin ready semaphore failed: 0x%x", ret);
            break;
        }
        // CFUN 0 1 switching
        sim_demo_test_cfun_01(simid);

        // Wait for CPIN ready signal
        qosa_sem_wait(g_sim_demo_cpin_ready_sem, QOSA_WAIT_FOREVER);

        // Check SIM card status again: whether inserted, check current status, get init status
        ret = sim_demo_test_basic_info_sim_status_check(simid);
        if (ret != QOSA_OK)
        {
            QLOGE("Sim status second check failed:0x%x", ret);
            break;
        }
    } while (0);

    // Clean up semaphore resources
    qosa_sem_delete(g_sim_demo_cpin_ready_sem);
    g_sim_demo_cpin_ready_sem = QOSA_NULL;

    QLOGI("=== Testing Basic Info Done ===");
    return ret;
}
#endif /* TEST_BASIC_INFO */

/*------------------------------------------
 * PIN Code Operations Test (TEST_PIN_OPS) - Use with caution!
 !!WARNING!! This code is for example purposes only. Actual PIN code operations may cause the SIM card to be locked!
 *------------------------------------------*/
#if TEST_PIN_OPS

/**
 * @brief SIM card facility lock setting callback function
 *
 * This function is used to handle the response results of the SIM card facility lock setting command
 *
 * @param [in] ctx
 *           - AT command context pointer, containing command-related information
 * @param [in] argv
 *           - SIM card general confirmation information pointer, containing execution results and error codes
 *
 */
static void sim_demo_set_facility_lock_cb(void *ctx, void *argv)
{
    qosa_sim_general_cnf_t *cnf = (qosa_sim_general_cnf_t *)argv;

    QOSA_UNUSED(ctx);
    QLOGI("set facility lock result=%d", cnf->err_code);
    qosa_msgq_release(g_sim_demo_async_ret_msg, sizeof(qosa_sim_general_cnf_t), (qosa_uint8_t *)cnf, 0);
}

/**
 * @brief SSIM card facility lock status query callback function
 *
 * This function is used to handle the response results of the SIM card facility lock status query
 *
 * @param [in] ctx
 *           - AT command context pointer, containing command-related information
 * @param [in] argv
 *           - SIM card general confirmation information pointer, containing execution results and error codes
 *
 */
static void sim_demo_get_facility_lock_cb(void *ctx, void *argv)
{
    qosa_sim_get_facility_lock_cnf_t *cnf = argv;
    QOSA_UNUSED(ctx);
    QLOGI("get facility lock result=%d", cnf->err_code);
    qosa_msgq_release(g_sim_demo_async_ret_msg, sizeof(qosa_sim_get_facility_lock_cnf_t), (qosa_uint8_t *)cnf, 0);
}

/**
 * @brief SIM card PIN code verification callback function
 * This function is used to handle the results of SIM card PIN code verification operations
 *
 * @param [in] ctx
 *           - AT command context pointer, containing command-related information
 * @param [in] argv
 *           - SIM card general confirmation information pointer, containing operation results
 */
static void sim_demo_sim_cpin_cb(void *ctx, void *argv)
{
    qosa_sim_general_cnf_t *cnf = argv;
    QOSA_UNUSED(ctx);

    QLOGI("ver pin  result=%d", cnf->err_code);
    qosa_msgq_release(g_sim_demo_async_ret_msg, sizeof(qosa_sim_general_cnf_t), (qosa_uint8_t *)cnf, 0);
}

/**
 * @brief Test function for verifying SIM card PIN code operations
 *
 * This function simulates the verification of a SIM card's PIN code. It creates a message queue to receive asynchronous operation results,
 * calls the qosa_sim_verify_pin interface to send a verification request, and waits for the verification result.
 *
 * @param [in] simid
 *           - Specifies the SIM card identifier to operate on
 * 
 * @return int Returns the operation result, QOSA_OK indicates success, other values indicate failure
 */
static int sim_demo_test_pin_operations_verify_pin(qosa_uint8_t simid)
{
    int                    ret = 0;
    qosa_sim_pin_t         pin = {4, "1234"};
    qosa_sim_general_cnf_t cnf = {0};
    do {
        // Create an asynchronous return result message queue to receive qosa_sim_verify_pin results
        ret = qosa_msgq_create(&g_sim_demo_async_ret_msg, sizeof(qosa_sim_general_cnf_t), 4);
        if (ret != QOSA_OK)
        {
            QLOGE("Create g_sim_demo_async_ret_msg failed: %d", ret);
            break;
        }

        // Send qosa_sim_verify_pin command and register callback function
        ret = qosa_sim_verify_pin(simid, &pin, sim_demo_sim_cpin_cb, QOSA_NULL);
        if (ret != QOSA_SIM_ERR_OK)
        {
            QLOGE("qosa_sim_verify_pin failed: 0x%x", ret);
            break;
        }

        // Wait for asynchronous interface to return results
        ret = qosa_msgq_wait(g_sim_demo_async_ret_msg, (qosa_uint8_t *)&cnf, sizeof(qosa_sim_general_cnf_t), 5000);
        if (QOSA_OK != ret)
        {
            if (QOSA_SIM_ERR_OK == cnf.err_code)
            {
                QLOGI("verify pin success");
                ret = QOSA_OK;
            }
            else
            {
                QLOGE("verify pin error: 0x%x", cnf.err_code);
                break;
            }
        }
        else
        {
            QLOGE("wait verify pin result error: 0x%x", ret);
            break;
        }

    } while (0);

    // Clean up resources: delete the previously created message queue
    if (g_sim_demo_async_ret_msg != QOSA_NULL)
    {
        qosa_msgq_delete(g_sim_demo_async_ret_msg);
        g_sim_demo_async_ret_msg = QOSA_NULL;
    }
    return ret;
}

/**
 * @brief Check if the SIM card's PIN lock status meets expectations.
 *
 * This function asynchronously calls the qosa_sim_get_facility_lock interface to obtain the current SIM card facility lock (PIN lock) status,
 * and judges whether its status matches the expectation according to the incoming parameters. It supports detecting both locked and unlocked states.
 *
 * @param [in] locked
 *           - Expected PIN lock status: QOSA_TRUE indicates it should be in a locked state; QOSA_FALSE indicates it should be unlocked.
 * @param [in] simid
 *           - SIM card identifier, used to specify which SIM card to operate on.
 *
 * @return QOSA_OK Success and status match;
 *         Other values indicate failure, including message queue creation failure, interface call errors, timeout waiting, status mismatch, etc.
 */
static int sim_demo_test_pin_operations_check_pin_stat(qosa_bool_t locked, qosa_uint8_t simid)
{
    int                              ret = 0;
    qosa_sim_get_facility_lock_cnf_t cnf = {0};

    do {
        // Create message queue to receive qosa_sim_get_facility_lock results
        ret = qosa_msgq_create(&g_sim_demo_async_ret_msg, sizeof(qosa_sim_get_facility_lock_cnf_t), 4);
        if (ret != QOSA_OK)
        {
            QLOGE("Create g_sim_demo_async_ret_msg failed: %d", ret);
            break;
        }

        // Initiate asynchronous request to get SIM card facility lock status
        ret = qosa_sim_get_facility_lock(simid, QOSA_SIM_FACILITY_SC, sim_demo_get_facility_lock_cb, QOSA_NULL);
        if (ret != QOSA_SIM_ERR_OK)
        {
            QLOGE("qosa_sim_get_facility_lock  failed: 0x%x", ret);
            break;
        }
        // Wait for asynchronous interface to return results
        ret = qosa_msgq_wait(g_sim_demo_async_ret_msg, (qosa_uint8_t *)&cnf, sizeof(qosa_sim_get_facility_lock_cnf_t), 5000);
        if (QOSA_OK != ret)
        {
            // PIN lock status obtained successfully
            if (QOSA_SIM_ERR_OK == cnf.err_code)
            {
                QLOGI("get lock status success");
            }
            else  // PIN lock status acquisition failed
            {
                QLOGE("get lock status error: 0x%x", cnf.err_code);
                ret = cnf.err_code;
                break;
            }
        }
        else  // PIN lock status result timeout
        {
            QLOGE("wait get lock result error: 0x%x", ret);
            break;
        }

        // Verify whether the actual returned status is consistent with the expected status
        if (locked)
        {
            if (cnf.status != QOSA_SIM_STATUS_SIM_PIN)
            {
                QLOGE("SIM is not locked");
                ret = QOSA_SIM_ERR_FAILURE;
            }
            else
            {
                QLOGI("SIM is locked");
                ret = QOSA_OK;
            }
        }
        else
        {
            if (cnf.status != QOSA_SIM_STATUS_READY)
            {
                QLOGE("SIM is not ready");
                ret = QOSA_SIM_ERR_FAILURE;
            }
            else
            {
                QLOGI("SIM is locked");
                ret = QOSA_OK;
            }
        }

    } while (0);

    // Clean up resources: delete the previously created message queue
    if (g_sim_demo_async_ret_msg != QOSA_NULL)
    {
        qosa_msgq_delete(g_sim_demo_async_ret_msg);
        g_sim_demo_async_ret_msg = QOSA_NULL;
    }
    return ret;
}

/**
 * @brief Configure SIM card PIN operations, including unlocking and setting PIN code
 *
 * This function sets the SIM card facility lock (such as SC lock) by calling the qosa_sim_set_facility_lock interface,
 * and waits for asynchronous callback results to confirm whether the operation was successful. Message queues are used for asynchronous communication processing.
 *
 * @param [in] simid 
 *           - SIM card identifier, specifying which SIM card to operate on.
 * @param [in] mode
 *           - Operation mode, 0-unlock PIN code 1-set PIN code
 * 
 * @return int      Returns the operation result, QOSA_OK indicates success, other values indicate error codes.
 */
static int sim_demo_test_pin_operations_config_pin(qosa_uint8_t simid, qosa_uint8_t mode)
{
    int                    ret = 0;
    qosa_sim_general_cnf_t cnf = {0};
    qosa_sim_pin_t         pin = {4, "1234"};

    do {
        // Create message queue to receive qosa_sim_set_facility_lock results
        ret = qosa_msgq_create(&g_sim_demo_async_ret_msg, sizeof(qosa_sim_general_cnf_t), 4);
        if (ret != QOSA_OK)
        {
            QLOGE("Create g_sim_demo_async_ret_msg failed: %d", ret);
            break;
        }

        // Call interface to set SIM card facility lock (e.g., SC lock), and pass PIN code and callback function */
        ret = qosa_sim_set_facility_lock(simid, QOSA_SIM_FACILITY_SC, &pin, mode, 7, sim_demo_set_facility_lock_cb, QOSA_NULL);
        if (QOSA_SIM_ERR_OK != ret)
        {
            QLOGE("qosa_sim_set_facility_lock error: 0x%x", ret);
            break;
        }

        // Wait for asynchronous interface to return results
        ret = qosa_msgq_wait(g_sim_demo_async_ret_msg, (qosa_uint8_t *)&cnf, sizeof(qosa_sim_general_cnf_t), 10000);
        if (QOSA_OK == ret)
        {
            if (QOSA_SIM_ERR_OK == cnf.err_code)
            {
                QLOGI("lock pin success");
                ret = QOSA_OK;
            }
            else
            {
                QLOGI("lock pin error: 0x%x", cnf.err_code);
                break;
            }
        }
        else  // Timeout or other error
        {
            QLOGE("wait lock pin result error: 0x%x", ret);
            break;
        }

    } while (0);

    // Clean up resources: delete the previously created message queue
    if (g_sim_demo_async_ret_msg != QOSA_NULL)
    {
        qosa_msgq_delete(g_sim_demo_async_ret_msg);
        g_sim_demo_async_ret_msg = QOSA_NULL;
    }
    return ret;
}

/**
 * @brief Test the PIN operation flow of the SIM card, including setting, verifying, removing the PIN lock, and related status checks.
 *
 * This function executes the following operations in sequence:
 * 1. Set the PIN lock;
 * 2. Perform CFUN 0/1 mode switching;
 * 3. Check if the PIN lock is enabled;
 * 4. Verify the PIN code to unlock;
 * 5. Remove the PIN lock;
 * 6. Perform CFUN 0/1 mode switching again;
 * 7. Check if the PIN lock is disabled.
 *
 * @param [in] simid 
 *           - SIM card identifier, used to specify the SIM card to be tested.
 * 
 * @return QOSA_OK indicates all test steps were successful; other values indicate failure, specific error codes can be viewed through logs.
 */
static int sim_demo_test_pin_operations(qosa_uint8_t simid)
{
    int ret = 0;

    QLOGI("=== Testing PIN Operations for SIM%d ===", simid);

    // Set PIN lock
    ret = sim_demo_test_pin_operations_config_pin(simid, 1);
    if (ret != QOSA_OK)
    {
        QLOGE("lock pin failed: 0x%x", ret);
        return ret;
    }
    // CFUN 0 1 switching
    ret = sim_demo_test_cfun_01(simid);
    if (ret != QOSA_OK)
    {
        QLOGE("CFUN 0 1 switch failed: 0x%x", ret);
        return ret;
    }
    qosa_task_sleep_ms(1000);
    // Check PIN lock status (expect enabled)
    ret = sim_demo_test_pin_operations_check_pin_stat(QOSA_TRUE, simid);
    if (ret != QOSA_OK)
    {
        QLOGE("check pin status failed: 0x%x", ret);
        return ret;
    }

    // Verify PIN code to unlock
    ret = sim_demo_test_pin_operations_verify_pin(simid);
    if (ret != QOSA_OK)
    {
        QLOGE("unlock pin failed: 0x%x", ret);
        return ret;
    }

    // Remove PIN lock
    ret = sim_demo_test_pin_operations_config_pin(simid, 0);
    if (ret != QOSA_OK)
    {
        QLOGE("remove pin failed: 0x%x", ret);
        return ret;
    }

    // CFUN 0 1 switching
    ret = sim_demo_test_cfun_01(simid);
    if (ret != QOSA_OK)
    {
        QLOGE("CFUN 0 1 switch failed: 0x%x", ret);
        return ret;
    }
    qosa_task_sleep_ms(3000);
    // Check PIN lock status (expect disabled)
    ret = sim_demo_test_pin_operations_check_pin_stat(QOSA_FALSE, simid);
    if (ret != QOSA_OK)
    {
        QLOGE("check pin status failed: 0x%x", ret);
        return ret;
    }
    QLOGI("=== Testing PIN Operations for SIM%d passed ===", simid);
    return ret;
}
#endif /* TEST_PIN_OPS */

/*------------------------------------------
 *  APDU Access Test (TEST_APDU_ACCESS)
 *------------------------------------------*/
#if TEST_APDU_ACCESS

int byte2hexstr(qosa_uint8_t *bufin, int len, char *bufout)
{
    int          i = 0;
    qosa_uint8_t tmp_l = 0x0;
    qosa_uint8_t tmp_h = 0;
    if ((QOSA_NULL == bufin) || (len <= 0) || (QOSA_NULL == bufout))
    {
        return -1;
    }
    for (i = 0; i < len; i++)
    {
        tmp_h = (bufin[i] >> 4) & 0X0F;
        tmp_l = bufin[i] & 0x0F;
        bufout[2 * i] = (tmp_h > 9) ? (tmp_h - 10 + 'a') : (tmp_h + '0');
        bufout[2 * i + 1] = (tmp_l > 9) ? (tmp_l - 10 + 'a') : (tmp_l + '0');
    }
    bufout[2 * len] = '\0';

    return 0;
}

/**
 * @brief SIM card open logical channel callback function
 *
 * This function is used to handle the response results of the SIM card open logical channel command
 *
 * @param [in] ctx
 *           - AT command context pointer, containing command-related information
 * @param [in] argv
 *           - Command response parameter pointer, containing the result information of opening the logical channel
 */
static void sim_demo_sim_open_logical_channel_cb(void *ctx, void *argv)
{
    qosa_sim_open_logical_channel_cnf_t *cnf = argv;
    QOSA_UNUSED(ctx);
    QLOGI("open logical channel result=0x%x", cnf->err_code);
    qosa_msgq_release(g_sim_demo_async_ret_msg, sizeof(qosa_sim_open_logical_channel_cnf_t), (qosa_uint8_t *)cnf, 0);
}

/**
 * @brief SIM card close logical channel callback function
 *
 * This function is used to handle the response results of the SIM card close logical channel command
 *
 * @param [in] ctx
 *           - AT command context pointer, containing command-related information
 * @param [in] argv
 *           - Command response parameter pointer, containing the result information of opening the logical channel
 */
static void sim_demo_sim_close_logical_channel_cb(void *ctx, void *argv)
{
    qosa_sim_general_cnf_t *cnf = argv;
    QOSA_UNUSED(ctx);
    QLOGI("open logical channel result=0x%x", cnf->err_code);
    qosa_msgq_release(g_sim_demo_async_ret_msg, sizeof(qosa_sim_general_cnf_t), (qosa_uint8_t *)cnf, 0);
}

/**
 * @brief SIM card APDU communication callback function
 *
 * This function is used to handle the response results of the SIM card APDU communication command
 * 
 * @param [in] ctx
 *           - AT command context pointer, containing command-related information
 * @param [in] argv
 *           - SIM card generic logical channel access confirmation message pointer, containing execution results and response data
 */
static void sim_demo_logical_channel_access_cb(void *ctx, void *argv)
{
    qosa_sim_generic_logical_channel_access_cnf_t *cnf = argv;
    QOSA_UNUSED(ctx);
    QLOGI(" send apdu result=0x%x", cnf->err_code);
    qosa_msgq_release(g_sim_demo_async_ret_msg, sizeof(qosa_sim_generic_logical_channel_access_cnf_t), (qosa_uint8_t *)cnf, 0);
}

/**
 * @brief Test function to open a logical channel on the SIM card
 *
 * This function opens a logical channel on the specified SIM card asynchronously and waits for the operation result.
 * A message queue is used to receive the asynchronous callback results.
 *
 * @param [in]  simid
 *           - SIM card identifier, used to specify the SIM card to operate on
 * @param [out] channel
 *           - Returns the opened logical channel number upon success
 *
 * @return QOSA_OK indicates success; other values indicate failure, specific errors can be viewed through logs
 */
static int sim_demo_test_apdu_access_open_channel(qosa_uint8_t simid, qosa_uint8_t *channel)
{
    int                                 ret = 0;
    qosa_sim_dfname_t                   dfname = {16, {0xA0, 0x00, 0x00, 0x00, 0x87, 0x10, 0x02, 0xFF, 0x86, 0xFF, 0xFF, 0x89, 0xFF, 0xFF, 0xFF, 0xFF}};
    qosa_sim_open_logical_channel_cnf_t cnf = {0};
    do {
        // Create message queue to receive results of qosa_sim_open_logical_channel
        ret = qosa_msgq_create(&g_sim_demo_async_ret_msg, sizeof(qosa_sim_open_logical_channel_cnf_t), 4);
        if (ret != QOSA_OK)
        {
            QLOGE("Create g_sim_demo_async_ret_msg failed: %d", ret);
            break;
        }
        // Open logical channel and register callback function
        ret = qosa_sim_open_logical_channel(simid, &dfname, sim_demo_sim_open_logical_channel_cb, QOSA_NULL);
        if (QOSA_OK != ret)
        {
            QLOGE("qosa_sim_open_logical_channel error: 0x%x", ret);
            break;
        }

        // Wait for asynchronous interface to return results
        ret = qosa_msgq_wait(g_sim_demo_async_ret_msg, (qosa_uint8_t *)&cnf, sizeof(qosa_sim_open_logical_channel_cnf_t), 5000);
        if (QOSA_OK == ret)
        {
            if (QOSA_SIM_ERR_OK == cnf.err_code)
            {
                QLOGI("open channel success");
                // Return channel number
                *channel = cnf.channel_session;
                ret = QOSA_OK;
            }
            else
            {
                QLOGI("open channel error: 0x%x", cnf.err_code);
                ret = cnf.err_code;
                break;
            }
        }
        else
        {
            QLOGE("wait open channel result error: 0x%x", ret);
            break;
        }

    } while (0);

    // Clean up resources: delete the previously created message queue
    if (g_sim_demo_async_ret_msg != QOSA_NULL)
    {
        qosa_msgq_delete(g_sim_demo_async_ret_msg);
        g_sim_demo_async_ret_msg = QOSA_NULL;
    }
    return ret;
}

/**
 * @brief Close the SIM card logical channel and wait for the operation result.
 *
 * This function requests to close the logical channel of the specified SIM card asynchronously, and waits for the operation to complete via a message queue.
 * The operation result is processed by the callback function sim_demo_sim_close_logical_channel_cb and placed into the message queue.
 *
 * @param [in] simid   
 *           - SIM card identifier, used to specify the SIM card to operate on.
 * @param [in] channel 
 *           - Logical channel number to be closed.
 *
 * @return QOSA_OK indicates successful channel closure; other values indicate failure, specific errors can be viewed through logs.
 */
static int sim_demo_test_apdu_access_close_channel(qosa_uint8_t simid, qosa_uint8_t channel)
{
    int                    ret = 0;
    qosa_sim_general_cnf_t cnf = {0};
    do {
        /* Create a message queue to receive the results of the close logical channel operation */
        ret = qosa_msgq_create(&g_sim_demo_async_ret_msg, sizeof(qosa_sim_general_cnf_t), 4);
        if (ret != QOSA_OK)
        {
            QLOGE("Create g_sim_demo_async_ret_msg failed: %d", ret);
            break;
        }
        /* Initiate the close logical channel operation, using a callback function to handle the asynchronous response */
        ret = qosa_sim_close_logical_channel(simid, channel, sim_demo_sim_close_logical_channel_cb, QOSA_NULL);
        if (QOSA_OK != ret)
        {
            QLOGE("qosa_sim_close_logical_channel error: 0x%x", ret);
            break;
        }

        // Wait for asynchronous interface to return results
        ret = qosa_msgq_wait(g_sim_demo_async_ret_msg, (qosa_uint8_t *)&cnf, sizeof(qosa_sim_general_cnf_t), 5000);
        if (QOSA_OK == ret)
        {
            if (QOSA_SIM_ERR_OK == cnf.err_code)
            {
                QLOGI("close channel success");
                ret = QOSA_OK;
            }
            else
            {
                QLOGI("close channel error: 0x%x", cnf.err_code);
                break;
            }
        }
        else
        {
            QLOGE("wait close channel result error: 0x%x", ret);
            break;
        }

    } while (0);

    // Delete g_sim_demo_async_ret_msg
    if (g_sim_demo_async_ret_msg != QOSA_NULL)
    {
        qosa_msgq_delete(g_sim_demo_async_ret_msg);
        g_sim_demo_async_ret_msg = QOSA_NULL;
    }
    return ret;
}

/**
 * @brief Send APDU command to the specified logical channel of the SIM card and wait for the response result.
 *
 * This function sends an APDU command asynchronously through the underlying interface and waits for the return result via a message queue,
 * finally copying the response data to the output parameters.
 *
 * @param [in]  simid      
 *           - SIM card identifier
 * @param [in]  channel
 *           - Logical channel number
 * @param [in]  apdu_arr   
 *           - Buffer containing the APDU command data to be sent
 * @param [in]  len        
 *           - Length of the APDU command data
 * @param [out] apdu_res
 *           - Buffer to store the received APDU response data
 * @param [out] res_len
 *           - Returns the actual length of the APDU response data
 *
 * @return Operation result status code 
 *           - QOSA_OK indicates success; other values indicate failure, specific errors can be viewed through logs.
 */
static int sim_demo_test_apdu_access_send_apdu(qosa_uint8_t simid, qosa_uint8_t channel, qosa_uint8_t *apdu_arr, int len, qosa_uint8_t *apdu_res, int *res_len)
{
    int                                           ret = 0;
    qosa_sim_generic_logical_channel_access_cnf_t cnf = {0};
    do {
        // Create message queue to receive asynchronous results from qosa_sim_generic_logical_channel_access
        ret = qosa_msgq_create(&g_sim_demo_async_ret_msg, sizeof(qosa_sim_generic_logical_channel_access_cnf_t), 4);
        if (ret != QOSA_OK)
        {
            QLOGE("Create g_sim_demo_async_ret_msg failed: %d", ret);
            break;
        }
        // Send APDU and register callback handler
        ret = qosa_sim_generic_logical_channel_access(simid, channel, apdu_arr, len, sim_demo_logical_channel_access_cb, QOSA_NULL);
        if (QOSA_OK != ret)
        {
            QLOGE("qosa_sim_generic_logical_channel_access error: 0x%x", ret);
            break;
        }

        // Wait for asynchronous interface to return results
        ret = qosa_msgq_wait(g_sim_demo_async_ret_msg, (qosa_uint8_t *)&cnf, sizeof(qosa_sim_generic_logical_channel_access_cnf_t), 5000);
        if (QOSA_OK == ret)
        {
            if (QOSA_SIM_ERR_OK == cnf.err_code)
            {
                QLOGI("apdu send success");

                // Validate if the returned data length is valid
                if (cnf.data_len > QOSA_SIM_R_APDU_LEN_MAX || cnf.data_len < QOSA_SIM_R_APDU_LEN_MIN)
                {
                    QLOGI("apdu resp len[%d] error", cnf.data_len);
                    ret = QOSA_ERROR_GENERAL;
                    break;
                }
                // Copy response data to output parameter
                qosa_memcpy(apdu_res, cnf.data, cnf.data_len);
                *res_len = cnf.data_len;
                ret = QOSA_OK;
            }
            else
            {
                QLOGI("apdu send error: 0x%x", cnf.err_code);
                break;
            }
        }
        else
        {
            QLOGE("wait apdu send result error: 0x%x", ret);
            break;
        }

    } while (0);

    // Clean up resources: delete the created message queue
    if (g_sim_demo_async_ret_msg != QOSA_NULL)
    {
        qosa_msgq_delete(g_sim_demo_async_ret_msg);
        g_sim_demo_async_ret_msg = QOSA_NULL;
    }
    return ret;
}

/**
 * @brief Test function to access SIM card and read IMSI information through APDU commands.
 *
 * This function simulates a complete APDU interaction process, sequentially selecting MF, DF_GSM, and IMS files,
 * and finally reading binary data to obtain IMSI content. After the operation is completed, the logical channel is closed.
 *
 * @param [in] simid
 *           - SIM card identifier, used to specify the target SIM card for operation.
 * 
 * @return Operation result status code, QOSA_OK indicates success, other values indicate failure.
 */
static int sim_demo_test_apdu_access(qosa_uint8_t simid)
{
    // Example: Read IMSI through APDU
    qosa_uint8_t channel = 0;
    int          ret = 0;
    // Define a series of APDU commands, respectively used to select IMSI file and read binary content
    qosa_uint8_t select_imsi_cmd[] = {0x01, 0xA4, 0x08, 0x04, 0x04, 0x7F, 0x20, 0x6F, 0x07};
    qosa_uint8_t read_binary_cmd[] = {0x01, 0xB0, 0x00, 0x00, 0x00};

    int res_len = 0;
    // Store APDU response data and length
    qosa_uint8_t *apdu_res = (qosa_uint8_t *)qosa_calloc(QOSA_SIM_R_APDU_LEN_MAX, sizeof(qosa_uint8_t));
    // Convert response data to hexadecimal string format for log output
    char *data_response = (char *)qosa_calloc(QOSA_SIM_R_APDU_LEN_MAX * 2 + 1, sizeof(char));
    if (apdu_res == QOSA_NULL || data_response == QOSA_NULL)
    {
        QLOGE("calloc failed for apdu_res/data_response");
        if (apdu_res != QOSA_NULL)
        {
            qosa_free(apdu_res);
        }
        if (data_response != QOSA_NULL)
        {
            qosa_free(data_response);
        }
        return QOSA_ERROR_NO_MEMORY;
    }

    QLOGI("=== Testing APDU access for SIM%d ===", simid);

    do {
        // Open channel
        ret = sim_demo_test_apdu_access_open_channel(simid, &channel);
        if (ret != QOSA_OK)
        {
            QLOGE("Create g_sim_demo_async_ret_msg failed: %d", ret);
            break;
        }

        // Send SELECT IMSI command to select IMSI EF file
        ret = sim_demo_test_apdu_access_send_apdu(simid, channel, select_imsi_cmd, sizeof(select_imsi_cmd), apdu_res, &res_len);
        if (QOSA_OK != ret)
        {
            QLOGE("select imsi error: 0x%x", ret);
            break;
        }

        // Send READ BINARY command to read the actual data content of IMSI
        ret = sim_demo_test_apdu_access_send_apdu(simid, channel, read_binary_cmd, sizeof(read_binary_cmd), apdu_res, &res_len);
        if (QOSA_OK != ret)
        {
            QLOGE("read binary error: 0x%x", ret);
            break;
        }

        // Convert the read data to HEX string and print log
        byte2hexstr(apdu_res, res_len, data_response);
        QLOGI("IMSI resp: %s", data_response);

        // Close the previously opened logical channel
        ret = sim_demo_test_apdu_access_close_channel(simid, channel);
        if (QOSA_OK != ret)
        {
            QLOGE("close channel error: 0x%x", ret);
            break;
        }
        QLOGI("=== Testing APDU access for SIM%d passed===", simid);
        ret = QOSA_OK;
    } while (0);

    if (apdu_res != QOSA_NULL)
    {
        qosa_free(apdu_res);
    }
    if (data_response != QOSA_NULL)
    {
        qosa_free(data_response);
    }
    return ret;
}
#endif /* TEST_APDU_ACCESS */

/*------------------------------------------
 * Hot Swap Test (TEST_HOT_SWAP)
 *------------------------------------------*/
#if TEST_HOT_SWAP

/**
 * @brief Test SIM card hot swap function configuration and basic status checking.
 *
 * This function is used to simulate and test the hot swap process of the SIM card. It will create two semaphores:
 * one for detecting SIM card insertion/removal events, and another for waiting CPIN readiness.
 * The function will enable hot swap detection for the specified SIM card and prompt the user to remove and insert the SIM card,
 * after completing the insertion/removal operation, wait for CPIN to be ready, and finally obtain SIM card basic information to confirm the status.
 *
 * @param [in] simid 
 *           - Specify the SIM card ID to be tested (uint8_t type)
 * 
 * @return Return execution result, QOSA_OK indicates success, other values indicate failure
 */
static int sim_demo_test_hot_swap_config(qosa_uint8_t simid)
{
    int ret = 0;
    // Create dedicated semaphore for SIM hot swap event notification

    QLOGI("=== Testing hot swap for SIM%d ===", simid);
    ret = qosa_sem_create(&g_sim_demo_hot_swap_sem, 0);
    if (ret != QOSA_OK)
    {
        QLOGE("Create g_sim_demo_sem failed: %d", ret);
        return ret;
    }

    // Create semaphore for waiting CPIN Ready status
    ret = qosa_sem_create(&g_sim_demo_cpin_ready_sem, 0);
    if (ret != QOSA_OK)
    {
        QLOGE("Create cpin ready semaphore failed: 0x%x", ret);
        return ret;
    }

    QLOGW("Please remove the SIM card.");

    // Wait for SIM card removal event trigger
    qosa_sem_wait(g_sim_demo_hot_swap_sem, QOSA_WAIT_FOREVER);

    QLOGW("Please insert the SIM card.");

    // Wait for SIM card insertion event trigger
    qosa_sem_wait(g_sim_demo_hot_swap_sem, QOSA_WAIT_FOREVER);

    // Wait for CPIN status to become Ready
    qosa_sem_wait(g_sim_demo_cpin_ready_sem, QOSA_WAIT_FOREVER);

    // Clean up CPIN Ready semaphore resources
    qosa_sem_delete(g_sim_demo_cpin_ready_sem);
    g_sim_demo_cpin_ready_sem = QOSA_NULL;

    // Clean up hot swap dedicated semaphore resources
    qosa_sem_delete(g_sim_demo_hot_swap_sem);
    g_sim_demo_hot_swap_sem = QOSA_NULL;

    // Get basic information and status of the SIM card to verify normal recognition
    QLOGW("SIM hot swap test done. getting basic info...");
    ret = sim_demo_test_basic_info_sim_status_check(simid);
    return ret;
}
#endif /* TEST_HOT_SWAP */

/*------------------------------------------
 * Dual SIM Test (TEST_DUAL_SIM)
 *------------------------------------------*/
#if TEST_DUAL_SIM
/**
 * @brief Switch SIM card slots on dual SIM devices and complete related initialization operations.
 *
 * This function first sets the specified SIM card module to CFUN=0 (minimum functional mode), then switches to the target card slot,
 * waits for some time for the module to recognize the new SIM card, then restores to CFUN=1 (full functional mode) and waits for the CPIN ready signal.
 *
 * @param [in] simid 
 *           - Specify the SIM card ID to be tested (uint8_t type)
 * 
 * @param [in] new_slot 
 *           - Target card slot ID
 * 
 * @return Operation result, QOSA_SIM_ERR_OK indicates success, other values indicate failure
 */
static int sim_demo_test_dual_sim_switch_set_slot_id(qosa_uint8_t simid, qosa_uint8_t new_slot)
{
    int ret = 0;

    // Set SIM card module to minimum functional mode (CFUN=0)
    QLOGI("[CFUN]: 0");
    ret = qosa_dev_set_cfun(simid, 0, sim_demo_set_cfun_cb, QOSA_NULL);
    QLOGI("ret=%d,qosa_dev_set_cfun", ret);
    // Wait for CFUN=0 to complete
    qosa_sem_wait(g_sim_demo_sem, QOSA_WAIT_FOREVER);

    // Switch SIM card slot
    ret = qosa_sim_set_slot_id(0, new_slot);
    if (ret == QOSA_SIM_ERR_OK)
    {
        QLOGI("Switched to SIM slot: %d", new_slot);
        qosa_task_sleep_ms(1000);
    }

    // Create cpin ready semaphore for waiting CPIN ready
    qosa_sem_create(&g_sim_demo_cpin_ready_sem, 0);
    if (ret != QOSA_OK)
    {
        QLOGE("Create cpin ready semaphore failed: 0x%x", ret);
        return ret;
    }

    // Set SIM card module to full functional mode (CFUN=1)
    QLOGI("[CFUN]: 1");
    ret = qosa_dev_set_cfun(simid, 1, sim_demo_set_cfun_cb, QOSA_NULL);
    QLOGI("ret=%d,qosa_dev_set_cfun", ret);

    // Wait for CFUN=1 to complete
    qosa_sem_wait(g_sim_demo_sem, QOSA_WAIT_FOREVER);

    // Wait for cpin ready signal
    qosa_sem_wait(g_sim_demo_cpin_ready_sem, QOSA_WAIT_FOREVER);

    // Clean up CPIN Ready semaphore resources
    qosa_sem_delete(g_sim_demo_cpin_ready_sem);
    g_sim_demo_cpin_ready_sem = QOSA_NULL;

    return ret;
}

/**
 * @brief Dual SIM switching test function
 * 
 * This function is used to test the card slot switching function of dual SIM devices. The function will get the currently used card slot,
 * switch to another card slot, and verify the basic information of the new card slot.
 * 
 * @param [in] simid 
 *           - Specify the SIM card ID to be tested (uint8_t type)
 * 
 * @return QOSA_OK - Test successful
 * @return Other values - Test failed, return specific error code
 */
static int sim_demo_test_dual_sim_switch(qosa_uint8_t simid)
{
    qosa_uint8_t current_slot = 0;
    int          ret = 0;

    QLOGI("=== Testing dual sim ===");
    // Get the currently used card slot
    ret = qosa_sim_get_slot_id(0, &current_slot);
    if (ret != QOSA_OK)
    {
        QLOGE("Get SIM slot failed: 0x%x", ret);
        return ret;
    }
    QLOGI("Current SIM slot: %d", current_slot);

    // Switch card slot (0/1)
    qosa_uint8_t new_slot = (current_slot == 0) ? 1 : 0;
    // Read card slot status
    ret = qosa_sim_read_slot_stat(new_slot);
    if (ret != QOSA_SIM_INSERT_STAT_INSERTED)
    {
        QLOGE("SIM slot %d is not inserted.", new_slot);
        return ret;
    }

    // Execute card slot switching operation
    ret = sim_demo_test_dual_sim_switch_set_slot_id(simid, new_slot);
    if (ret != QOSA_OK)
    {
        QLOGE("Switch SIM slot failed: 0x%x", ret);
        return ret;
    }
    QLOGI("Switched to SIM slot: %d, getting basic info... ", new_slot);

    // Verify basic information of the new card slot
    ret = sim_demo_test_basic_info_sim_status_check(simid);
    if (ret != QOSA_OK)
    {
        QLOGE("Get SIM info failed: 0x%x", ret);
        return ret;
    }
    QLOGI("=== Testing dual sim passed ===");
    return ret;
}
#endif /* TEST_DUAL_SIM */

/**
 * @brief Initialize SIM card demo event callback function
 * 
 * This function is used to register event callback handlers related to SIM card status,
 * including SIM card status change events and SIM card insertion/removal status events.
 * 
 * @param None
 * 
 * @return None
 */
void sim_demo_event_cb_init(void)
{
    // 1. Register callback handlers for SIM card related events
    qosa_event_notify_register(QOSA_EVENT_MODEM_SIM_STATUS, sim_demo_sim_status_event_cb, QOSA_NULL);
#if TEST_HOT_SWAP
    qosa_event_notify_register(QOSA_EVENT_MODEM_SIM_INSERT_STATUS, sim_demo_sim_insert_status_event_cb, QOSA_NULL);
#endif /* TEST_HOT_SWAP */

    QLOGI("SIM Demo event Initialized.");
}

/**
 * @brief SIM card demo function startup function
 * 
 * This function is used to start the demonstration test of SIM card related functions, including basic information acquisition, PIN code operations,
 * APDU access, hot swap detection, and dual SIM switching function tests.
 * 
 * @param No parameters
 * 
 * @return No return value
 * 
 * @note The function will decide which test items to execute based on precompiled macro definitions
 */
void sim_demo_run_test_suite(void)
{
    // Wait for some time to ensure SIM card initialization is complete
    qosa_task_sleep_ms(10000);

    QLOGI("=== SIM Demo beginning ===");

#if TEST_BASIC_INFO
    /* Test SIM card basic information acquisition function */
    sim_demo_test_basic_info(0);
#endif

#if TEST_PIN_OPS
    /* Test SIM card PIN code related operation function */
    qosa_task_sleep_ms(3000);
    sim_demo_test_pin_operations(0);
#endif

#if TEST_APDU_ACCESS
    /* Test SIM card APDU instruction access function */
    qosa_task_sleep_ms(3000);
    sim_demo_test_apdu_access(0);
#endif

#if TEST_HOT_SWAP
    /* Test SIM card hot swap configuration function */
    qosa_task_sleep_ms(3000);
    sim_demo_test_hot_swap_config(0);
#endif

#if TEST_DUAL_SIM
    /* Test dual SIM switching function */
    qosa_task_sleep_ms(3000);
    sim_demo_test_dual_sim_switch(0);
#endif

    QLOGI("=== SIM Demo Completed ===");
}

/**
 * @brief Initialize SIM demo semaphore
 * 
 * This function is used to create the general semaphore for demonstration, preparing for subsequent semaphore operations
 * 
 * @param No parameters
 * 
 * @return int Return operation result
 *         - QOSA_OK: Semaphore created successfully
 *         - Other values: Semaphore creation failed, see return value for specific error code
 */
int sim_demo_sig_init(void)
{
    int ret = 0;

    // Create demo general semaphore
    ret = qosa_sem_create(&g_sim_demo_sem, 0);
    if (ret != QOSA_OK)
    {
        QLOGE("Create g_sim_demo_sem failed: %d", ret);
    }
    return ret;
}

/**
 * @brief SIM card demo task main function
 * 
 * This function is responsible for initializing SIM card demo related event callbacks and signal handling,
 * then enters the main loop to continuously execute the demo logic.
 * 
 * @param [in] arg Task parameter pointer, unused
 * 
 * @return Task execution result, always returns 0
 */
void sim_demo_task(void *arg)
{
    int ret = 0;
    // Initialize event callbacks
    sim_demo_event_cb_init();

    // Initialize signal handling
    ret = sim_demo_sig_init();
    if (ret != QOSA_OK)
    {
        QLOGE("sim_demo_sig_init failed: %d", ret);
    }

    // If testing hot swap function, need to enable hot swap and then restart the module
#if TEST_HOT_SWAP
    if (sim_demo_test_hot_swap_enabled_check(0) == QOSA_FALSE)
    {
        // Enable hot swap
        sim_demo_test_hot_swap_enable(0);
        qosa_task_sleep_ms(3000);
        // Restart the module
        QLOGE("power reset");
        qosa_power_reset(QOSA_RESET_NORMAL);
        while (1)
        {
            qosa_task_sleep_ms(3000);
            QLOGE("wait power reset");
        }
    }
#endif /* TEST_HOT_SWAP */
    // Main loop processing...
    while (1)
    {
        sim_demo_run_test_suite();
    }
}

/**
 * @brief sim example demo, create demonstration task
 *
 * This function is responsible for creating the sim demonstration task. After creation, After the task is created, 
 * it will automatically run. It will automatically check the SIM card status, test the PIN code functionality, 
 * SIM card access, SIM card hot-swapping, and dual SIM single standby functionality, in order to demonstrate the SIM card features.
 *
 * @note Task stack size is 4KB, priority is normal priority
 */
void unir_sim_demo_init(void)
{
    int err = 0;

    // Create WiFi scan demonstration task
    err = qosa_task_create(&g_sim_demo_task, 4 * 1024, QOSA_PRIORITY_NORMAL, "sim_demo", sim_demo_task, QOSA_NULL);
    // Check if task creation was successful
    if (err != QOSA_OK)
    {
        QLOGE("sim_demo_init task create error");
        return;
    }
}

UNIRTOS_APP_EXPORT(329, "sim_demo", unir_sim_demo_init);
/**
 * @file avcServer.c
 *
 * AirVantage Controller Daemon
 *
 * <hr>
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */

#include "legato.h"
#include "interfaces.h"
#include "pa_avc.h"
#include "avcServer.h"
#include "assetData.h"
#include "lwm2m.h"
#include "avData.h"

#include "le_print.h"

//--------------------------------------------------------------------------------------------------
// Definitions
//--------------------------------------------------------------------------------------------------

#define AVC_SERVICE_CFG "/apps/avcService"

//--------------------------------------------------------------------------------------------------
/**
 * This ref is returned when a status handler is added/registered.  It is used when the handler is
 * removed.  Only one ref is needed, because only one handler can be registered at a time.
 */
//--------------------------------------------------------------------------------------------------
#define REGISTERED_HANDLER_REF ((le_avc_StatusEventHandlerRef_t)0x1234)

//--------------------------------------------------------------------------------------------------
/**
 * This is the default defer time (in minutes) if an install is blocked by a user app.  Should
 * probably be a prime number.
 *
 * Use small number to ensure deferred installs happen quickly, once no longer deferred.
 */
//--------------------------------------------------------------------------------------------------
#define BLOCKED_DEFER_TIME 3

//--------------------------------------------------------------------------------------------------
/**
 * Current internal state.
 *
 * Used mainly to ensure that API functions don't do anything if in the wrong state.
 *
 * TODO: May need to revisit some of the state transitions here.
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    AVC_IDLE,                    ///< No updates pending or in progress
    AVC_DOWNLOAD_PENDING,        ///< Received pending download; no response sent yet
    AVC_DOWNLOAD_IN_PROGRESS,    ///< Accepted download, and in progress
    AVC_INSTALL_PENDING,         ///< Received pending install; no response sent yet
    AVC_INSTALL_IN_PROGRESS,     ///< Accepted install, and in progress
    AVC_UNINSTALL_PENDING,         ///< Received pending uninstall; no response sent yet
    AVC_UNINSTALL_IN_PROGRESS      ///< Accepted uninstall, and in progress
}
AvcState_t;


//--------------------------------------------------------------------------------------------------
// Data structures
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
/**
 * The current state of any update.
 *
 * Although this variable is accessed both in API functions and in UpdateHandler(), access locks
 * are not needed.  This is because this is running as a daemon, and so everything runs in the
 * main thread.
 */
//--------------------------------------------------------------------------------------------------
static AvcState_t CurrentState = AVC_IDLE;

//--------------------------------------------------------------------------------------------------
/**
 * The type of the current update.  Only valid if CurrentState is not AVC_IDLE
 */
//--------------------------------------------------------------------------------------------------
static le_avc_UpdateType_t CurrentUpdateType = LE_AVC_UNKNOWN_UPDATE;

//--------------------------------------------------------------------------------------------------
/**
 * Handler registered by control app to receive status updates.  Only one is allowed.
 */
//--------------------------------------------------------------------------------------------------
static le_avc_StatusHandlerFunc_t StatusHandlerRef = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Is there a control app installed?  If so, we don't want to take automatic actions, even if
 * the control app has not yet registered a handler.  This flag is updated at COMPONENT_INIT,
 * and also when the control app explicitly registers.
 *
 * One case that is not currently handled is if the control app is uninstalled.  Thus, once this
 * flag is set to true, it will never be set to false. This is not expected to be a problem, but
 * if it becomes an issue, we could register for app installs and uninstalls.
 */
//--------------------------------------------------------------------------------------------------
static bool IsControlAppInstalled = false;

//--------------------------------------------------------------------------------------------------
/**
 * Context pointer associated with the above user registered handler to receive status updates.
 */
//--------------------------------------------------------------------------------------------------
static void* StatusHandlerContextPtr = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Reference for the registered control app.  Only one is allowed
 */
//--------------------------------------------------------------------------------------------------
static le_msg_SessionRef_t RegisteredControlAppRef = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Safe Reference Map for the block/unblock references
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t BlockRefMap;

//--------------------------------------------------------------------------------------------------
/**
 * Count of the number of allocated safe references from 'BlockRefMap' above.
 *
 * If there was a safeRef wrapper around le_hashmap_Size(), then this value would probably not be
 * needed, although we would then be dependent on the implementation of le_hashmap_Size() not
 * being overly complex.
 */
//--------------------------------------------------------------------------------------------------
static uint32_t BlockRefCount=0;

//--------------------------------------------------------------------------------------------------
/**
 * Handler registered from avcServer_QueryInstall() to receive notification when app install is
 * allowed. Only one registered handler is allowed, and will be set to NULL after being called.
 */
//--------------------------------------------------------------------------------------------------
static avcServer_InstallHandlerFunc_t QueryInstallHandlerRef = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Handler registered from avcServer_QueryUninstall() to receive notification when app uninstall is
 * allowed. Only one registered handler is allowed, and will be set to NULL after being called.
 */
//--------------------------------------------------------------------------------------------------
static avcServer_UninstallHandlerFunc_t QueryUninstallHandlerRef = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Timer used for deferring app install.
 */
//--------------------------------------------------------------------------------------------------
static le_timer_Ref_t InstallDeferTimer;

//--------------------------------------------------------------------------------------------------
/**
 * Timer used for deferring app uninstall.
 */
//--------------------------------------------------------------------------------------------------
static le_timer_Ref_t UninstallDeferTimer;

//--------------------------------------------------------------------------------------------------
/**
 * Error occurred during update via airvantage.
 */
//--------------------------------------------------------------------------------------------------
static le_avc_ErrorCode_t AvcErrorCode = LE_AVC_ERR_NONE;


//--------------------------------------------------------------------------------------------------
// Local functions
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
/**
 * Check to see if le_avc is bound to a client.
 */
//--------------------------------------------------------------------------------------------------
static bool IsAvcBound
(
    void
)
{
    le_cfg_IteratorRef_t iterRef = le_cfg_CreateReadTxn("system:/apps");

    // If there are no apps, then there's no bindings.
    if (le_cfg_GoToFirstChild(iterRef) != LE_OK)
    {
        le_cfg_CancelTxn(iterRef);
        return false;
    }

    // Loop through all installed applications.
    do
    {
        // Check out all of the bindings for this application.
        le_cfg_GoToNode(iterRef, "./bindings");

        if (le_cfg_GoToFirstChild(iterRef) == LE_OK)
        {
            do
            {
                // Check to see if this binding is for the <root>.le_avc service.
                char strBuffer[LE_CFG_STR_LEN_BYTES] = "";

                le_cfg_GetString(iterRef, "./interface", strBuffer, sizeof(strBuffer), "");
                if (strcmp(strBuffer, "le_avc") == 0)
                {
                    // The app can be bound to the AVC app directly, or through the root user.
                    // so check for both.
                    le_cfg_GetString(iterRef, "./app", strBuffer, sizeof(strBuffer), "");
                    if (strcmp(strBuffer, "avcService") == 0)
                    {
                        // Success.
                        le_cfg_CancelTxn(iterRef);
                        return true;
                    }

                    le_cfg_GetString(iterRef, "./user", strBuffer, sizeof(strBuffer), "");
                    if (strcmp(strBuffer, "root") == 0)
                    {
                        // Success.
                        le_cfg_CancelTxn(iterRef);
                        return true;
                    }
                }
            }
            while (le_cfg_GoToNextSibling(iterRef) == LE_OK);

            le_cfg_GoToParent(iterRef);
        }

        le_cfg_GoToParent(iterRef);
    }
    while (le_cfg_GoToNextSibling(iterRef) == LE_OK);

    // The binding was not found.
    le_cfg_CancelTxn(iterRef);
    return false;
}


//--------------------------------------------------------------------------------------------------
/**
 * Handler to receive update status notifications from PA
 */
//--------------------------------------------------------------------------------------------------
static void UpdateHandler
(
    le_avc_Status_t updateStatus,
    le_avc_UpdateType_t updateType,
    int32_t totalNumBytes,
    int32_t dloadProgress,
    le_avc_ErrorCode_t errorCode
)
{
    le_avc_Status_t reportStatus = LE_AVC_NO_UPDATE;

    // Keep track of the state of any pending downloads or installs.
    switch ( updateStatus )
    {
        case LE_AVC_DOWNLOAD_PENDING:
            CurrentState = AVC_DOWNLOAD_PENDING;

            // Only store the new update type, when we get download pending.
            LE_DEBUG("Update type for DOWNLOAD is %d", updateType);
            CurrentUpdateType = updateType;
            break;

        case LE_AVC_INSTALL_PENDING:
            CurrentState = AVC_INSTALL_PENDING;

            // If the device resets during a FOTA download, then the CurrentUpdateType is lost
            // and needs to be assigned again.  Since we don't easily know if a reset happened,
            // always re-assign the value.
            LE_DEBUG("Update type for INSTALL is %d", updateType);
            CurrentUpdateType = updateType;
            break;

        case LE_AVC_DOWNLOAD_IN_PROGRESS:
        case LE_AVC_DOWNLOAD_COMPLETE:
            LE_DEBUG("Update type for DOWNLOAD is %d", updateType);
            CurrentUpdateType = updateType;
            break;

        case LE_AVC_UNINSTALL_PENDING:
        case LE_AVC_UNINSTALL_IN_PROGRESS:
        case LE_AVC_UNINSTALL_FAILED:
        case LE_AVC_UNINSTALL_COMPLETE:
            LE_ERROR("Received unexpected update status.");
            break;

        case LE_AVC_INSTALL_IN_PROGRESS:
            // These events do not cause a state transition
            break;

        case LE_AVC_NO_UPDATE:
        case LE_AVC_INSTALL_COMPLETE:
            // There is no longer any current update, so go back to idle
            CurrentState = AVC_IDLE;
            break;

        case LE_AVC_DOWNLOAD_FAILED:
        case LE_AVC_INSTALL_FAILED:
            // There is no longer any current update, so go back to idle
            AvcErrorCode = errorCode;
            CurrentState = AVC_IDLE;
            break;

        case LE_AVC_SESSION_STARTED:
            // This can be safely ignored
            break;

        case LE_AVC_SESSION_STOPPED:
            // Retain CurrentState when session stops.
            break;
    }


    if ( StatusHandlerRef != NULL )
    {
        LE_DEBUG("Reporting status %d", updateStatus);
        LE_DEBUG("Total number of Bytes to download = %d", totalNumBytes);
        LE_DEBUG("Download progress = %d%%", dloadProgress);

        // Notify registered control app
        StatusHandlerRef( updateStatus,
                          totalNumBytes,
                          dloadProgress,
                          StatusHandlerContextPtr);

        // After a session is successfully started, if we are in one of the pending states,
        // notify the user for acceptance.
        if ( updateStatus == LE_AVC_SESSION_STARTED )
        {
            // The currentState is really the previous state in case of session start, as we don't
            // do a state change.
            switch ( CurrentState )
            {
                case AVC_DOWNLOAD_PENDING:
                    reportStatus = LE_AVC_DOWNLOAD_PENDING;
                    break;
                case AVC_INSTALL_PENDING:
                    reportStatus = LE_AVC_INSTALL_PENDING;
                    break;
                case AVC_UNINSTALL_PENDING:
                    reportStatus = LE_AVC_UNINSTALL_PENDING;
                    break;
                default:
                    break;
            }

            // Notify pending state to registered control app for user acceptance.
            if ( reportStatus != LE_AVC_NO_UPDATE )
            {
                LE_DEBUG("Reporting status  %d,", reportStatus);
                StatusHandlerRef(reportStatus,
                                 -1,
                                 -1,
                                 StatusHandlerContextPtr);
            }
        }
    }
    else if ( IsControlAppInstalled )
    {
        // There is a control app installed, but the handler is not yet registered.  Defer
        // the decision to allow the control app time to register.
        if ( ( updateStatus == LE_AVC_DOWNLOAD_PENDING )
             || ( updateStatus == LE_AVC_INSTALL_PENDING ) )
        {
            LE_INFO("Automatically deferring %i, while waiting for control app to register",
                    updateStatus);
            pa_avc_SendSelection(PA_AVC_DEFER, BLOCKED_DEFER_TIME);

            // Since the decision is defer at this time, go back to idle
            CurrentState = AVC_IDLE;
        }
        else
        {
            LE_DEBUG("No handler registered to receive status %i", updateStatus);
        }
    }

    else
    {
        // There is no control app; automatically accept any pending downloads.
        if ( updateStatus == LE_AVC_DOWNLOAD_PENDING )
        {
            LE_INFO("Automatically accepting download");
            pa_avc_SendSelection(PA_AVC_ACCEPT, 0);
            CurrentState = AVC_DOWNLOAD_IN_PROGRESS;
        }

        // There is no control app; automatically accept any pending installs,
        // if there are no blocking apps, otherwise, defer the install.
        else if ( updateStatus == LE_AVC_INSTALL_PENDING )
        {
            if ( BlockRefCount == 0 )
            {
                LE_INFO("Automatically accepting install");
                pa_avc_SendSelection(PA_AVC_ACCEPT, 0);
                CurrentState = AVC_INSTALL_IN_PROGRESS;
            }
            else
            {
                LE_INFO("Automatically deferring install");
                pa_avc_SendSelection(PA_AVC_DEFER, BLOCKED_DEFER_TIME);

                // Since the decision is not to install at this time, go back to idle
                CurrentState = AVC_IDLE;
            }
        }

        // Otherwise, log a message
        else
        {
            LE_DEBUG("No handler registered to receive status %i", updateStatus);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Handler for client session closes for clients that use the block/unblock API.
 *
 * Note: if the registered control app has closed then the associated data is cleaned up by
 * le_avc_RemoveStatusEventHandler(), since the remove handler is automatically called.
 */
//--------------------------------------------------------------------------------------------------
static void ClientCloseSessionHandler
(
    le_msg_SessionRef_t sessionRef,
    void*               contextPtr
)
{
    if ( sessionRef == NULL )
    {
        LE_ERROR("sessionRef is NULL");
        return;
    }

    LE_INFO("Client %p closed, remove allocated resources", sessionRef);

    // Search for the block reference(s) used by the closed client, and clean up any data.
    le_ref_IterRef_t iterRef = le_ref_GetIterator(BlockRefMap);

    while ( le_ref_NextNode(iterRef) == LE_OK )
    {
        if ( le_ref_GetValue(iterRef) == sessionRef )
        {
            le_ref_DeleteRef( BlockRefMap, (void*)le_ref_GetSafeRef(iterRef) );
            BlockRefCount--;
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Determine whether the current client is the registered control app client.
 *
 * As a side-effect, will kill the client if it is not the registered control app client.
 */
//--------------------------------------------------------------------------------------------------
static bool IsValidControlAppClient
(
    void
)
{
    if ( RegisteredControlAppRef == NULL ||
         RegisteredControlAppRef != le_avc_GetClientSessionRef() )
    {
        LE_KILL_CLIENT("Client is not registered as control app");
        return false;
    }
    else
        return true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Query if it's okay to proceed with an application install
 *
 * @return
 *      - LE_OK if install can proceed right away
 *      - LE_BUSY if install is deferred
 */
//--------------------------------------------------------------------------------------------------
static le_result_t QueryInstall
(
    void
)
{
    le_result_t result = LE_BUSY;

    if ( StatusHandlerRef != NULL )
    {
        // Notify registered control app.
        LE_DEBUG("Reporting status LE_AVC_INSTALL_PENDING");
        CurrentState = AVC_INSTALL_PENDING;
        StatusHandlerRef(LE_AVC_INSTALL_PENDING,
                         -1,
                         -1,
                         StatusHandlerContextPtr);
    }

    else if ( IsControlAppInstalled )
    {
        // There is a control app installed, but the handler is not yet registered.  Defer
        // the decision to allow the control app time to register.
        LE_INFO("Automatically deferring install, while waiting for control app to register");

        // Since the decision is not to install at this time, go back to idle
        CurrentState = AVC_IDLE;

        // Try the install later
        le_clk_Time_t interval = { .sec = BLOCKED_DEFER_TIME*60 };

        le_timer_SetInterval(InstallDeferTimer, interval);
        le_timer_Start(InstallDeferTimer);
    }

    else
    {
        // There is no control app; automatically accept any pending installs,
        // if there are no blocking apps, otherwise, defer the install.
        if ( BlockRefCount == 0 )
        {
            LE_INFO("Automatically accepting install");
            CurrentState = AVC_INSTALL_IN_PROGRESS;
            result = LE_OK;
        }
        else
        {
            LE_INFO("Automatically deferring install");

            // Since the decision is not to install at this time, go back to idle
            CurrentState = AVC_IDLE;

            // Try the install later
            le_clk_Time_t interval = { .sec = BLOCKED_DEFER_TIME*60 };

            le_timer_SetInterval(InstallDeferTimer, interval);
            le_timer_Start(InstallDeferTimer);
        }
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Query if it's okay to proceed with an application uninstall
 *
 * @return
 *      - LE_OK if uninstall can proceed right away
 *      - LE_BUSY if uninstall is deferred
 */
//--------------------------------------------------------------------------------------------------
static le_result_t QueryUninstall
(
    void
)
{
    le_result_t result = LE_BUSY;

    if ( StatusHandlerRef != NULL )
    {
        // Notify registered control app.
        LE_DEBUG("Reporting status LE_AVC_UNINSTALL_PENDING");
        CurrentState = AVC_UNINSTALL_PENDING;
        StatusHandlerRef( LE_AVC_UNINSTALL_PENDING,
                          0,
                          0,
                          StatusHandlerContextPtr);
    }

    else if ( IsControlAppInstalled )
    {
        // There is a control app installed, but the handler is not yet registered.  Defer
        // the decision to allow the control app time to register.
        LE_INFO("Automatically deferring uninstall, while waiting for control app to register");

        // Since the decision is not to uninstall at this time, go back to idle
        CurrentState = AVC_IDLE;

        // Try the uninstall later
        le_clk_Time_t interval = { .sec = BLOCKED_DEFER_TIME*60 };

        le_timer_SetInterval(UninstallDeferTimer, interval);
        le_timer_Start(UninstallDeferTimer);
    }

    else
    {
        // There is no control app; automatically accept any pending uninstalls,
        // if there are no blocking apps, otherwise, defer the uninstall.
        if ( BlockRefCount == 0 )
        {
            LE_INFO("Automatically accepting uninstall");
            CurrentState = AVC_UNINSTALL_IN_PROGRESS;
            result = LE_OK;
        }
        else
        {
            LE_INFO("Automatically deferring uninstall");

            // Since the decision is not to uninstall at this time, go back to idle
            CurrentState = AVC_IDLE;

            // Try the uninstall later
            le_clk_Time_t interval = { .sec = BLOCKED_DEFER_TIME*60 };

            le_timer_SetInterval(UninstallDeferTimer, interval);
            le_timer_Start(UninstallDeferTimer);
        }
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Called when the install defer timer expires.
 */
//--------------------------------------------------------------------------------------------------
void InstallTimerExpiryHandler
(
    le_timer_Ref_t timerRef    ///< Timer that expired
)
{
    if ( QueryInstall() == LE_OK )
    {
        // Notify the registered handler to proceed with the install; only called once.
        QueryInstallHandlerRef();
        QueryInstallHandlerRef = NULL;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Called when the uninstall defer timer expires.
 */
//--------------------------------------------------------------------------------------------------
void UninstallTimerExpiryHandler
(
        le_timer_Ref_t timerRef    ///< Timer that expired
)
{
    if ( QueryUninstall() == LE_OK )
    {
        // Notify the registered handler to proceed with the uninstall; only called once.
        QueryUninstallHandlerRef();
        QueryUninstallHandlerRef = NULL;
    }
}



//--------------------------------------------------------------------------------------------------
// Internal interface functions
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
/**
 * Query the AVC Server if it's okay to proceed with an application install
 *
 * If an install can't proceed right away, then the handlerRef function will be called when it is
 * okay to proceed with an install. Note that handlerRef will be called at most once.
 *
 * @return
 *      - LE_OK if install can proceed right away (handlerRef will not be called)
 *      - LE_BUSY if handlerRef will be called later to notify when install can proceed
 *      - LE_FAULT on error
 */
//--------------------------------------------------------------------------------------------------
le_result_t avcServer_QueryInstall
(
    avcServer_InstallHandlerFunc_t handlerRef  ///< [IN] Handler to receive install response.
)
{
    le_result_t result;

    if ( QueryInstallHandlerRef != NULL )
    {
        LE_ERROR("Duplicate install attempt");
        return LE_FAULT;
    }

    result = QueryInstall();

    // Store the handler to call later, once install is allowed.
    if ( result == LE_BUSY )
    {
        QueryInstallHandlerRef = handlerRef;
    }
    else
    {
        QueryInstallHandlerRef = NULL;
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Query the AVC Server if it's okay to proceed with an application uninstall
 *
 * If an uninstall can't proceed right away, then the handlerRef function will be called when it is
 * okay to proceed with an uninstall. Note that handlerRef will be called at most once.
 *
 * @return
 *      - LE_OK if uninstall can proceed right away (handlerRef will not be called)
 *      - LE_BUSY if handlerRef will be called later to notify when uninstall can proceed
 *      - LE_FAULT on error
 */
//--------------------------------------------------------------------------------------------------
le_result_t avcServer_QueryUninstall
(
    avcServer_UninstallHandlerFunc_t handlerRef  ///< [IN] Handler to receive install response.
)
{
    le_result_t result;

    // Return busy, if user tries to uninstall multiple apps together
    // As the query is already in progress, both the apps will be removed after we get permission
    // for a single uninstall
    if ( QueryUninstallHandlerRef != NULL )
    {
        LE_ERROR("Duplicate uninstall attempt");
        return LE_BUSY;
    }

    result = QueryUninstall();

    // Store the handler to call later, once uninstall is allowed.
    if ( result == LE_BUSY )
    {
        QueryUninstallHandlerRef = handlerRef;
    }
    else
    {
        QueryUninstallHandlerRef = NULL;
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Receive the report from avcAppUpdate and pass it to the control APP
 *
 *
 * @return
 *      - void
 */
//--------------------------------------------------------------------------------------------------
void avcServer_ReportInstallProgress
(
    le_avc_Status_t updateStatus,
    uint installProgress,
    le_avc_ErrorCode_t errorCode
)
{
    if (StatusHandlerRef != NULL)
    {
        LE_DEBUG("Report install progress to registered handler.");

        // Notify registered control app
        StatusHandlerRef(updateStatus,
                         -1,
                         installProgress,
                         StatusHandlerContextPtr);
    }
    else
    {
        LE_DEBUG("No handler registered to receive install progress.");
    }

    if (updateStatus == LE_AVC_INSTALL_FAILED)
    {
        AvcErrorCode = errorCode;
    }
}


//--------------------------------------------------------------------------------------------------
// API functions
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
/**
 * le_avc_StatusHandler handler ADD function
 */
//--------------------------------------------------------------------------------------------------
le_avc_StatusEventHandlerRef_t le_avc_AddStatusEventHandler
(
    le_avc_StatusHandlerFunc_t handlerPtr,
        ///< [IN]

    void* contextPtr
        ///< [IN]
)
{
    // handlerPtr must be valid
    if ( handlerPtr == NULL )
    {
        LE_KILL_CLIENT("Null handlerPtr");
    }

    // Only allow the handler to be registered, if nothing is currently registered. In this way,
    // only one user app is allowed to register at a time.
    if ( StatusHandlerRef == NULL )
    {
        StatusHandlerRef = handlerPtr;
        StatusHandlerContextPtr = contextPtr;

        // Store the client session ref, to ensure only the registered client can call the other
        // control related API functions.
        RegisteredControlAppRef = le_avc_GetClientSessionRef();

        // Register our local handler with the PA, and this handler will in turn call the user
        // specified handler.  If there is no installed control app at the time this daemon starts,
        // then this registration happens in the COMPONENT_INIT. If a control app is later installed,
        // and registers a handler, there is no harm with re-registering with the PA.
        pa_avc_SetAVMSMessageHandler(UpdateHandler);

        // We only check at startup if the control app is installed, so this flag could be false
        // if the control app is installed later.  Obviously control app is installed now, so set
        // it to true, in case it is currently false.
        IsControlAppInstalled = true;

        return REGISTERED_HANDLER_REF;
    }
    else
    {
        LE_KILL_CLIENT("Handler already registered");
        return NULL;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * le_avc_StatusHandler handler REMOVE function
 */
//--------------------------------------------------------------------------------------------------
void le_avc_RemoveStatusEventHandler
(
    le_avc_StatusEventHandlerRef_t addHandlerRef
        ///< [IN]
)
{
    if ( addHandlerRef != REGISTERED_HANDLER_REF )
    {
        if ( addHandlerRef == NULL )
        {
            // If le_avc_AddStatusEventHandler() returns NULL, the value is still stored by the
            // generated code and cleaned up when the client dies, thus this check is necessary.
            // TODO: Fix the generated code.
            LE_ERROR("NULL ref ignored");
            return;
        }
        else
        {
            LE_KILL_CLIENT("Invalid ref = %p", addHandlerRef);
        }
    }

    if ( StatusHandlerRef == NULL )
    {
        LE_KILL_CLIENT("Handler not registered");
    }

    // Clear all info related to the registered handler.  Note that our local 'UpdateHandler'
    // must stay registered with the PA to ensure that automatic actions are performed, and
    // the state is properly tracked.
    StatusHandlerRef = NULL;
    StatusHandlerContextPtr = NULL;
    RegisteredControlAppRef = NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Start a session with the AirVantage server
 *
 * This will also cause a query to be sent to the server, for pending updates.
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_StartSession
(
    void
)
{
    if ( ! IsValidControlAppClient() )
        return LE_FAULT;

    return pa_avc_StartSession();
}


//--------------------------------------------------------------------------------------------------
/**
 * Stop a session with the AirVantage server
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_StopSession
(
    void
)
{
    if ( ! IsValidControlAppClient() )
        return LE_FAULT;

    return pa_avc_StopSession();
}


//--------------------------------------------------------------------------------------------------
/**
 * Accept the currently pending download
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_AcceptDownload
(
    void
)
{
    if ( ! IsValidControlAppClient() )
        return LE_FAULT;

    if ( CurrentState != AVC_DOWNLOAD_PENDING )
    {
        LE_ERROR("Expected AVC_DOWNLOAD_PENDING state; current state is %i", CurrentState);
        return LE_FAULT;
    }

    // Clear the error code.
    AvcErrorCode = LE_AVC_ERR_NONE;

    le_result_t result = pa_avc_SendSelection(PA_AVC_ACCEPT, 0);

    if ( result == LE_OK )
    {
        CurrentState = AVC_DOWNLOAD_IN_PROGRESS;
    }
    else
    {
        CurrentState = AVC_IDLE;
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Defer the currently pending download, for the given number of minutes
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_DeferDownload
(
    uint32_t deferMinutes
        ///< [IN]
)
{
    if ( ! IsValidControlAppClient() )
        return LE_FAULT;

    if ( CurrentState != AVC_DOWNLOAD_PENDING )
    {
        LE_ERROR("Expected AVC_DOWNLOAD_PENDING state; current state is %i", CurrentState);
        return LE_FAULT;
    }

    // Since the decision is not to download at this time, go back to idle
    CurrentState = AVC_IDLE;

    return pa_avc_SendSelection(PA_AVC_DEFER, deferMinutes);
}


//--------------------------------------------------------------------------------------------------
/**
 * Accept the currently pending firmware install
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
static le_result_t AcceptInstallFirmware
(
    void
)
{
    le_result_t result;

    // If a user app is blocking the install, then just defer for some time.  Hopefully, the
    // next time this function is called, the user app will no longer be blocking the install.
    //
    // todo: If there is an app that periodically blocks updates, then we don't want to use a
    //       BLOCKED_DEFER_TIME that is related to the period of the blocking app, as this might
    //       mean that we never accept the install.  This needs to be revisited. and perhaps a
    //       simple random number generator can be used to give varying defer times, or a
    //       completely different mechanism needs to be used.
    if ( BlockRefCount > 0 )
    {
        // Since the decision is not to install at this time, go back to idle
        CurrentState = AVC_IDLE;

        // This will cause another LE_AVC_INSTALL_PENDING to be sent to the control app. The API
        // documentation does not explicitly describe this behaviour, but it is implied.
        // todo: It may be worth either revisiting the documentation or changing the behaviour, but
        //       leave this decision for later.
        result = pa_avc_SendSelection(PA_AVC_DEFER, BLOCKED_DEFER_TIME);
    }
    else
    {
        result = pa_avc_SendSelection(PA_AVC_ACCEPT, 0);

        if ( result == LE_OK )
        {
            CurrentState = AVC_INSTALL_IN_PROGRESS;
        }
        else
        {
            CurrentState = AVC_IDLE;
        }
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Accept the currently pending application install
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
static le_result_t AcceptInstallApplication
(
    void
)
{
    // If a user app is blocking the install, then just defer for some time.  Hopefully, the
    // next time this function is called, the user app will no longer be blocking the install.
    if ( BlockRefCount > 0 )
    {
        // Since the decision is not to install at this time, go back to idle
        CurrentState = AVC_IDLE;

        // Try the install later
        le_clk_Time_t interval = { .sec = BLOCKED_DEFER_TIME*60 };

        le_timer_SetInterval(InstallDeferTimer, interval);
        le_timer_Start(InstallDeferTimer);
    }
    else
    {
        // Notify the registered handler to proceed with the install; only called once.
        CurrentState = AVC_INSTALL_IN_PROGRESS;
        QueryInstallHandlerRef();
        QueryInstallHandlerRef = NULL;
    }

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Accept the currently pending application uninstall
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
static le_result_t AcceptUninstallApplication
(
    void
)
{
    // If a user app is blocking the install, then just defer for some time.  Hopefully, the
    // next time this function is called, the user app will no longer be blocking the install.
    if ( BlockRefCount > 0 )
    {
        // Since the decision is not to install at this time, go back to idle
        CurrentState = AVC_IDLE;

        // Try the install later
        le_clk_Time_t interval = { .sec = BLOCKED_DEFER_TIME*60 };

        le_timer_SetInterval(UninstallDeferTimer, interval);
        le_timer_Start(UninstallDeferTimer);
    }
    else
    {
        // Notify the registered handler to proceed with the install; only called once.
        CurrentState = AVC_UNINSTALL_IN_PROGRESS;
        QueryUninstallHandlerRef();
        QueryUninstallHandlerRef = NULL;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Accept the currently pending install
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_AcceptInstall
(
    void
)
{
    if ( ! IsValidControlAppClient() )
    {
        return LE_FAULT;
    }

    if ( CurrentState != AVC_INSTALL_PENDING )
    {
        LE_ERROR("Expected AVC_INSTALL_PENDING state; current state is %i", CurrentState);
        return LE_FAULT;
    }

    // Clear the error code.
    AvcErrorCode = LE_AVC_ERR_NONE;

    if ( CurrentUpdateType == LE_AVC_FIRMWARE_UPDATE )
    {
        return AcceptInstallFirmware();
    }
    else if ( CurrentUpdateType == LE_AVC_APPLICATION_UPDATE )
    {
        return AcceptInstallApplication();
    }
    else
    {
        LE_ERROR("Unknown update type %d", CurrentUpdateType);
        return LE_FAULT;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Defer the currently pending install
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_DeferInstall
(
    uint32_t deferMinutes
        ///< [IN]
)
{
    if ( ! IsValidControlAppClient() )
    {
        return LE_FAULT;
    }

    if ( CurrentState != AVC_INSTALL_PENDING )
    {
        LE_ERROR("Expected AVC_INSTALL_PENDING state; current state is %i", CurrentState);
        return LE_FAULT;
    }

    // Since the decision is not to install at this time, go back to idle
    CurrentState = AVC_IDLE;

    if ( CurrentUpdateType == LE_AVC_FIRMWARE_UPDATE )
    {
        return pa_avc_SendSelection(PA_AVC_DEFER, deferMinutes);
    }
    else if ( CurrentUpdateType == LE_AVC_APPLICATION_UPDATE )
    {
        // Try the install later
        le_clk_Time_t interval = { .sec = (deferMinutes*60) };

        le_timer_SetInterval(InstallDeferTimer, interval);
        le_timer_Start(InstallDeferTimer);

        return LE_OK;
    }
    else
    {
        LE_ERROR("Unknown update type");
        return LE_FAULT;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Accept the currently pending uninstall
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_AcceptUninstall
(
    void
)
{
    if ( ! IsValidControlAppClient() )
    {
        return LE_FAULT;
    }

    if ( CurrentState != AVC_UNINSTALL_PENDING )
    {
        LE_ERROR("Expected AVC_UNINSTALL_PENDING state; current state is %i", CurrentState);
        return LE_FAULT;
    }

    return AcceptUninstallApplication();
}


//--------------------------------------------------------------------------------------------------
/**
 * Defer the currently pending uninstall
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_DeferUninstall
(
    uint32_t deferMinutes
        ///< [IN]
)
{
    if ( ! IsValidControlAppClient() )
    {
        return LE_FAULT;
    }

    if ( CurrentState != AVC_UNINSTALL_PENDING )
    {
        LE_ERROR("Expected AVC_UNINSTALL_PENDING state; current state is %i", CurrentState);
        return LE_FAULT;
    }

    // Since the decision is not to install at this time, go back to idle
    CurrentState = AVC_IDLE;

    LE_DEBUG("Deferring Uninstall for %d minute.", deferMinutes);

    // Try the uninstall later
    le_clk_Time_t interval = { .sec = (deferMinutes*60) };

    le_timer_SetInterval(UninstallDeferTimer, interval);
    le_timer_Start(UninstallDeferTimer);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the error code of the current update.
 */
//--------------------------------------------------------------------------------------------------
le_avc_ErrorCode_t le_avc_GetErrorCode
(
    void
)
{
    if ( ! IsValidControlAppClient() )
        return LE_FAULT;

    return AvcErrorCode;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the update type of the currently pending update
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT if not available
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_GetUpdateType
(
    le_avc_UpdateType_t* updateTypePtr
        ///< [OUT]
)
{
    if ( ! IsValidControlAppClient() )
        return LE_FAULT;

    if ( CurrentState == AVC_IDLE )
    {
        LE_ERROR("In AVC_IDLE state; no update pending or in progress");
        return LE_FAULT;
    }

    *updateTypePtr = CurrentUpdateType;
    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the update type of the currently pending update
 *
 */
//--------------------------------------------------------------------------------------------------
void avcServer_SetUpdateType
(
    le_avc_UpdateType_t updateType  ///< [IN]
)
{
    CurrentUpdateType = updateType;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the name for the currently pending application update
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT if not available, or is not APPL_UPDATE type
 */
//--------------------------------------------------------------------------------------------------
le_result_t le_avc_GetAppUpdateName
(
    char* updateName,
        ///< [OUT]

    size_t updateNameNumElements
        ///< [IN]
)
{
    if ( ! IsValidControlAppClient() )
        return LE_FAULT;

    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Prevent any pending updates from being installed.
 *
 * @return
 *      - Reference for block update request (to be used later for unblocking updates)
 *      - NULL if the operation was not successful
 */
//--------------------------------------------------------------------------------------------------
le_avc_BlockRequestRef_t le_avc_BlockInstall
(
    void
)
{
    // Need to return a unique reference that will be used by Unblock. Use the client session ref
    // as the data, since we need to delete the ref when the client closes.
    le_avc_BlockRequestRef_t blockRef = le_ref_CreateRef(BlockRefMap,
                                                         le_avc_GetClientSessionRef());

    // Keep track of how many refs have been allocated
    BlockRefCount++;

    return blockRef;
}


//--------------------------------------------------------------------------------------------------
/**
 * Allow any pending updates to be installed
 */
//--------------------------------------------------------------------------------------------------
void le_avc_UnblockInstall
(
    le_avc_BlockRequestRef_t blockRef
        ///< [IN]
        ///< block request ref returned by le_avc_BlockInstall
)
{
    // Look up the reference.  If it is NULL, then the reference is not valid.
    // Otherwise, delete the reference and update the count.
    void* dataRef = le_ref_Lookup(BlockRefMap, blockRef);
    if ( dataRef == NULL )
    {
        LE_KILL_CLIENT("Invalid block request reference %p", blockRef);
    }
    else
    {
        LE_PRINT_VALUE("%p", blockRef);
        le_ref_DeleteRef(BlockRefMap, blockRef);
        BlockRefCount--;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Sends a registration update to the server.
 */
//--------------------------------------------------------------------------------------------------
void avcServer_RegistrationUpdate
(
    void
)
{
    lwm2m_RegistrationUpdate();
}


//--------------------------------------------------------------------------------------------------
/**
 * Initialization function for AVC Daemon
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    // Create safe reference map for block references. The size of the map should be based on
    // the expected number of simultaneous block requests, so take a reasonable guess.
    BlockRefMap = le_ref_CreateMap("BlockRef", 5);

    // Add a handler for client session closes
    le_msg_AddServiceCloseHandler( le_avc_GetServiceRef(), ClientCloseSessionHandler, NULL );

    // Init shared timer for deferring app install
    InstallDeferTimer = le_timer_Create("install defer timer");
    le_timer_SetHandler(InstallDeferTimer, InstallTimerExpiryHandler);

    UninstallDeferTimer = le_timer_Create("uninstall defer timer");
    le_timer_SetHandler(UninstallDeferTimer, UninstallTimerExpiryHandler);

    // Initialize the sub-components
    assetData_Init();
    lwm2m_Init();
    avData_Init();

    // Read the user defined timeout from config tree @ /apps/avcService/modemActivityTimeout
    le_cfg_IteratorRef_t iterRef = le_cfg_CreateReadTxn(AVC_SERVICE_CFG);
    int timeout = le_cfg_GetInt(iterRef, "modemActivityTimeout", 20);
    le_cfg_CancelTxn(iterRef);

    pa_avc_SetModemActivityTimeout(timeout);

    // Check to see if le_avc is bound, which means there is an installed control app.
    IsControlAppInstalled = IsAvcBound();
    LE_INFO("Is control app installed? %i", IsControlAppInstalled);

    // If there is no installed control app, then register for indications with the PA. This is
    // necessary to ensure that automatic actions are performed.  If there's an installed control
    // app, the registration with the PA will happen when the control app registers a handler.
    if ( ! IsControlAppInstalled )
    {
        pa_avc_SetAVMSMessageHandler(UpdateHandler);
    }
}


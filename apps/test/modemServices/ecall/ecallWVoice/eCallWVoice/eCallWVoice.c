 /**
  * This module implements the le_ecall's test with a voice call connection.
  *
 * You must issue the following commands:
 * @verbatim
   $ app start eCallWVoice
   $ execInApp eCallWVoice eCallWVoice <PSAP number>
   @endverbatim

  * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
  *
  */

#include "legato.h"
#include "interfaces.h"

static const char *                     PsapNumber = NULL;
static le_ecall_CallRef_t               LastTestECallRef = NULL;

static le_audio_StreamRef_t MdmRxAudioRef = NULL;
static le_audio_StreamRef_t MdmTxAudioRef = NULL;
static le_audio_StreamRef_t FeInRef = NULL;
static le_audio_StreamRef_t FeOutRef = NULL;

static le_audio_ConnectorRef_t AudioInputConnectorRef = NULL;
static le_audio_ConnectorRef_t AudioOutputConnectorRef = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Connect audio.
 *
 */
//--------------------------------------------------------------------------------------------------
static void ConnectAudio
(
    void
)
{
    le_result_t res;

    MdmRxAudioRef = le_audio_OpenModemVoiceRx();
    LE_ERROR_IF((MdmRxAudioRef==NULL), "OpenModemVoiceRx returns NULL!");
    MdmTxAudioRef = le_audio_OpenModemVoiceTx();
    LE_ERROR_IF((MdmTxAudioRef==NULL), "OpenModemVoiceTx returns NULL!");

#if (ENABLE_CODEC == 1)
    // Redirect audio to the in-built Microphone and Speaker.
    FeOutRef = le_audio_OpenSpeaker();
    LE_ERROR_IF((FeOutRef==NULL), "OpenSpeaker returns NULL!");
    FeInRef = le_audio_OpenMic();
    LE_ERROR_IF((FeInRef==NULL), "OpenMic returns NULL!");
#else
    // Redirect audio to the PCM interface.
    FeOutRef = le_audio_OpenPcmTx(0);
    LE_ERROR_IF((FeOutRef==NULL), "OpenPcmTx returns NULL!");
    FeInRef = le_audio_OpenPcmRx(0);
    LE_ERROR_IF((FeInRef==NULL), "OpenPcmRx returns NULL!");
#endif

    AudioInputConnectorRef = le_audio_CreateConnector();
    LE_ERROR_IF((AudioInputConnectorRef==NULL), "AudioInputConnectorRef is NULL!");
    AudioOutputConnectorRef = le_audio_CreateConnector();
    LE_ERROR_IF((AudioOutputConnectorRef==NULL), "AudioOutputConnectorRef is NULL!");

    if (MdmRxAudioRef && MdmTxAudioRef && FeOutRef && FeInRef &&
        AudioInputConnectorRef && AudioOutputConnectorRef)
    {
        res = le_audio_Connect(AudioInputConnectorRef, FeInRef);
        LE_ERROR_IF((res!=LE_OK), "Failed to connect Mic on Input connector!");
        res = le_audio_Connect(AudioInputConnectorRef, MdmTxAudioRef);
        LE_ERROR_IF((res!=LE_OK), "Failed to connect mdmTx on Input connector!");
        res = le_audio_Connect(AudioOutputConnectorRef, FeOutRef);
        LE_ERROR_IF((res!=LE_OK), "Failed to connect Speaker on Output connector!");
        res = le_audio_Connect(AudioOutputConnectorRef, MdmRxAudioRef);
        LE_ERROR_IF((res!=LE_OK), "Failed to connect mdmRx on Output connector!");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Disconnection function.
 *
 */
//--------------------------------------------------------------------------------------------------
static void DisconnectAudio
(
    void
)
{
    if (AudioInputConnectorRef)
    {
        if (FeInRef)
        {
            LE_INFO("Disconnect %p from connector.%p", FeInRef, AudioInputConnectorRef);
            le_audio_Disconnect(AudioInputConnectorRef, FeInRef);
        }
        if(MdmTxAudioRef)
        {
            LE_INFO("Disconnect %p from connector.%p", MdmTxAudioRef, AudioInputConnectorRef);
            le_audio_Disconnect(AudioInputConnectorRef, MdmTxAudioRef);
        }
    }
    if(AudioOutputConnectorRef)
    {
        if(FeOutRef)
        {
            LE_INFO("Disconnect %p from connector.%p", FeOutRef, AudioOutputConnectorRef);
            le_audio_Disconnect(AudioOutputConnectorRef, FeOutRef);
        }
        if(MdmRxAudioRef)
        {
            LE_INFO("Disconnect %p from connector.%p", MdmRxAudioRef, AudioOutputConnectorRef);
            le_audio_Disconnect(AudioOutputConnectorRef, MdmRxAudioRef);
        }
    }

    if(AudioInputConnectorRef)
    {
        le_audio_DeleteConnector(AudioInputConnectorRef);
        AudioInputConnectorRef = NULL;
    }
    if(AudioOutputConnectorRef)
    {
        le_audio_DeleteConnector(AudioOutputConnectorRef);
        AudioOutputConnectorRef = NULL;
    }

    if(FeInRef)
    {
        le_audio_Close(FeInRef);
        FeInRef = NULL;
    }
    if(FeOutRef)
    {
        le_audio_Close(FeOutRef);
        FeOutRef = NULL;
    }
    if(MdmRxAudioRef)
    {
        le_audio_Close(MdmRxAudioRef);
        FeOutRef = NULL;
    }
    if(MdmTxAudioRef)
    {
        le_audio_Close(MdmTxAudioRef);
        FeOutRef = NULL;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Handler function for eCall state Notifications.
 *
 */
//--------------------------------------------------------------------------------------------------
static void MyECallEventHandler
(
    le_ecall_CallRef_t  eCallRef,
    le_ecall_State_t    state,
    void*               contextPtr
)
{
    LE_INFO("eCall TEST: New eCall state: %d for eCall ref.%p", state, eCallRef);

    switch(state)
    {
        case LE_ECALL_STATE_STARTED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_STARTED.");
#if (ENABLE_CODEC == 1)
            LE_INFO("Mute Speaker");
#else
            LE_INFO("Mute PCM Tx interface.");
#endif
            le_audio_Mute(FeOutRef);
            break;
        }
        case LE_ECALL_STATE_CONNECTED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_CONNECTED.");
            break;
        }
        case LE_ECALL_STATE_DISCONNECTED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_DISCONNECTED.");
            break;
        }
        case LE_ECALL_STATE_WAITING_PSAP_START_IND:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_WAITING_PSAP_START_IND.");
            break;
        }
        case LE_ECALL_STATE_PSAP_START_IND_RECEIVED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_PSAP_START_IND_RECEIVED.");
            if (le_ecall_SendMsd(eCallRef) != LE_OK)
            {
                LE_ERROR("Could not send the MSD");
            }
            break;
        }
        case LE_ECALL_STATE_MSD_TX_STARTED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_MSD_TX_STARTED.");
            break;
        }
        case LE_ECALL_STATE_LLNACK_RECEIVED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_LLNACK_RECEIVED.");
            break;
        }
        case LE_ECALL_STATE_LLACK_RECEIVED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_LLACK_RECEIVED.");
            break;
        }
        case LE_ECALL_STATE_MSD_TX_COMPLETED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_MSD_TX_COMPLETED.");
            break;
        }
        case LE_ECALL_STATE_MSD_TX_FAILED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_MSD_TX_FAILED.");
            break;
        }
        case LE_ECALL_STATE_ALACK_RECEIVED_POSITIVE:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_ALACK_RECEIVED_POSITIVE.");
            break;
        }
        case LE_ECALL_STATE_ALACK_RECEIVED_CLEAR_DOWN:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_ALACK_RECEIVED_CLEAR_DOWN.");
            break;
        }
        case LE_ECALL_STATE_STOPPED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_STOPPED.");
#if (ENABLE_CODEC == 1)
            LE_INFO("Unmute Speaker");
#else
            LE_INFO("Unmute PCM Tx interface.");
#endif
            le_audio_Unmute(FeOutRef);
            break;
        }
        case LE_ECALL_STATE_RESET:
        {
            // PSAP has correctly received the MSD
            LE_INFO("eCall state is LE_ECALL_STATE_RESET.");
#if (ENABLE_CODEC == 1)
            LE_INFO("Unmute Speaker");
#else
            LE_INFO("Unmute PCM Tx interface.");
#endif
            le_audio_Unmute(FeOutRef);
            break;
        }
        case LE_ECALL_STATE_COMPLETED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_COMPLETED.");
            break;
        }
        case LE_ECALL_STATE_FAILED:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_FAILED.");
#if (ENABLE_CODEC == 1)
            LE_INFO("Unmute Speaker");
#else
            LE_INFO("Unmute PCM Tx interface.");
#endif
            le_audio_Unmute(FeOutRef);
            break;
        }
        case LE_ECALL_STATE_END_OF_REDIAL_PERIOD:
        {
            LE_INFO("eCall state is LE_ECALL_STATE_END_OF_REDIAL_PERIOD.");
            break;
        }
        case LE_ECALL_STATE_UNKNOWN:
        default:
        {
            LE_INFO("Unknown eCall state.");
            break;
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Create and start a test eCall.
 *
 */
//--------------------------------------------------------------------------------------------------
static void StartTestECall
(
    void
)
{
    le_ecall_State_t                   state = LE_ECALL_STATE_UNKNOWN;
    le_ecall_StateChangeHandlerRef_t   stateChangeHandlerRef = 0x00;

    LE_INFO("Start StartTestECall");

    LE_ASSERT((stateChangeHandlerRef = le_ecall_AddStateChangeHandler(MyECallEventHandler, NULL)) != NULL);

    LE_ASSERT(le_ecall_SetPsapNumber(PsapNumber) == LE_OK);

    LE_ASSERT(le_ecall_SetMsdTxMode(LE_ECALL_TX_MODE_PUSH) == LE_OK);

    LE_ASSERT((LastTestECallRef=le_ecall_Create()) != NULL);

    LE_ASSERT(le_ecall_SetMsdPosition(LastTestECallRef, true, +48898064, +2218092, 0) == LE_OK);

    LE_ASSERT(le_ecall_SetMsdPassengersCount(LastTestECallRef, 3) == LE_OK);

    ConnectAudio();

    LE_ASSERT(le_ecall_StartTest(LastTestECallRef) == LE_OK);

    state=le_ecall_GetState(LastTestECallRef);
    LE_ASSERT(((state>=LE_ECALL_STATE_STARTED) && (state<=LE_ECALL_STATE_FAILED)));
}

//--------------------------------------------------------------------------------------------------
/**
 * The signal event handler function for SIGINT/SIGTERM when process dies.
 */
//--------------------------------------------------------------------------------------------------
static void SigHandler
(
    int sigNum
)
{
    LE_INFO("End and delete last test eCall");
    le_ecall_End(LastTestECallRef);
    le_ecall_Delete(LastTestECallRef);
    DisconnectAudio();
    exit(EXIT_SUCCESS);
}

//--------------------------------------------------------------------------------------------------
/**
 * Helper.
 *
 */
//--------------------------------------------------------------------------------------------------
static void PrintUsage()
{
    int idx;
    bool sandboxed = (getuid() != 0);
    const char * usagePtr[] = {
            "Usage of the eCallWVoice is:",
            "   execInApp eCallWVoice eCallWVoice <PSAP number>",
    };

    for(idx = 0; idx < NUM_ARRAY_MEMBERS(usagePtr); idx++)
    {
        if(sandboxed)
        {
            LE_INFO("%s", usagePtr[idx]);
        }
        else
        {
            fprintf(stderr, "%s\n", usagePtr[idx]);
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * App init.
 *
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    if (le_arg_NumArgs() == 1)
    {
        // Register a signal event handler for SIGINT when user interrupts/terminates process
        signal(SIGINT, SigHandler);

        PsapNumber = le_arg_GetArg(0);
        LE_INFO("======== Start eCallWVoice Test with PSAP.%s ========", PsapNumber);
#if (ENABLE_CODEC == 1)
        LE_INFO("         Audio is connected on Analogic interface.");
#else
        LE_INFO("         Audio is connected on PCM interface.");
#endif
        StartTestECall();

        LE_INFO("======== eCallWVoice Test SUCCESS ========");
    }
    else
    {
        PrintUsage();
        LE_INFO("EXIT eCallWVoice");
        exit(EXIT_FAILURE);
    }
}

//-------------------------------------------------------------------------------------------------
/**
 * @file cm_adc.c
 *
 * Handle adc related functionality. ADC channels are under the control of the modem as the adc
 * is muxed with the antennae inputs and used by the modem for antenna diagnostics. Therefore
 * we have to use modem services to read thos inputs.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */
//-------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "cm_adc.h"
#include "cm_common.h"


//--------------------------------------------------------------------------------------------------
/**
 * Structure for ADC list
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    char*                       channelNameStr;     ///< ADC channel string
    le_adc_AdcChannelInput_t    adcChannel;         ///< ADC channel value
} cm_adc_List_t;

//-------------------------------------------------------------------------------------------------
/**
 * ADC list definition
 */
//-------------------------------------------------------------------------------------------------
static const cm_adc_List_t AdcList[LE_ADC_MAX] =
{
    {"EXT_ADC1", LE_ADC_EXT_ADC1},
    {"EXT_ADC2", LE_ADC_EXT_ADC2}
};

//-------------------------------------------------------------------------------------------------
/**
 * Print the adc help text to stdout.
 */
//-------------------------------------------------------------------------------------------------
void cm_adc_PrintAdcHelp
(
    void
)
{
    printf("ADC usage\n"
           "==========\n\n"
           "To print known adc channels:\n"
           "\tcm adc list\n\n"
           "To read and print the value from an adc channel:\n"
           "\tcm adc read channel\n"
           "\t\twhere \"channel\" is one of the names returned by list\n\n"
           );
}

//-------------------------------------------------------------------------------------------------
/**
 * Print the adc channel list
 */
//-------------------------------------------------------------------------------------------------
void cm_adc_List
(
    void
)
{
    int i;
    printf("Available ADC channels:\n");
    for (i = 0; i < LE_ADC_MAX; i++)
    {
        printf("\t%s\n", AdcList[i].channelNameStr);
    }
}

//-------------------------------------------------------------------------------------------------
/**
 * Read the value form a named adc channel.
 */
//-------------------------------------------------------------------------------------------------
static le_result_t cm_adc_ReadAndPrintValue
(
    const char* channelName
)
{
    int i;
    le_result_t result = LE_FAULT;
    int32_t value;
    bool found = false;

    for (i = 0; i < LE_ADC_MAX; i++)
    {
        if (strcmp(AdcList[i].channelNameStr, channelName) == 0)
        {
            found = true;
            result = le_adc_ReadValue(AdcList[i].adcChannel, &value);
            if (result == LE_OK)
            {
                printf("%s:%d\n", channelName, value);
            }
            break;
        }
    }

    if (!found)
    {
        printf("Unknown channel: %s\n", channelName);
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Process commands for ADC service.
 */
//--------------------------------------------------------------------------------------------------
void cm_adc_ProcessAdcCommand
(
    const char * command,   ///< [IN] Command
    size_t numArgs          ///< [IN] Number of arguments
)
{
    if (strcmp(command, "help") == 0)
    {
        cm_adc_PrintAdcHelp();
    }
    else if (strcmp(command, "list") == 0)
    {
        cm_adc_List();
    }
    else if (strcmp(command, "read") == 0)
    {
        const char* channelName;

        if(numArgs < 3)
        {
            printf("adc read requires a channel name\n");
            exit(EXIT_FAILURE);
        }
        else if (numArgs > 3)
        {
            printf("adc read extra arguments will be ignored\n");
        }

        channelName = le_arg_GetArg(2);

        if(LE_OK != cm_adc_ReadAndPrintValue(channelName))
        {
            printf("Read %s failed.\n", channelName);
            exit(EXIT_FAILURE);
        }

    }
    else
    {
        printf("Invalid command for adc service.\n");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}

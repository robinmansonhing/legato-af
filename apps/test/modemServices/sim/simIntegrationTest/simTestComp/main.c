 /**
  * This module is for unit testing of the modemServices component.
  *
  *
  * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
  *
  */

#include "main.h"

#define TEST_STRING_LEN 50

//-------------------------------------------------------------------------------------------------
/**
 * Structure to hold an enum <=> string relation for @ref le_sim_Id_t.
 */
//-------------------------------------------------------------------------------------------------
typedef struct
{
    le_sim_Id_t  simId;
    const char * strPtr;
}
SimIdStringAssoc_t;

//-------------------------------------------------------------------------------------------------
/**
 * Array containing all enum <=> string relations for @ref le_sim_Id_t.
 */
//-------------------------------------------------------------------------------------------------
static const SimIdStringAssoc_t SimIdStringAssocs[] = {
    { LE_SIM_EMBEDDED,          "emb" },
    { LE_SIM_EXTERNAL_SLOT_1,   "ext1" },
    { LE_SIM_EXTERNAL_SLOT_2,   "ext2" },
    { LE_SIM_REMOTE,            "rem" },
};

//! [Print]
//--------------------------------------------------------------------------------------------------
/**
 * Print function.
 *
 */
//--------------------------------------------------------------------------------------------------
void Print
(
    char* string
)
{
    bool sandboxed = (getuid() != 0);

    if(sandboxed)
    {
        LE_INFO("%s", string);
    }
    else
    {
        fprintf(stderr, "%s\n", string);
    }
}
//! [Print]

//--------------------------------------------------------------------------------------------------
/**
 * Help.
 *
 */
//--------------------------------------------------------------------------------------------------
static void PrintUsage
(
    void
)
{
    int idx;
    const char * usagePtr[] = {
            "Usage of the 'simTest' application is:",
            "SIM allocation test: simTest create <ext/emb> <pin>",
            "SIM state test: simTest state <ext/emb> <pin>",
            "SIM authentification test: simTest auth <ext/emb> <pin> <puk>",
            "No SIM test: simTest nosim <ext/emb>",
            "SIM select: simTest select",
            "SIM lock test: simTest lock <emb/ext1/ext2/rem> <pin>",
            "SIM GetICCID test: simTest iccid <emb/ext1/ext2/rem>"
            "",
    };

    for(idx = 0; idx < NUM_ARRAY_MEMBERS(usagePtr); idx++)
    {
        Print((char*) usagePtr[idx]);
    }
}

//-------------------------------------------------------------------------------------------------
/**
 * This function converts a string to a le_sim_Id_t.
 *
 * @return Type as an enum
 */
//-------------------------------------------------------------------------------------------------
static le_sim_Id_t GetSimId
(
    const char * strPtr
)
{
    int i;
    for(i = 0; i < NUM_ARRAY_MEMBERS(SimIdStringAssocs); i++)
    {
        if (0 == strcmp(SimIdStringAssocs[i].strPtr, strPtr))
        {
            return SimIdStringAssocs[i].simId;
        }
    }

    LE_ERROR("Unable to convert '%s' to a le_sim_Id_t", strPtr);
    PrintUsage();
    exit(EXIT_FAILURE);
}

//--------------------------------------------------------------------------------------------------
/**
 * Main thread.
 *
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    const char*     testString = "";
    le_sim_Id_t     cardId;

    LE_INFO("Start simTest app.");

    // Get the test identifier and SIM select
    if (le_arg_NumArgs() >= 1)
    {
        testString = le_arg_GetArg(0);
    }

    if (le_arg_NumArgs() > 1)
    {
        cardId = GetSimId(le_arg_GetArg(1));
    }

    // Test: state
    if (strcmp(testString, "state") == 0)
    {
        const char* pin;

        // Get the pin code
        if (le_arg_NumArgs() == 3)
        {
            pin = le_arg_GetArg(2);
        }
        else
        {
            PrintUsage();
            exit(EXIT_FAILURE);
        }

        // Call the test function
        simTest_State(cardId, pin);
    }
    // Test: create
    else if (strcmp(testString, "create") == 0)
    {
        const char* pin;

        // Get the pin code
        if (le_arg_NumArgs() == 3)
        {
            pin = le_arg_GetArg(2);
        }
        else
        {
            Print("error");
            PrintUsage();
            exit(EXIT_FAILURE);
        }

        // Call the test function
        simTest_Create(cardId, pin);
    }
    // Test: authentification
    else if (strcmp(testString, "auth") == 0)
    {
        const char* pin;
        const char* puk;



        // Get the pin and puk codes
        if (le_arg_NumArgs() == 4)
        {
            pin = le_arg_GetArg(2);
            puk = le_arg_GetArg(3);
        }
        else
        {
            PrintUsage();
            exit(EXIT_FAILURE);
        }

        // Call the test function
        simTest_Authentication(cardId,pin,puk);
    }
    // Test: no sim
    else if (strcmp(testString, "nosim") == 0)
    {
        // Call the test function
        simTest_SimAbsent(cardId);
    }
    // Test: SIM selection
    else if (strcmp(testString, "select") == 0)
    {
        // Call the test function
        simTest_SimSelect();
    }
    // Test: lock
    else if (strcmp(testString, "lock") == 0)
    {
        const char* pin;

        // Get the pin code
        if (le_arg_NumArgs() == 3)
        {
            pin = le_arg_GetArg(2);
        }
        else
        {
            Print("error");
            PrintUsage();
            exit(EXIT_FAILURE);
        }

        // Call the test function
        simTest_Lock(cardId, pin);
    }
    // Test: SIM Get ICCID
    else if (strcmp(testString, "iccid") == 0)
    {
        // Call the test function
        simTest_SimGetIccid(cardId);
    }
    else
    {
        PrintUsage();
        exit(EXIT_FAILURE);
    }
}

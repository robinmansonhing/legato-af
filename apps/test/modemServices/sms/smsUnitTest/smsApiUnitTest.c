/**
  * This module implements the le_sms's unit tests.
  *
  * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
  *
  */

#include "interfaces.h"
#include "pa_sms.h"
#include "pa_simu.h"
#include "pa_sms_simu.h"

#include "le_sms_local.h"

//--------------------------------------------------------------------------------------------------
/**
 * Test sequence Structure list
 */
//--------------------------------------------------------------------------------------------------


#define VOID_PATTERN  ""

#define SHORT_TEXT_TEST_PATTERN  "Short"
#define LARGE_TEXT_TEST_PATTERN  "Large Text Test pattern Large Text Test pattern Large Text" \
     " Test pattern Large Text Test pattern Large Text Test pattern Large Text Test patt"
#define TEXT_TEST_PATTERN        "Text Test pattern"

#define FAIL_TEXT_TEST_PATTERN  "Fail Text Test pattern Fail Text Test pattern Fail Text Test" \
    " pattern Fail Text Test pattern Fail Text Test pattern Fail Text Test pattern Fail" \
    " Text Test pattern Text Test pattern "

#define NB_SMS_ASYNC_TO_SEND   5

/*
 * Pdu message can be created with this link http://www.smartposition.nl/resources/sms_pdu.html
 */
static uint8_t PDU_TEST_PATTERN_7BITS[]=
                           {0x00,0x01,0x00,0x0A,0x81,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x11,0xD4,0x32,0x9E,0x0E,0xA2,0x96,0xE7,0x74,0x10,0x3C,0x4C,
                            0xA7,0x97,0xE5,0x6E};

static uint8_t BINARY_TEST_PATTERN[]={0x05,0x01,0x00,0x0A};


static char DEST_TEST_PATTERN[] = "0123456789";


//--------------------------------------------------------------------------------------------------
/**
 * Test: Text Message Object Set/Get APIS.
 *
 */
//--------------------------------------------------------------------------------------------------
static void Testle_sms_SetGetText
(
    void
)
{
    le_sms_MsgRef_t       myMsg;
    char                  timestamp[LE_SMS_TIMESTAMP_MAX_BYTES];
    char                  tel[LE_MDMDEFS_PHONE_NUM_MAX_BYTES];
    char                  text[LE_SMS_TEXT_MAX_BYTES] = {0};

    myMsg = le_sms_Create();
    LE_ASSERT(myMsg);

    LE_ASSERT(le_sms_SetDestination(myMsg, DEST_TEST_PATTERN) == LE_OK);

    LE_ASSERT(le_sms_SetText(myMsg, TEXT_TEST_PATTERN) == LE_OK);

    LE_ASSERT(le_sms_GetFormat(myMsg) == LE_SMS_FORMAT_TEXT);

    LE_ASSERT(le_sms_GetSenderTel(myMsg, tel, sizeof(tel)) == LE_NOT_PERMITTED);

    LE_ASSERT(le_sms_GetTimeStamp(myMsg, timestamp, sizeof(timestamp)) == LE_NOT_PERMITTED);

    LE_ASSERT(le_sms_GetUserdataLen(myMsg) == strlen(TEXT_TEST_PATTERN));

    LE_ASSERT(le_sms_GetText(myMsg, text, 1) == LE_OVERFLOW);

    LE_ASSERT(le_sms_GetText(myMsg, text, sizeof(text)) == LE_OK);

    LE_ASSERT(strncmp(text, TEXT_TEST_PATTERN, strlen(TEXT_TEST_PATTERN)) == 0);

    LE_ASSERT(le_sms_SetDestination(myMsg, VOID_PATTERN) == LE_BAD_PARAMETER);

    LE_ASSERT(le_sms_SetText(myMsg, VOID_PATTERN) == LE_BAD_PARAMETER);

    le_sms_Delete(myMsg);
}

//--------------------------------------------------------------------------------------------------
/**
 * Test: Raw binary Message Object Set/Get APIS.
 *
 */
//--------------------------------------------------------------------------------------------------
static void Testle_sms_SetGetBinary
(
    void
)
{
    le_sms_MsgRef_t       myMsg;
    char                  timestamp[LE_SMS_TIMESTAMP_MAX_BYTES];
    char                  tel[LE_MDMDEFS_PHONE_NUM_MAX_BYTES];
    uint8_t               raw[LE_SMS_BINARY_MAX_BYTES];
    size_t                uintval;
    uint32_t              i;

    myMsg = le_sms_Create();
    LE_ASSERT(myMsg);

    LE_ASSERT(le_sms_SetDestination(myMsg, DEST_TEST_PATTERN) == LE_OK);

    LE_ASSERT(le_sms_SetPDU(myMsg, PDU_TEST_PATTERN_7BITS, (sizeof(PDU_TEST_PATTERN_7BITS)/
                     sizeof(PDU_TEST_PATTERN_7BITS[0])) ) == LE_OK);

    LE_ASSERT(le_sms_SetBinary(myMsg, BINARY_TEST_PATTERN, sizeof(BINARY_TEST_PATTERN)) == LE_OK);

    LE_ASSERT(le_sms_GetFormat(myMsg) == LE_SMS_FORMAT_BINARY);

    LE_ASSERT(le_sms_GetSenderTel(myMsg, tel, sizeof(tel)) == LE_NOT_PERMITTED);

    LE_ASSERT(le_sms_GetTimeStamp(myMsg, timestamp, sizeof(timestamp)) == LE_NOT_PERMITTED);

    uintval = le_sms_GetUserdataLen(myMsg);
    LE_ASSERT(uintval == sizeof(BINARY_TEST_PATTERN));

    uintval=1;
    LE_ASSERT(le_sms_GetBinary(myMsg, raw, &uintval) == LE_OVERFLOW);

    uintval = sizeof(BINARY_TEST_PATTERN);
    LE_ASSERT(le_sms_GetBinary(myMsg, raw, &uintval) == LE_OK);

    for(i=0; i<sizeof(BINARY_TEST_PATTERN) ; i++)
    {
        LE_ASSERT(raw[i] == BINARY_TEST_PATTERN[i]);
    }

    LE_ASSERT(uintval == sizeof(BINARY_TEST_PATTERN));

    LE_ASSERT(le_sms_SetDestination(myMsg, VOID_PATTERN) == LE_BAD_PARAMETER);

    LE_ASSERT(le_sms_SetBinary(myMsg, BINARY_TEST_PATTERN, 0) == LE_BAD_PARAMETER);

    le_sms_Delete(myMsg);
}

//--------------------------------------------------------------------------------------------------
/**
 * Test: PDU Message Object Set/Get APIS.
 *
 */
//--------------------------------------------------------------------------------------------------
static void Testle_sms_SetGetPDU
(
    void
)
{
    le_sms_MsgRef_t       myMsg;
    char                  timestamp[LE_SMS_TIMESTAMP_MAX_BYTES];
    char                  tel[LE_MDMDEFS_PHONE_NUM_MAX_BYTES];
    uint8_t               pdu[LE_SMS_PDU_MAX_BYTES];
    size_t                uintval;
    uint32_t              i;

    myMsg = le_sms_Create();
    LE_ASSERT(myMsg);

    LE_ASSERT(le_sms_SetPDU(myMsg, PDU_TEST_PATTERN_7BITS, (sizeof(PDU_TEST_PATTERN_7BITS)/
                    sizeof(PDU_TEST_PATTERN_7BITS[0]))) == LE_OK);

    LE_ASSERT(le_sms_GetSenderTel(myMsg, tel, sizeof(tel)) == LE_NOT_PERMITTED);

    LE_ASSERT(le_sms_GetTimeStamp(myMsg, timestamp, sizeof(timestamp)) == LE_NOT_PERMITTED);

    uintval = le_sms_GetPDULen(myMsg);
    LE_ASSERT(uintval == (sizeof(PDU_TEST_PATTERN_7BITS)/sizeof(PDU_TEST_PATTERN_7BITS[0])));

    uintval = 1;
    LE_ASSERT(le_sms_GetPDU(myMsg, pdu, &uintval) == LE_OVERFLOW);

    uintval = sizeof(pdu);
    LE_ASSERT(le_sms_GetPDU(myMsg, pdu, &uintval) == LE_OK);

    for(i=0; i<sizeof(PDU_TEST_PATTERN_7BITS) ; i++)
    {
        LE_ASSERT(pdu[i] == PDU_TEST_PATTERN_7BITS[i]);
    }

    LE_ASSERT(uintval == sizeof(PDU_TEST_PATTERN_7BITS)/sizeof(PDU_TEST_PATTERN_7BITS[0]));

    LE_ASSERT(le_sms_SetPDU(myMsg, PDU_TEST_PATTERN_7BITS, 0) == LE_BAD_PARAMETER);

    le_sms_Delete(myMsg);
}

/*
 * Test case le_sms_GetSmsCenterAddress() and le_sms_SetSmsCenterAddress() APIs
 */
static void Testle_sms_SetGetSmsCenterAddress
(
    void
)
{
    char smscMdmRefStr[LE_MDMDEFS_PHONE_NUM_MAX_BYTES] = {0};
    char smscMdmStr[LE_MDMDEFS_PHONE_NUM_MAX_BYTES] = {0};
    char smscStrs[LE_MDMDEFS_PHONE_NUM_MAX_BYTES] = "+33123456789";

    // Get current SMS service center address.
    // Check LE_OVERFLOW error case
    LE_ASSERT(le_sms_GetSmsCenterAddress(smscMdmRefStr, 5) == LE_OVERFLOW);

    // Get current SMS service center address.
    LE_ASSERT(le_sms_GetSmsCenterAddress(smscMdmRefStr, LE_MDMDEFS_PHONE_NUM_MAX_BYTES) == LE_OK);

    // Set "+33123456789" SMS service center address.
    LE_ASSERT(le_sms_SetSmsCenterAddress(smscStrs) == LE_OK);

    // Get current SMS service center address.
    LE_ASSERT(le_sms_GetSmsCenterAddress(smscMdmStr, LE_MDMDEFS_PHONE_NUM_MAX_BYTES) == LE_OK);

    // Restore previous SMS service center address.
    LE_ASSERT(le_sms_SetSmsCenterAddress(smscMdmRefStr) == LE_OK);

    // check if value get match with value set.
    LE_ASSERT(strncmp(smscStrs,smscMdmStr, LE_MDMDEFS_PHONE_NUM_MAX_BYTES) == 0);
}


//--------------------------------------------------------------------------------------------------
/**
 * Required: At less two SMS with unknown encoding format must be present in the SIM
 * Test: Check that Legato can create a List object that lists the received messages with
 *   unknown encoding format present in the storage area.  Test that messages status can be
 *   changed or these messages can be deleted
 */
//--------------------------------------------------------------------------------------------------
static void Testle_sms_ErrorDecodingReceivedList()
{
    le_sms_MsgRef_t         lMsg1, lMsg2;
    le_sms_MsgListRef_t     receivedList;

    // List Received messages
    receivedList = le_sms_CreateRxMsgList();

    if (receivedList)
    {
        lMsg1 = le_sms_GetFirst(receivedList);
        LE_ASSERT(lMsg1 != NULL);
        LE_ASSERT(le_sms_GetStatus(lMsg1) == LE_SMS_RX_READ);

        // le_sms_Delete() API kills client if message belongs in a Rx list.
        LE_INFO("-TEST- Delete Rx message 1 from storage.%p", lMsg1);

        // Verify Mark Read functions on Rx message list
        le_sms_MarkRead(lMsg1);
        LE_ASSERT(le_sms_GetStatus(lMsg1) == LE_SMS_RX_READ);

        // Verify Mark Unread functions on Rx message list
        le_sms_MarkUnread(lMsg1);
        LE_ASSERT(le_sms_GetStatus(lMsg1) == LE_SMS_RX_UNREAD);

        LE_INFO("-TEST- Delete Rx message 1 from storage.%p", lMsg1);

        le_sms_DeleteFromStorage(lMsg1);
        lMsg2 = le_sms_GetNext(receivedList);
        LE_ASSERT(lMsg2 != NULL);
        le_sms_DeleteFromStorage(lMsg2);
        LE_INFO("-TEST- Delete the ReceivedList");
        le_sms_DeleteList(receivedList);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * SMS API Unitary Test
 */
//--------------------------------------------------------------------------------------------------
void testle_sms_SmsApiUnitTest
(
    void
)
{
    LE_ASSERT(le_sms_Init() == LE_OK);

    LE_INFO("Test Testle_sms_SetGetSmsCenterAddress started");
    Testle_sms_SetGetSmsCenterAddress();

    LE_INFO("Test Testle_sms_SetGetBinary started");
    Testle_sms_SetGetBinary();

    LE_INFO("Test Testle_sms_SetGetText started");
    Testle_sms_SetGetText();

    LE_INFO("Test Testle_sms_SetGetPDU started");
    Testle_sms_SetGetPDU();

    LE_INFO("Test Testle_sms_ErrorDecodingReceivedList started");
    Testle_sms_ErrorDecodingReceivedList();

    LE_INFO("smsApiUnitTest sequence PASSED");
}

/**
 * @file semaphores.h
 *
 * Semaphore module's intra-framework header file. This file exposes type definitions and
 * function interfaces to other modules inside the framework implementation.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */

#ifndef LEGATO_SRC_SEMAPHORE_H_INCLUDE_GUARD
#define LEGATO_SRC_SEMAPHORE_H_INCLUDE_GUARD

#include "limit.h"

//--------------------------------------------------------------------------------------------------
/**
 * Semaphore object.
 */
//--------------------------------------------------------------------------------------------------
typedef struct le_sem_t
{
    le_dls_Link_t       semaphoreListLink;   ///< Used to link onto the process's Semaphore List.
    le_dls_List_t       waitingList;         ///< List of threads waiting for this semaphore.
    pthread_mutex_t     waitingListMutex;    ///< Pthreads mutex used to protect the waiting list.
    sem_t               semaphore;           ///< Pthreads semaphore that does the real work. :)
    char                nameStr[LIMIT_MAX_SEMAPHORE_NAME_BYTES]; ///< The name of the semaphore (UTF8 string).
}
Semaphore_t;


//--------------------------------------------------------------------------------------------------
/**
 * Semaphore Thread Record.
 *
 * This structure is to be stored as a member in each Thread object.  The function
 * thread_GetSemaphoreRecPtr() is used by the semaphore module to fetch a pointer to one of
 * these records for a given thread.
 *
 * @warning No code outside of the semaphore module (semaphore.c) should ever access the members
 * of this structure.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    le_sem_Ref_t    waitingOnSemaphore; ///< Reference to the semaphore that is being waited on.
    le_dls_Link_t   waitingListLink;    ///< Used to link into Semaphore object's waiting list.
}
sem_ThreadRec_t;


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the Semaphore module.
 *
 * This function must be called exactly once at process start-up before any other semaphore
 * module functions are called.
 */
//--------------------------------------------------------------------------------------------------
void sem_Init
(
    void
);


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the thread-specific parts of the semaphore module.
 *
 * This function must be called once by each thread when it starts, before any other semaphore
 * module functions are called by that thread.
 */
//--------------------------------------------------------------------------------------------------
void sem_ThreadInit
(
    void
);


#endif /* LEGATO_SRC_SEMAPHORE_H_INCLUDE_GUARD */

//--------------------------------------------------------------------------------------------------
/** @file cgroups.c
 *
 * API for creating and managing cgroups.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */

#include "legato.h"
#include "cgroups.h"
#include "limit.h"
#include "fileDescriptor.h"
#include "fileSystem.h"
#include "killProc.h"


//--------------------------------------------------------------------------------------------------
/**
 * Cgroup sub-system names.
 */
//--------------------------------------------------------------------------------------------------
static const char* SubSysName[CGRP_NUM_SUBSYSTEMS] = {"cpu", "memory", "freezer"};


//--------------------------------------------------------------------------------------------------
/**
 * Root path for all cgroups.
 */
//--------------------------------------------------------------------------------------------------
#define ROOT_PATH                   "/sys/fs/cgroup"
#define ROOT_NAME                   "cgroupsRoot"


//--------------------------------------------------------------------------------------------------
/**
 * Cgroup tasks file.  The tasks file list the PIDs of all threads in a cgroup.
 */
//--------------------------------------------------------------------------------------------------
#define TASKS_FILENAME              "tasks"


//--------------------------------------------------------------------------------------------------
/**
 * Cgroup procs file.  The procs file list the PIDs of all processes in a cgroup.
 */
//--------------------------------------------------------------------------------------------------
#define PROCS_FILENAME              "cgroup.procs"


//--------------------------------------------------------------------------------------------------
/**
 * Cpu shares file.
 */
//--------------------------------------------------------------------------------------------------
#define CPU_SHARES_FILENAME         "cpu.shares"


//--------------------------------------------------------------------------------------------------
/**
 * Memory limit file.
 */
//--------------------------------------------------------------------------------------------------
#define MEM_LIMIT_FILENAME          "memory.limit_in_bytes"


//--------------------------------------------------------------------------------------------------
/**
 * Freeze state file.
 */
//--------------------------------------------------------------------------------------------------
#define FREEZE_STATE_FILENAME       "freezer.state"


//--------------------------------------------------------------------------------------------------
/**
 * Maximum digits in a cgroup integer value.
 */
//--------------------------------------------------------------------------------------------------
#define MAX_DIGITS                  100


//--------------------------------------------------------------------------------------------------
/**
 * Maximum number of bytes in a freezing state string.
 */
//--------------------------------------------------------------------------------------------------
#define MAX_FREEZE_STATE_BYTES      20


//--------------------------------------------------------------------------------------------------
/**
 * Initializes cgroups for the system.  Sets up a hierarchy for each supported subsystem.
 *
 * @note Should be called once for the entire system, subsequent calls to this function will have no
 *       effect.  Must be called before any of the other functions in this API is called.
 *
 * @note Failures will cause the calling process to exit.
 */
//--------------------------------------------------------------------------------------------------
void cgrp_Init
(
    void
)
{
    // Setup the cgroup root directory if it does not already exist.
    if (!fs_IsMounted(ROOT_NAME, ROOT_PATH))
    {
        LE_FATAL_IF(mount(ROOT_NAME, ROOT_PATH, "tmpfs", 0, NULL) != 0,
                    "Could not mount cgroup root file system.  %m.");
    }

    // Setup a separate cgroup hierarch for each supported subsystem.
    cgrp_SubSys_t subSys = 0;
    for (; subSys < CGRP_NUM_SUBSYSTEMS; subSys++)
    {
        char dir[LIMIT_MAX_PATH_BYTES] = ROOT_PATH;

        LE_ASSERT(le_path_Concat("/", dir, sizeof(dir), SubSysName[subSys], (char*)NULL) == LE_OK);

        LE_ASSERT(le_dir_Make(dir, S_IRWXU) != LE_FAULT);

        if (!fs_IsMounted(SubSysName[subSys], dir))
        {
            LE_FATAL_IF(mount(SubSysName[subSys], dir, "cgroup", 0, SubSysName[subSys]) != 0,
                        "Could not mount cgroup subsystem '%s'.  %m.", SubSysName[subSys]);

            LE_INFO("Mounted cgroup hiearchy for subsystem '%s'.", SubSysName[subSys]);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Opens a cgroup file.
 *
 * @return
 *      The file descriptor of the cgroup's tasks file if successful.
 *      A negative value if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static int OpenCgrpFile
(
    cgrp_SubSys_t subsystem,        ///< [IN] Sub-system of the cgroup.
    const char* cgroupNamePtr,      ///< [IN] Name of the cgroup.
    const char* fileNamePtr,        ///< [IN] Name of the file.
    int accessMode                  ///< [IN] Either O_RDONLY, O_WRONLY, or O_RDWR.

)
{
    // Create the path to the cgroup file.
    char path[LIMIT_MAX_PATH_BYTES] = ROOT_PATH;
    LE_ASSERT(le_path_Concat("/", path, sizeof(path), SubSysName[subsystem], cgroupNamePtr,
                             fileNamePtr, (char*)NULL) == LE_OK);

    // Open the cgroup file.
    int fd;

    do
    {
        fd = open(path, accessMode);
    }
    while ((fd < 0) && (errno == EINTR));

    if (fd < 0)
    {
        LE_ERROR("Could not open file '%s'.  %m.", path);
    }

    return fd;
}


//--------------------------------------------------------------------------------------------------
/**
 * Writes a string to a cgroup file.  Overwrites what is currently in the file.
 *
 * @note  Certain file types cannot accept certain types of data, and the write may fail with a
 *        specific errno value.  If the write fails with errno ESRCH this function will return
 *        LE_OUT_OF_RANGE.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OUT_OF_RANGE if an attempt was made to write a value that the file cannot accept.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t WriteToFile
(
    cgrp_SubSys_t subsystem,        ///< [IN] Sub-system of the cgroup.
    const char* cgroupNamePtr,      ///< [IN] Name of the cgroup.
    const char* fileNamePtr,        ///< [IN] File to write to.
    const char* string              ///< [IN] String to write into the file.
)
{
    // Get the length of the string.
    size_t len = strlen(string);
    LE_ASSERT(len > 0);

    // Open the file.
    int fd = OpenCgrpFile(subsystem, cgroupNamePtr, fileNamePtr, O_WRONLY);

    if (fd < 0)
    {
        return LE_FAULT;
    }

    // Write the string to the file.
    le_result_t result = LE_OK;
    ssize_t numBytesWritten = 0;

    do
    {
        numBytesWritten = write(fd, string, len);
    }
    while ((numBytesWritten == -1) && (errno == EINTR));

    if (numBytesWritten != len)
    {
        LE_ERROR("Could not write '%s' to file '%s' in cgroup '%s'.  %m.",
                 string, fileNamePtr, cgroupNamePtr);

        if (errno == ESRCH)
        {
            result = LE_OUT_OF_RANGE;
        }
        else
        {
            result = LE_FAULT;
        }
    }

    fd_Close(fd);

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets a value from a cgroup file.  The value is read as a string and so a NULL-terminator is
 * always appended to the end of the read value in bufPtr.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW if the provided buffer is too small.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetValue
(
    cgrp_SubSys_t subsystem,        ///< [IN] Sub-system of the cgroup.
    const char* cgroupNamePtr,      ///< [IN] Name of the cgroup.
    const char* fileNamePtr,        ///< [IN] File name to read from.
    char* bufPtr,                   ///< [OUT] Buffer to store the value in.
    size_t bufSize                  ///< [IN] Size of the buffer.
)
{
    // Open the file.
    int fd = OpenCgrpFile(subsystem, cgroupNamePtr, fileNamePtr, O_RDONLY);

    if (fd < 0)
    {
        return LE_FAULT;
    }

    // Read the value from the file.
    ssize_t numBytesRead;

    do
    {
        numBytesRead = read(fd, bufPtr, bufSize);
    }
    while ( (numBytesRead == -1) && (errno == EINTR) );

    // Check if the read value is valid.
    le_result_t result;

    if (numBytesRead == -1)
    {
        LE_ERROR("Could not read file '%s' in cgroup '%s'.  %m.", fileNamePtr, cgroupNamePtr);
        result = LE_FAULT;
    }
    else if (numBytesRead == bufSize)
    {
        // The value in the file is larger than the provided buffer.  Truncate the buffer.
        bufPtr[bufSize-1] = '\0';
        result = LE_OVERFLOW;
    }
    else
    {
        // Null-terminate the string.
        bufPtr[numBytesRead] = '\0';

        // Remove trailing newline characters.
        while ((numBytesRead > 0) && (bufPtr[numBytesRead - 1] == '\n'))
        {
            numBytesRead--;
            bufPtr[numBytesRead] = '\0';
        }

        result = LE_OK;
    }

    fd_Close(fd);

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Reads a PID from the opened procs or tasks file specified by fd.  Updates the file offset of fd.
 *
 * @return
 *      The current PID read from the file if successful.
 *      LE_OUT_OF_RANGE if there is nothing left to read.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static pid_t GetTasksId
(
    int fd                      ///< [IN] File descriptor to an opened procs or tasks file.
)
{
    // Read a pid from the file.
    pid_t pid;
    char pidStr[100];
    le_result_t result = fd_ReadLine(fd, pidStr, sizeof(pidStr));

    LE_FATAL_IF(result == LE_OVERFLOW, "Buffer to read PID is too small.");

    if (result == LE_OK)
    {
        // Convert the string to a pid and store it in the caller's buffer.
        le_result_t r = le_utf8_ParseInt(&pid, pidStr);

        if (r == LE_OK)
        {
            return pid;
        }

        LE_ERROR("Could not convert '%s' to a PID.  %s.", pidStr, LE_RESULT_TXT(r));
        result = LE_FAULT;
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Modifies the string by removing all trailing white space from the string.
 */
//--------------------------------------------------------------------------------------------------
static void RemoveTrailingWhiteSpace
(
    char* strPtr                    ///< [IN] String to modify.
)
{
    ssize_t i = strlen(strPtr) - 1;

    for (; i >= 0; i--)
    {
        if (isspace(strPtr[i]) == 0)
        {
            strPtr[i + 1] = '\0';
            return;
        }
    }

    strPtr[0] = '\0';
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a cgroup with the specified name in the specified sub-system.  If the cgroup already
 * exists this function has no effect.
 *
 * Sub-groups can be created by providing a path as the name.  For example,
 * cgrp_Create(CGRP_SUBSYS_CPU, "Students/Undergrads"); will create a cgroup called "Undergrads"
 * that is a sub-group of "Students".  Note that all parent groups must first exist before a
 * sub-group can be created.
 *
 * @return
 *      LE_OK if successful.
 *      LE_DUPLICATE if the cgroup already exists.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t cgrp_Create
(
    cgrp_SubSys_t subsystem,        ///< Sub-system the cgroup belongs to.
    const char* cgroupNamePtr       ///< Name of the cgroup to create.
)
{
    // Create the path to the cgroup.
    char path[LIMIT_MAX_PATH_BYTES] = ROOT_PATH;
    LE_ASSERT(le_path_Concat("/", path, sizeof(path), SubSysName[subsystem], cgroupNamePtr,
                             (char*)NULL) == LE_OK);

    // Create the cgroup.
    le_result_t result = le_dir_Make(path, S_IRWXU);

    if (result == LE_DUPLICATE)
    {
        LE_ERROR("Cgroup %s already exists.", path);
        return LE_DUPLICATE;
    }
    else if (result == LE_FAULT)
    {
        LE_ERROR("Could not create cgroup %s.", path);
        return LE_FAULT;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds a process to a cgroup.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OUT_OF_RANGE if the process doesn't exist.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t cgrp_AddProc
(
    cgrp_SubSys_t subsystem,        ///< Sub-system of the cgroup.
    const char* cgroupNamePtr,      ///< Name of the cgroup to add the process to.
    pid_t pidToAdd                  ///< PID of the process to add.
)
{
    // Convert the pid to a string.
    char pidStr[MAX_DIGITS];

    LE_ASSERT(snprintf(pidStr, sizeof(pidStr), "%d", pidToAdd) < sizeof(pidStr));

    // Write the pid to the file.
    return WriteToFile(subsystem, cgroupNamePtr, PROCS_FILENAME, pidStr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Reads a list of tids/pids from an open file descriptor.  The number of pids in the file may be
 * larger than maxIds, in which case idListPtr will be filled with the first maxIds PIDs. We can
 * re-use this code for tids or pids because, in linux, all tids are pids and vice versa.
 *
 * @return
 *      The number of ids that are read if successful.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
static ssize_t BuildTidList
(
    int fd,
    pid_t* idListPtr,               ///< [OUT] Buffer that will contain the list of PIDs.
    size_t maxIds                   ///< [IN] The maximum number of pids pidListPtr can hold.
)
{
    // Read the pids from the file.
    size_t numTids = 0;

    while (1)
    {
        pid_t tid = GetTasksId(fd);

        if (tid >= 0)
        {
            numTids++;

            if (numTids <= maxIds)
            {
                idListPtr[numTids-1] = tid;
            }
        }
        else if (tid == LE_OUT_OF_RANGE)
        {
            // No more PIDs.
            break;
        }
        else
        {
            return LE_FAULT;
        }
    }
    return numTids;
}

//--------------------------------------------------------------------------------------------------
/**
 * Gets a list of threads that are in a cgroup.  The number of threads in the cgroup may be
 * larger than maxTids, in which case tidListPtr will be filled with the first maxTids TIDs.
 *
 * @return
 *      The number of threads that are in the cgroup if successful.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
ssize_t cgrp_GetThreadList
(
    cgrp_SubSys_t subsystem,        ///< [IN] Sub-system of the cgroup.
    const char* cgroupNamePtr,      ///< [IN] Name of the cgroup.
    pid_t* tidListPtr,              ///< [OUT] Buffer that will contain the list of TIDs.
    size_t maxTids                  ///< [IN] The maximum number of tids tidListPtr can hold.
)
{
    // Open the cgroup's tasks file for reading.
    int fd = OpenCgrpFile(subsystem, cgroupNamePtr, TASKS_FILENAME, O_RDONLY);

    if (fd < 0)
    {
        return LE_FAULT;
    }

    size_t numTids = BuildTidList(fd, tidListPtr, maxTids);

    fd_Close(fd);

    if (numTids == LE_FAULT)
    {
        LE_ERROR("Error reading the '%s' cgroup's tasks.", cgroupNamePtr);
    }

    return numTids;
}

//--------------------------------------------------------------------------------------------------
/**
 * Gets a list of processes that are in a cgroup.  The number of processes in the cgroup may be
 * larger than maxPids, in which case pidListPtr will be filled with the first maxPids PIDs.
 *
 * @return
 *      The number of threads that are in the cgroup if successful.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
ssize_t cgrp_GetProcessesList
(
    cgrp_SubSys_t subsystem,        ///< [IN] Sub-system of the cgroup.
    const char* cgroupNamePtr,      ///< [IN] Name of the cgroup.
    pid_t* pidListPtr,              ///< [OUT] Buffer that will contain the list of PIDs.
    size_t maxPids                  ///< [IN] The maximum number of pids pidListPtr can hold.
)
{
    // Open the cgroup's processes file for reading.
    int fd = OpenCgrpFile(subsystem, cgroupNamePtr, PROCS_FILENAME, O_RDONLY);

    if (fd < 0)
    {
        return LE_FAULT;
    }

    size_t numPids = BuildTidList(fd, pidListPtr, maxPids);

    if (numPids == LE_FAULT)
    {
        LE_ERROR("Error reading the '%s' cgroup's tasks.", cgroupNamePtr);
    }

    fd_Close(fd);

    return numPids;
}

//--------------------------------------------------------------------------------------------------
/**
 * Sends the specified signal to all the processes in the specified cgroup.
 *
 * @return
 *      The number of PIDs that are in the cgroup.
 *      LE_FAULT if there an error.
 */
//--------------------------------------------------------------------------------------------------
ssize_t cgrp_SendSig
(
    cgrp_SubSys_t subsystem,        ///< [IN] Sub-system of the cgroup.
    const char* cgroupNamePtr,      ///< [IN] Name of the cgroup.
    int sig                         ///< [IN] The signal to send.
)
{
    // Open the cgroup's procs file for reading.
    int fd = OpenCgrpFile(subsystem, cgroupNamePtr, PROCS_FILENAME, O_RDONLY);

    if (fd < 0)
    {
        return LE_FAULT;
    }

    // Iterate over the pids in the procs file.
    size_t numPids = 0;

    while (1)
    {
        pid_t pid = GetTasksId(fd);

        if (pid >= 0)
        {
            numPids++;

            kill_SendSig(pid, sig);
        }
        else if (pid == LE_OUT_OF_RANGE)
        {
            // No more PIDs.
            break;
        }
        else
        {
            LE_ERROR("Error reading the '%s' cgroup's tasks.", cgroupNamePtr);
            fd_Close(fd);
            return LE_FAULT;
        }
    }

    fd_Close(fd);
    return numPids;
}


//--------------------------------------------------------------------------------------------------
/**
 * Checks if the specified cgroup is empty of all processes.
 *
 * @return
 *      true if the specified cgroup has no processes in it.
 *      false if there are processes in the specified cgroup.
 */
//--------------------------------------------------------------------------------------------------
bool cgrp_IsEmpty
(
    cgrp_SubSys_t subsystem,        ///< [IN] Sub-system of the cgroup.
    const char* cgroupNamePtr       ///< [IN] Name of the cgroup.
)
{
    // Open the cgroup's tasks file for reading.
    int fd = OpenCgrpFile(subsystem, cgroupNamePtr, TASKS_FILENAME, O_RDONLY);

    if (fd < 0)
    {
        return false;
    }

    // Read a tid from the file.
    pid_t tid = GetTasksId(fd);
    fd_Close(fd);

    if (tid >= 0)
    {
        return false;
    }
    else if (tid == LE_OUT_OF_RANGE)
    {
        // No tasks.
        return true;
    }
    else
    {
        LE_ERROR("Error reading the '%s' cgroup's tasks.", cgroupNamePtr);
        return false;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Deletes a cgroup.
 *
 * @note A cgroup can only be removed when there are no processes in the group.  Ensure there are no
 *       processes in a cgroup (by killing the processes) before attempting to delete it.
 *
 * @return
 *      LE_OK if the cgroup was successfully deleted.
 *      LE_BUSY if the cgroup could not be deleted because there are still processes in the cgroup.
 *      LE_FAULT if there was some other error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t cgrp_Delete
(
    cgrp_SubSys_t subsystem,        ///< Sub-system of the cgroup.
    const char* cgroupNamePtr       ///< Name of the cgroup to delete.
)
{
    // Create the path to the cgroup.
    char path[LIMIT_MAX_PATH_BYTES] = ROOT_PATH;
    LE_ASSERT(le_path_Concat("/", path, sizeof(path), SubSysName[subsystem], cgroupNamePtr,
                             (char*)NULL) == LE_OK);

    // Attempt to remove the cgroup directory.
    if (rmdir(path) != 0)
    {
        if (errno == EBUSY)
        {
            LE_ERROR("Could not remove cgroup '%s'.  Tasks (process) list may not be empty.  %m.",
                     path);
            return LE_BUSY;
        }
        else
        {
            LE_ERROR("Could not remove cgroup '%s'.  %m.", path);
            return LE_FAULT;
        }
    }

    LE_DEBUG("Deleted cgroup %s.", path);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the name of sub-system.
 *
 * @note Do not attempt to modify the returned name in place.  If you need to make modifications
 *       copy the name into your own buffer.
 *
 * @return
 *      The name of the sub-system.
 */
//--------------------------------------------------------------------------------------------------
const char* cgrp_SubSysName
(
    cgrp_SubSys_t subsystem         ///< Sub-system.
)
{
    return SubSysName[subsystem];
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets the cpu share of a cgroup.
 *
 * Cpu share is used to calculate the cpu percentage for a process relative to all other processes
 * in the system.  Newly created cgroups and processes not belonging to a cgroup are given a default
 * value of 1024.  The actual percentage of the cpu given to a process is calculated as:
 *
 * (share value of process) / (sum of shares from all processes contending for the cpu)
 *
 * All processes within a cgroup share the available cpu share for that cgroup.
 *
 * For example:
 *
 * cgroupA is configured with the default share value, 1024.
 * cgroupB is configured with 512 as its share value.
 * cgroupC is configured with 2048 as its share value.
 *
 * cgroupA has one process running.
 * cgroupB has two processes running.
 * cgroupC has one process running.
 *
 * Assuming that all processes in cgroupA, cgroupB and cgroupC are running and not blocked waiting
 * for some I/O or timer event and that another system process is also running.
 *
 * Sum of all shares (including the one system process) is 1024 + 512 + 2048 + 1024 = 4608
 *
 * The process in cgroupA will get 1024/4608 = 22% of the cpu.
 * The two processes in cgroupB will share 512/4608 = 11% of the cpu, each process getting 5.5%.
 * The process in cgroupC will get 2048/4608 = 44% of the cpu.
 * The system process will get 1024/4608 = 22% of the cpu.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t cgrp_cpu_SetShare
(
    const char* cgroupNamePtr,      ///< Name of the cgroup to set the share for.
    size_t share                    ///< Share value to set.  See the function header for more
                                    ///  details.
)
{
    // Convert the value to a string.
    char shareStr[MAX_DIGITS];
    LE_ASSERT(snprintf(shareStr, sizeof(shareStr), "%zd", share) < sizeof(shareStr));

    // Write the share value to the file.
    if (WriteToFile(CGRP_SUBSYS_CPU, cgroupNamePtr, CPU_SHARES_FILENAME, shareStr) != LE_OK)
    {
        return LE_FAULT;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets the memory limit for a cgroup.
 *
 * @note All processes in a cgroup share the available memory for that cgroup.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t cgrp_mem_SetLimit
(
    const char* cgroupNamePtr,      ///< Name of the cgroup to set the limit for.
    size_t limit                    ///< Memory limit in kilobytes.
)
{
    // Convert the limit to a string.
    char limitStr[MAX_DIGITS];

    LE_ASSERT(snprintf(limitStr, sizeof(limitStr), "%zd", limit * 1024) < sizeof(limitStr));

    // Write the limit to the file.
    if (WriteToFile(CGRP_SUBSYS_MEM, cgroupNamePtr, MEM_LIMIT_FILENAME, limitStr) != LE_OK)
    {
        return LE_FAULT;
    }

    // Read the limit to see if it was set properly.
    char readLimitStr[MAX_DIGITS];

    if (GetValue(CGRP_SUBSYS_MEM,
                 cgroupNamePtr,
                 MEM_LIMIT_FILENAME,
                 readLimitStr,
                 sizeof(readLimitStr)) != LE_OK)
    {
        return LE_FAULT;
    }

    if (strcmp(limitStr, readLimitStr) != 0)
    {
        LE_WARN("The memory limit for %s was actually set to %s instead of %s because of either \
page rounding or memory availability.", cgroupNamePtr, readLimitStr, limitStr);
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Freezes all the tasks in a cgroup.  This is an asynchronous function call that returns
 * immediately at which point the freeze state of the cgroup may not be updated yet.  Check the
 * current state of the cgroup using cgrp_frz_GetState().  Once a cgroup is frozen all tasks in the
 * cgroup are prevented from being scheduled by the kernel.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t cgrp_frz_Freeze
(
    const char* cgroupNamePtr       ///< [IN] Name of the cgroup.
)
{
    if (WriteToFile(CGRP_SUBSYS_FREEZE, cgroupNamePtr, FREEZE_STATE_FILENAME, "FROZEN") != LE_OK)
    {
        return LE_FAULT;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Thaws all the tasks in a cgroup.  This is an asynchronous function call that returns
 * immediately at which point the freeze state of the cgroup may not be updated yet.  Check the
 * current state of the cgroup using cgrp_frz_GetState().  Once a cgroup is thawed all tasks in the
 * cgroup are permitted to be scheduled by the kernel.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t cgrp_frz_Thaw
(
    const char* cgroupNamePtr       ///< [IN] Name of the cgroup.
)
{
    if (WriteToFile(CGRP_SUBSYS_FREEZE, cgroupNamePtr, FREEZE_STATE_FILENAME, "THAWED") != LE_OK)
    {
        return LE_FAULT;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the freeze state of the cgroup.
 *
 * @return
 *      Freeze state of the cgroup if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
cgrp_FreezeState_t cgrp_frz_GetState
(
    const char* cgroupNamePtr       ///< [IN] Name of the cgroup.
)
{
    char stateStr[MAX_FREEZE_STATE_BYTES] = {0};

    le_result_t result = GetValue(CGRP_SUBSYS_FREEZE,
                                  cgroupNamePtr,
                                  FREEZE_STATE_FILENAME,
                                  stateStr,
                                  sizeof(stateStr));

    LE_FATAL_IF(result == LE_OVERFLOW, "Freeze state string '%s...' is too long.", stateStr);

    if (result == LE_FAULT)
    {
        return LE_FAULT;
    }

    RemoveTrailingWhiteSpace(stateStr);

    if ( (strcmp(stateStr, "THAWED") == 0) ||
         (strcmp(stateStr, "FREEZING") == 0) )
    {
        return CGRP_THAWED;
    }
    else if (strcmp(stateStr, "FROZEN") == 0)
    {
        return CGRP_FROZEN;
    }

    LE_FATAL("Unrecognized freeze state '%s'.", stateStr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Gets the amount of memory used in bytes by a cgroup
 *
 * @return
 *      Number of bytes in use by the cgroup.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
ssize_t cgrp_GetMemUsed
(
    const char* cgroupNamePtr       ///< [IN] Name of the cgroup.
)
{
    char buffer[32];
    ssize_t result;
    if (GetValue(CGRP_SUBSYS_MEM,
                 cgroupNamePtr,
                 "memory.memsw.usage_in_bytes",
                 buffer,
                 sizeof(buffer)) == LE_OK)
    {
        result = strtol(buffer, NULL, 10);
        if ((errno == ERANGE) || (errno == EINVAL))
        {
            result = LE_FAULT;
        }
    }else{
        result = LE_FAULT;
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Gets the maximum amount of memory used in bytes by a cgroup.
 * @return
 *      Maximum number of bytes used at any time up to now by this cgroup.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
ssize_t cgrp_GetMaxMemUsed
(
    const char* cgroupNamePtr       ///< [IN] Name of the cgroup.
)
{
    char buffer[32];
    ssize_t result;
    if (GetValue(CGRP_SUBSYS_MEM,
                 cgroupNamePtr,
                 "memory.memsw.max_usage_in_bytes",
                 buffer,
                 sizeof(buffer)) == LE_OK)
    {
        result = strtol(buffer, NULL, 10);
        if ((errno == ERANGE) || (errno == EINVAL))
        {
            result = LE_FAULT;
        }
    }else{
        result = LE_FAULT;
    }
    return result;
}



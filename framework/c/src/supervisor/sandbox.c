//--------------------------------------------------------------------------------------------------
/** @file supervisor/sandbox.c
 *
 * API for creating Legato Sandboxes.
 *
 *  - @ref c_sand_intro
 *  - @ref c_sand_sandboxSetup
 *  - @ref c_sand_resourceLimits
 *  - @ref c_sand_confineProcess
 *  - @ref c_sand_accessServices
 *  - @ref c_sand_removeSandbox
 *
 * @section c_sand_intro Introduction
 *
 * Legato sandboxes are intended to provide a layer of security to the system from untrusted (or
 * not necessarily trusted) applications running in the same system.  Legato sandboxes isolate
 * applications from the rest of the system and makes it more difficult for a misbehaving
 * application from doing damage to the system and/or other applications.
 *
 * A Legato sandbox is not a virtualized environment.  It is suitable for isolating applications
 * that do not require root privileges and have minimal system requirements.
 *
 * For example, a system analysis utility that is used to gather statistics about which applications
 * use which resources is not a good candidate for a Legato sandboxed application.  However, an
 * application that displays the analyzed data to a web server would be a good candidate.
 *
 * Legato sandboxes are based on chroot jails.  Chroot changes the root directory of a process to
 * a specified location.  The process then only has access to files and directories under its root
 * directory.  Only processes with certain capabilities can find their way outside of their chrooted
 * root directory.
 *
 * Another important aspect of Legato sandboxes is resource limitations.  Resource limitations place
 * limits on the amount of system resources an application may consume.  Without resource limits an
 * isolated application could still damage the system by consuming all available resources.
 *
 * Essentially, there are three steps to sandboxing an application, sandbox setup, setting resource
 * limits, confining processes in the sandbox.
 *
 * @section c_sand_sandboxSetup Setting Up a Sandbox
 *
 * All sandboxes are created in non-persistent memory under /tmp/Legato/sandboxes.  Using
 * non-persistent memory has the benefit of automatically removing all sandboxes on system shutdown.
 *
 * The process for setting up a sandbox for an application is:
 *
 *      1) Create the directory /tmp/legato/sandboxes/appName.  This is the root of the sandbox.
 *         Creating a new directory gives us a way to separate applications from each other.
 *
 *      2) Mount a ramfs with a fixed size at the root of the sandbox.  Ramfs is used because it
 *         does not grow.  This gives control over how much ram the application can use for files.
 *         This is essentially the maximum size of the application's root file system.
 *
 *      3) Create standard directories in the sandbox, such as /tmp, /dev, etc.
 *
 *      4) Bind mount in standard files and devices into the sandbox, such as /dev/null, the Service
 *         Directory sockets, etc.
 *
 *      5) Bind mount in all other required files into the sandbox specific to the application.
 *
 * Legato sandboxes use bind mounts for importing files from the root file system into sandboxes.
 * Bind mounted files are similar to hard links and so are updated when the file is updated in the
 * root file system.  The advantage of this over copying the file into the sandbox is memory usage
 * managing updates.
 *
 * Bind mounts also work better than hard links because when a sym link is bind mounted into a
 * sandbox the sym link is followed and the actual file is mounted in the sandbox.  Also,
 * permissions on bind mounted files can be modified by re-mounting the file.
 *
 * @todo Remount is not currently being used but should be possible.  Need a way to set file
 *       permissions appropriately either by the Supervisor or the Installer.
 *
 *
 * @section c_sand_resourceLimits Setting Resource Limits
 *
 * Resource limits are set using the resource limits API and are set for both the application as a
 * whole and the individual processes in the application.
 *
 * The current implementation for setting resource limits uses both rlimits and cgroups.  With
 * rlimits the resource limits are always placed on the processes themselves rather than on the
 * application or user.  This is not an ideal situation because some limits such as the number
 * of processes in an application should apply directly to the application.  As a consequence
 * there is a bit of awkwardness when defining and setting some application level resource limits.
 *
 *
 * @section c_sand_confineProcess Confining Processes in Legato Sandboxes
 *
 * After the sandbox has been setup the application processes must be confined in the sandbox.  The
 * process for confining a process in a sandbox is:
 *
 *      1) Change the process's working directory to somewhere inside the sandbox.  This is
 *         important because if the working directory is left outside the sandbox the process can
 *         take advantage of this to escape the sandbox.
 *
 *      2) Chroot to the sandbox root.
 *
 *      3) Clear the supplementary groups list.
 *
 *      4) Set the primary group ID.
 *
 *      5) Set the user ID (effective, real and saved user IDs) to the application's unprivileged
 *         user ID.
 *
 * The process must start with root privileges (or the proper capibilities) to perform the above
 * functions.  After the user ID is set to the unprivileged user ID the process cannot regain root
 * privileges and cannot perform the above operations.
 *
 * The main vulnerability of Legato sandboxes is that once a process regains root privileges
 * (possibly through kernel bugs) the process can easily escape the sandbox and do damage to the
 * system.
 *
 * @todo Use a mandatory access control system like AppArmour to prevent damage even if root
 *       privileges are gained.
 *
 *
 * @section c_sand_accessServices Access Services from within a Sandbox
 *
 * A Legato sandboxed application is still able to access services outside of its sandbox albeit
 * indirectly.  All available services are advertised by the Service Directory.  Applications
 * connect to services by making a request to the Service Directory.  The Service Directory grants
 * access to the application if the application is in the ACL for the specified service.  The
 * Legato sandboxes is only one part of the Legato framework that helps make running applications
 * more secure.
 *
 *
 * @section c_sand_removeSandbox Removing Sandboxes
 *
 * The following procedure is used to remove a sandbox:
 *
 *      1) All application processes are killed.
 *
 *      2) All mounts are undone.
 *
 *      3) Created directories are deleted.
 *
 * @todo Use lazy unmount so unmounts will always succeed.
 *
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */

#include "legato.h"
#include "sandbox.h"
#include "limit.h"
#include "resourceLimits.h"
#include "fileDescriptor.h"
#include "smack.h"
#include "interfaces.h"
#include "dir.h"


#if ! defined LE_RUNTIME_DIR
    #error LE_RUNTIME_DIR not defined.
#endif
//--------------------------------------------------------------------------------------------------
/**
 * Location for all sandboxed apps.
 */
//--------------------------------------------------------------------------------------------------
#define SANDBOXES_DIR                   STRINGIZE(LE_RUNTIME_DIR) "sandboxes/"


//--------------------------------------------------------------------------------------------------
/**
 * The maximum portion of available memory that an application can use as it's file system is
 * calculated as:
 *
 *      free memory / APP_FS_MEM_LIM_DIVISOR
 */
//--------------------------------------------------------------------------------------------------
#define APP_FS_MEM_LIM_DIVISOR          4


//--------------------------------------------------------------------------------------------------
/**
 * The likely limit on number of possible file descriptors in the calling process.
 */
//--------------------------------------------------------------------------------------------------
#define MAX_PROCESS_FD                  1024


//--------------------------------------------------------------------------------------------------
/**
 * The name of the node in the config tree that contains the list of bundled files and directories.
 */
//--------------------------------------------------------------------------------------------------
#define CFG_NODE_BUNDLES                                "bundles"


//--------------------------------------------------------------------------------------------------
/**
 * The name of the node in the config tree that contains the list of required files and directories.
 */
//--------------------------------------------------------------------------------------------------
#define CFG_NODE_REQUIRES                               "requires"


//--------------------------------------------------------------------------------------------------
/**
 * The name of the node in the config tree that contains the list of import directives for files
 * that an application needs.
 */
//--------------------------------------------------------------------------------------------------
#define CFG_NODE_FILES                                  "files"


//--------------------------------------------------------------------------------------------------
/**
 * The name of the node in the config tree that contains the list of import directives for
 * directories that an application needs.
 */
//--------------------------------------------------------------------------------------------------
#define CFG_NODE_DIRS                                   "dirs"


//--------------------------------------------------------------------------------------------------
/**
 * The name of the node in the config tree that contains the list of import directives for
 * devices that an application needs.
 */
//--------------------------------------------------------------------------------------------------
#define CFG_NODE_DEVICES                                "devices"


//--------------------------------------------------------------------------------------------------
/**
 * Import object.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    char src[LIMIT_MAX_PATH_BYTES];
    char dest[LIMIT_MAX_PATH_BYTES];
}
ImportObj_t;



#if ! defined LE_SVCDIR_SERVER_SOCKET_NAME
    #error LE_SVCDIR_SERVER_SOCKET_NAME not defined.
#endif
#if ! defined LE_SVCDIR_CLIENT_SOCKET_NAME
    #error LE_SVCDIR_CLIENT_SOCKET_NAME not defined.
#endif
//--------------------------------------------------------------------------------------------------
/**
 * Files and directories to import into all sandboxes by default.
 */
//--------------------------------------------------------------------------------------------------
const ImportObj_t DefaultImportObjs[] = { {.src = STRINGIZE(LE_SVCDIR_SERVER_SOCKET_NAME), .dest = "/tmp/legato/"},
                                          {.src = STRINGIZE(LE_SVCDIR_CLIENT_SOCKET_NAME), .dest = "/tmp/legato/"},
                                          {.src = "/dev/log", .dest = "/dev/"},
                                          {.src = "/dev/null", .dest = "/dev/"},
                                          {.src = "/dev/zero", .dest = "/dev/"},
                                          {.src = "/legato/systems/current/lib/liblegato.so", .dest = "/lib/"} };

#if defined(TARGET_IMPORTS_X86_64)

//--------------------------------------------------------------------------------------------------
/**
 * Files and directories to import into all sandboxes by default for the default system.
 */
//--------------------------------------------------------------------------------------------------
const ImportObj_t DefaultSystemImportObjs[] = {
                                          {.src = "/lib/ld-linux-x86-64.so.2", .dest = "/lib/"},
                                          {.src = "/lib/libc.so.6", .dest = "/lib/"},
                                          {.src = "/lib/libpthread.so.0", .dest = "/lib/"},
                                          {.src = "/lib/librt.so.1", .dest = "/lib/"},
                                          {.src = "/lib/libdl.so.2", .dest = "/lib/"},
                                          {.src = "/lib/libgcc_s.so.1", .dest = "/lib/"},
                                          {.src = "/lib/libm.so.6", .dest = "/lib/"},
                                          {.src = "/usr/lib/libstdc++.so.6", .dest = "/lib/"} };

#elif defined(TARGET_IMPORTS_X86)

//--------------------------------------------------------------------------------------------------
/**
 * Files and directories to import into all sandboxes by default for the default system.
 */
//--------------------------------------------------------------------------------------------------
const ImportObj_t DefaultSystemImportObjs[] = {
                                          {.src = "/lib/ld-linux.so.2", .dest = "/lib/"},
                                          {.src = "/lib/libc.so.6", .dest = "/lib/"},
                                          {.src = "/lib/libpthread.so.0", .dest = "/lib/"},
                                          {.src = "/lib/librt.so.1", .dest = "/lib/"},
                                          {.src = "/lib/libdl.so.2", .dest = "/lib/"},
                                          {.src = "/lib/libgcc_s.so.1", .dest = "/lib/"},
                                          {.src = "/lib/libm.so.6", .dest = "/lib/"},
                                          {.src = "/usr/lib/libstdc++.so.6", .dest = "/lib/"} };

#elif defined(TARGET_IMPORTS_ARMV7)

//--------------------------------------------------------------------------------------------------
/**
 * Files and directories to import into all sandboxes by default for the default system.
 */
//--------------------------------------------------------------------------------------------------
const ImportObj_t DefaultSystemImportObjs[] = {
                                          {.src = "/lib/ld-linux.so.3", .dest = "/lib/"},
                                          {.src = "/lib/libc.so.6", .dest = "/lib/"},
                                          {.src = "/lib/libpthread.so.0", .dest = "/lib/"},
                                          {.src = "/lib/librt.so.1", .dest = "/lib/"},
                                          {.src = "/lib/libdl.so.2", .dest = "/lib/"},
                                          {.src = "/lib/libgcc_s.so.1", .dest = "/lib/"},
                                          {.src = "/lib/libm.so.6", .dest = "/lib/"},
                                          {.src = "/usr/lib/libstdc++.so.6", .dest = "/lib/"} };
#else
#error No "TARGET_IMPORTS_x" defined.
#endif

//--------------------------------------------------------------------------------------------------
/**
 * Used to store import objects in a list.
 **/
//--------------------------------------------------------------------------------------------------
typedef struct
{
    le_dls_Link_t   link;
    ImportObj_t     obj;
}
ImportObjListEntry_t;


//--------------------------------------------------------------------------------------------------
/**
 * Pool of import object list entry objects.
 **/
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t ImportObjListEntryPool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Figure out whether a given index is in the middle of a path node in a given path.
 *
 * @return true if index NOT:
 *              - at a slash,
 *              - just after a slash, or
 *              - at the beginning of the path.
 **/
//--------------------------------------------------------------------------------------------------
static inline bool InMiddleOfPathNode
(
    const char* path,
    size_t index
)
{
    return (   (index != 0)
            && (path[index] != '/')
            && (path[index - 1] != '/') );
}


//--------------------------------------------------------------------------------------------------
/**
 * Check whether one path1 is "inside" path2.  E.g., /foo/bar is "inside" /foo.
 **/
//--------------------------------------------------------------------------------------------------
static bool IsInside
(
    const char* path1,
    const char* path2
)
{
    size_t i = 0;

    while (path1[i] != '\0')
    {
        // Path 2 is shorter than path 1, and everything matches to this point.
        if (path2[i] == '\0')
        {
            // If we are not in the middle of a path node for path 1, then path 1 is in path 2.
            if (!InMiddleOfPathNode(path1, i))
            {
                return true;
            }
            // Otherwise, path 1 can't be inside path 2.
            else
            {
                return false;
            }
        }

        // We haven't hit the end of either path yet, so if they are different, then neither
        // is inside the other.
        if (path1[i] != path2[i])
        {
            return false;
        }

        i++;
    }

    // We have reached the end of path 1.  Path 2 must be the same or longer, in which case path 1
    // cannot be inside it.
    return false;
}


//--------------------------------------------------------------------------------------------------
/**
 * Compare two imports list entries and decide how to handle them.
 *
 * @return  LE_OK = insert the new entry before the old entry.
 *          LE_NOT_FOUND = continue searching the list for an insertion point.
 *          Anything else is an error.
 **/
//--------------------------------------------------------------------------------------------------
static le_result_t CompareImportEntries
(
    app_Ref_t appRef,           ///< [IN] Reference to the app object.
    ImportObjListEntry_t* newEntryPtr,
    ImportObjListEntry_t* oldEntryPtr
)
//--------------------------------------------------------------------------------------------------
{
    // If the new entry is going to be bind mounted inside the old entry,
    if (IsInside(newEntryPtr->obj.dest, oldEntryPtr->obj.dest))
    {
        // Make sure the old entry is from inside the app.
        if (!IsInside(oldEntryPtr->obj.src, app_GetInstallDirPath(appRef)))
        {
            LE_ERROR("Bind-mounting into a directory outside the app is not permitted.");
            LE_ERROR("Rejecting attempt to mount at '%s' which is mounted from '%s'.",
                     newEntryPtr->obj.dest,
                     oldEntryPtr->obj.src);
            return LE_FAULT;
        }
    }

    // If the old entry is going to be bind mounted inside the new entry,
    else if (IsInside(oldEntryPtr->obj.dest, newEntryPtr->obj.dest))
    {
        // Make sure the new entry is from inside the app.
        if (!IsInside(newEntryPtr->obj.src, app_GetInstallDirPath(appRef)))
        {
            LE_ERROR("Bind-mounting into a directory outside the app is not permitted.");
            LE_ERROR("Rejecting attempt to mount at '%s' which is mounted from '%s'.",
                     oldEntryPtr->obj.dest,
                     newEntryPtr->obj.src);
            return LE_FAULT;
        }
    }

    // Do a plain old alphabetical comparison.
    if (strcmp(newEntryPtr->obj.dest, oldEntryPtr->obj.dest) < 0)
    {
        return LE_OK;
    }
    else
    {
        return LE_NOT_FOUND;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add an object to the sorted list of objects to be imported into the sandbox.
 *
 * @note The list is sorted alphabetically by destination path to ensure that things deeper
 *       in the directory heirarchy appear later in the list.  This makes mounting work by
 *       ensuring that directories are mounted before things that should appear inside them
 *       are mounted.
 *
 * @return LE_OK if successful;
 **/
//--------------------------------------------------------------------------------------------------
static le_result_t AddToImportList
(
    le_dls_List_t* listPtr,
    app_Ref_t appRef,           ///< [IN] Reference to the app object.
    const char* srcPathPtr,     ///< [IN] Source file to import (always absolute).
    const char* destPathPtr     ///< [IN] Relative location in the sandbox to import the file to.
)
{
    LE_FATAL_IF(srcPathPtr[0] != '/', "Source path not absolute: '%s'", srcPathPtr);

    ImportObjListEntry_t* entryPtr;

    // Make sure the Import Object List Entry Pool has been created before we go using it.
    if (ImportObjListEntryPool == NULL)
    {
        ImportObjListEntryPool = le_mem_CreatePool("FilePaths", sizeof(ImportObjListEntry_t));
        if (le_mem_GetObjectCount(ImportObjListEntryPool) < 50) // NOTE: 50 is arbitrary.
        {
            le_mem_ExpandPool(ImportObjListEntryPool,
                              50 - le_mem_GetObjectCount(ImportObjListEntryPool));
        }
    }

    // Create a new Import Object List Entry object.
    entryPtr = le_mem_ForceAlloc(ImportObjListEntryPool);

    entryPtr->link = LE_DLS_LINK_INIT;

    size_t len;

    if (le_utf8_Copy(entryPtr->obj.src, srcPathPtr, sizeof(entryPtr->obj.src), NULL) != LE_OK)
    {
        LE_CRIT("Path '%s' is too long.", srcPathPtr);
    }
    else if (   le_utf8_Copy(entryPtr->obj.dest, destPathPtr, sizeof(entryPtr->obj.dest), &len)
             != LE_OK)
    {
        LE_CRIT("Path '%s' is too long.", destPathPtr);
    }
    else
    {
        // If the dest path ends in a slash, append the base name of the source.
        if (destPathPtr[len - 1] == '/')
        {
            if (le_utf8_Copy(entryPtr->obj.dest + len,
                             le_path_GetBasenamePtr(srcPathPtr, "/"),
                             sizeof(entryPtr->obj.dest) - len,
                             NULL)
                != LE_OK)
            {
                LE_ERROR("Import destination path '%s' for app '%s' is too long.",
                         entryPtr->obj.dest,
                         app_GetName(appRef));
                return LE_FAULT;
            }
        }


        // Search the list of Import Objects to find the right insertion point.
        le_dls_Link_t* linkPtr = le_dls_Peek(listPtr);

        while (linkPtr != NULL)
        {
            ImportObjListEntry_t* existingEntryPtr;

            existingEntryPtr = CONTAINER_OF(linkPtr, ImportObjListEntry_t, link);

            // Compare the destination path strings.
            le_result_t compareResult = CompareImportEntries(appRef, entryPtr, existingEntryPtr);

            if (compareResult == LE_OK)
            {
                // Insert the new object in front of the existing one.
                le_dls_AddBefore(listPtr, linkPtr, &entryPtr->link);

                return LE_OK;
            }
            else if (compareResult != LE_NOT_FOUND)
            {
                // Something went wrong.  Discard the entry and return an error.
                le_mem_Release(entryPtr);

                return compareResult;
            }

            linkPtr = le_dls_PeekNext(listPtr, linkPtr);
        }

        // If everything in the list (if anything) had a lower or equal alphabetical value
        // than the one we are adding, then add the new one to the end of the list.
        le_dls_Queue(listPtr, &entryPtr->link);

        return LE_OK;
    }

    le_mem_Release(entryPtr);

    return LE_OVERFLOW;
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the sandbox location path string.  The sandbox does not have to exist before this function
 * is called.  This function gives the expected location of the sandbox by simply appending the
 * appName to the sandbox root path.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW the provided buffer is too small.
 */
//--------------------------------------------------------------------------------------------------
le_result_t sandbox_GetPath
(
    const char* appName,    ///< [IN] The application name.
    char* pathBuf,          ///< [OUT] The buffer that will contain the path string.
    size_t pathBufSize      ///< [IN] The buffer size.
)
{
    LE_ASSERT( (pathBuf != NULL) && (pathBufSize > 0) );

    pathBuf[0] = '\0';

    return le_path_Concat("/", pathBuf, pathBufSize, SANDBOXES_DIR, appName, NULL);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create the absolute destination path relative to the sandbox root by appending the destPtr to the
 * sandbox root.  If destPtr is a directory (ends with a separator) then append the basename of the
 * source path onto the end of the destination path.
 *
 * @return
 *      LE_OK if successful.
 *      LE_OVERFLOW the provided buffer is too small.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetAbsDestPath
(
    const char* srcPathPtr,     ///< [IN] Source path.
    const char* destPtr,        ///< [IN] Relative destination path.
    const char* sandboxRoot,    ///< [IN] Path to the root of the sandbox.
    char* bufPtr,               ///< [OUT] Buffer to store the final destination path.
    size_t bufSize              ///< [IN] Size of the buffer.
)
{
    bufPtr[0] = '\0';

    if (destPtr[strlen(destPtr)-1] == '/')
    {
        // The dest path is a directory so append the base name of the source.
        return le_path_Concat("/", bufPtr, bufSize, sandboxRoot, destPtr,
                              le_path_GetBasenamePtr(srcPathPtr, "/"), NULL);
    }
    else
    {
        return le_path_Concat("/", bufPtr, bufSize, sandboxRoot, destPtr, NULL);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Import the file at srcPathPtr into the sandbox at the relative location destPathPtr.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ImportFile
(
    const char* srcPathPtr,     ///< [IN] Source file to import.
    const char* destPathPtr,    ///< [IN] Relative location in the sandbox to import the file to.
    const char* sandboxRoot,    ///< [IN] Root directory of the sandbox to import the file into.
    const char* smackLabelPtr   ///< [IN] SMACK label to use for any created directories.
)
{
    // Create the absolute destination path.
    char destPath[LIMIT_MAX_PATH_BYTES] = "";

    if (GetAbsDestPath(srcPathPtr, destPathPtr, sandboxRoot, destPath, sizeof(destPath)) != LE_OK)
    {
        LE_ERROR("Import destination path '%s' is too long.", destPath);
        return LE_FAULT;
    }

    // Get the destination directory to create.
    char destDir[LIMIT_MAX_PATH_BYTES];

    if (le_path_GetDir(destPath, "/", destDir, sizeof(destDir)) != LE_OK)
    {
        LE_ERROR("Destination path '%s' is too long.", destPath);
        return LE_FAULT;
    }

    // Make the destination path.
    if (dir_MakePathSmack(destDir,
                          S_IRUSR | S_IXUSR | S_IROTH | S_IXOTH,
                          smackLabelPtr) == LE_FAULT)
    {
        return LE_FAULT;
    }

    // Create an empty file at the specified path, if one does not already exist.
    // NOTE: This is opened read-only to prevent destruction of any pre-existing file.
    int fd;
    while ( ((fd = open(destPath, O_RDONLY | O_CREAT, S_IRUSR)) == -1) && (errno == EINTR) ) {}

    if (fd == -1)
    {
        LE_ERROR("Could not create file '%s'.  %m", destPath);
        return LE_FAULT;
    }

    fd_Close(fd);

    // Bind mount file into the sandbox.
    if (mount(srcPathPtr, destPath, NULL, MS_BIND, NULL) != 0)
    {
        LE_ERROR("Couldn't import '%s' into sandbox at '%s'. %m", srcPathPtr, destPath);
        return LE_FAULT;
    }

    LE_INFO("Imported file '%s' into sandbox '%s'.", srcPathPtr, destPath);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Import the directory at srcPathPtr into the sandbox at the relative location destPathPtr.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ImportDir
(
    const char* srcPathPtr,     ///< [IN] Source directory to import.
    const char* destPathPtr,    ///< [IN] Relative location in the sandbox to import to.
    const char* sandboxRoot,    ///< [IN] Root directory of the sandbox to import to.
    const char* smackLabelPtr   ///< [IN] SMACK label to use for any created directories.
)
{
    // Create the absolute destination path.
    char destPath[LIMIT_MAX_PATH_BYTES] = "";

    if (GetAbsDestPath(srcPathPtr, destPathPtr, sandboxRoot, destPath, sizeof(destPath)) != LE_OK)
    {
        LE_ERROR("Import destination path '%s' is too long.", destPath);
        return LE_FAULT;
    }

    // Make the destination path.
    if (dir_MakePathSmack(destPath,
                          S_IRUSR | S_IXUSR | S_IROTH | S_IXOTH,
                          smackLabelPtr) == LE_FAULT)
    {
        return LE_FAULT;
    }

    // Bind mount directory into the sandbox.
    if (mount(srcPathPtr, destPath, NULL, MS_BIND, NULL) != 0)
    {
        LE_ERROR("Could not import '%s' into sandbox destination '%s'.  %m", srcPathPtr, destPath);
        return LE_FAULT;
    }

    LE_INFO("Imported directory '%s' into sandbox '%s'.", srcPathPtr, destPath);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Import the file or directory at srcPathPtr into the sandbox at the relative location destPathPtr.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t Import
(
    le_dls_List_t* importListPtr,   ///< [IN] List of objects to be imported.
    const char* sandboxRoot,        ///< [IN] Root directory of the sandbox to import to.
    const char* smackLabelPtr       ///< [IN] SMACK label to use for any created directories.
)
{
    le_result_t result;

    le_dls_Link_t* linkPtr = le_dls_Peek(importListPtr);

    while (linkPtr != NULL)
    {
        ImportObjListEntry_t* listEntryPtr = CONTAINER_OF(linkPtr, ImportObjListEntry_t, link);

        const char* srcPathPtr = listEntryPtr->obj.src;
        const char* destPathPtr = listEntryPtr->obj.dest;

        if (le_dir_IsDir(srcPathPtr))
        {
            result = ImportDir(srcPathPtr, destPathPtr, sandboxRoot, smackLabelPtr);
        }
        else
        {
            result = ImportFile(srcPathPtr, destPathPtr, sandboxRoot, smackLabelPtr);
        }

        if (result != LE_OK)
        {
            return result;
        }

        linkPtr = le_dls_PeekNext(importListPtr, linkPtr);
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Release all the list entry objects on a given list.
 *
 * Leaves the list empty.
 **/
//--------------------------------------------------------------------------------------------------
static void ReleaseImportList
(
    le_dls_List_t* importListPtr    ///< [IN] List of objects to be released.
)
{
    le_dls_Link_t* linkPtr;

    while ((linkPtr = le_dls_Pop(importListPtr)) != NULL)
    {
        le_mem_Release(CONTAINER_OF(linkPtr, ImportObjListEntry_t, link));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the source path for importing into a sandbox for the import directive at the current node in
 * the config iterator.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetImportSrcPath
(
    app_Ref_t appRef,                   ///< [IN] Reference to the application object.
    le_cfg_IteratorRef_t cfgIter,       ///< [IN] Config iterator for the import.
    char* bufPtr,                       ///< [OUT] Buffer to store the source path.
    size_t bufSize                      ///< [IN] Size of the buffer.
)
{
    char srcPath[LIMIT_MAX_PATH_BYTES] = "";

    if (le_cfg_GetString(cfgIter, "src", srcPath, sizeof(srcPath), "") != LE_OK)
    {
        LE_ERROR("Source file path '%s...' for app '%s' is too long.", srcPath, app_GetName(appRef));
        return LE_FAULT;
    }

    if (strlen(srcPath) == 0)
    {
        LE_ERROR("Empty source file path supplied for app %s.", app_GetName(appRef));
        return LE_FAULT;
    }

    if (srcPath[0] == '/')
    {
        // The source path is an absolute path so just copy it to the user's buffer.
        if (le_utf8_Copy(bufPtr, srcPath, bufSize, NULL) != LE_OK)
        {
            LE_ERROR("Source file path '%s...' for app '%s' is too long.", srcPath, app_GetName(appRef));
            return LE_FAULT;
        }
    }
    else
    {
        // The source file path is relative to the app install directory.
        // Convert it to a different absolute path depending on whether or not it is writeable.
        bufPtr[0] = '\0';
        if (le_cfg_GetBool(cfgIter, "isWritable", false))
        {
            if (LE_OK != le_path_Concat("/",
                                        bufPtr,
                                        bufSize,
                                        app_GetWriteableFilesDirPath(appRef),
                                        srcPath,
                                        NULL) )
            {
                LE_ERROR("Import source path '%s' for app '%s' is too long.", bufPtr, app_GetName(appRef));
                return LE_FAULT;
            }
        }
        else
        {
            if (LE_OK != le_path_Concat("/",
                                        bufPtr,
                                        bufSize,
                                        app_GetInstallDirPath(appRef),
                                        "read-only",
                                        srcPath,
                                        NULL) )
            {
                LE_ERROR("Import source path '%s' for app '%s' is too long.", bufPtr, app_GetName(appRef));
                return LE_FAULT;
            }
        }
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the destination path for importing into a sandbox for the import directive at the current
 * node in the config iterator.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetImportDestPath
(
    const char* appNamePtr,             ///< [IN] App name.
    le_cfg_IteratorRef_t cfgIter,       ///< [IN] Config iterator for the import.
    char* bufPtr,                       ///< [OUT] Buffer to store the path.
    size_t bufSize                      ///< [IN] Size of the buffer.
)
{
    if (le_cfg_GetString(cfgIter, "dest", bufPtr, bufSize, "") != LE_OK)
    {
        LE_ERROR("Destination path '%s...' for app '%s' is too long.", bufPtr, appNamePtr);
        return LE_FAULT;
    }

    if (bufPtr[0] == '\0')
    {
        LE_ERROR("Empty dest path supplied for app %s.", appNamePtr);
        return LE_FAULT;
    }

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Get a list of default import objects
 *
 * @return
 *      Array of default import objects
 */
//--------------------------------------------------------------------------------------------------
static void GetDefaultImportObjs
(
    app_Ref_t appRef,                   ///< [IN] App ref
    le_dls_List_t * importListPtr,      ///< [IN/OUT] Import list to populate
    const ImportObj_t * importObjsPtr,  ///< [IN] Array of imports to add
    size_t importObjsLen                ///< [IN] Number of elements in array
)
{
    int i;
    for (i = 0; i < importObjsLen; i++)
    {
        if (AddToImportList(importListPtr,
                            appRef,
                            importObjsPtr[i].src,
                            importObjsPtr[i].dest)
            != LE_OK)
        {
            LE_FATAL("Invalid basic import list !");
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Add to the import list from files/dirs from the config.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t AddCfgToImportList
(
    app_Ref_t appRef,                   ///< [IN] Application reference.
    le_cfg_IteratorRef_t appCfg,        ///< [IN] Iterator that points to the list of files/dirs to
                                        ///       import.  The iterator will be returned to its
                                        ///       original position when the function returns.
    le_dls_List_t* importListPtr        ///< [IN] List of imports to add to.
)
{
    if (le_cfg_GoToFirstChild(appCfg) == LE_OK)
    {
        do
        {
            // Get source path.
            char srcPath[LIMIT_MAX_PATH_BYTES];
            if (GetImportSrcPath(appRef, appCfg, srcPath, sizeof(srcPath)) != LE_OK)
            {
                return LE_FAULT;
            }

            // Get destination path.
            char destPath[LIMIT_MAX_PATH_BYTES];
            if (GetImportDestPath(app_GetName(appRef), appCfg, destPath, sizeof(destPath)) != LE_OK)
            {
                return LE_FAULT;
            }

            // Add to the list of things to import into the sandbox.
            if (AddToImportList(importListPtr, appRef, srcPath, destPath) != LE_OK)
            {
                return LE_FAULT;
            }
        }
        while (le_cfg_GoToNextSibling(appCfg) == LE_OK);

        le_cfg_GoToParent(appCfg);
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Import all needed files to the application sandbox.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ImportAllFiles
(
    app_Ref_t appRef,               ///< [IN] Application reference.
    const char* smackLabelPtr       ///< [IN] SMACK label to use for any created directories.
)
{
    le_result_t result = LE_FAULT;
    const char* sandboxPath = app_GetSandboxPath(appRef);
    le_dls_List_t importList = LE_DLS_LIST_INIT;    // List of things to be imported to the sandbox.

    // First, add the default files to the list of things to import.
    // If we add these first, then they can be overridden by the application.
    GetDefaultImportObjs(appRef, &importList, DefaultImportObjs, NUM_ARRAY_MEMBERS(DefaultImportObjs));

    // Also import system specfic common imports.
    GetDefaultImportObjs(appRef, &importList, DefaultSystemImportObjs, NUM_ARRAY_MEMBERS(DefaultSystemImportObjs));

    // Add the app's bin directory, if it exists.
    char pathBuff[LIMIT_MAX_PATH_BYTES] = "";
    if (le_path_Concat("/", pathBuff, sizeof(pathBuff), app_GetInstallDirPath(appRef), "read-only/bin", NULL)
        != LE_OK)
    {
        LE_FATAL("App's install dir path too long!");
    }

    if (le_dir_IsDir(pathBuff))
    {
        if (AddToImportList(&importList,
                            appRef,
                            pathBuff,
                            "/")
            != LE_OK)
        {
            LE_FATAL("Failed to import app's own bin directory!");
        }
    }

    // Add the app's lib directory, if it exists.
    pathBuff[0] = '\0';
    if (le_path_Concat("/", pathBuff, sizeof(pathBuff), app_GetInstallDirPath(appRef), "read-only/lib", NULL)
        != LE_OK)
    {
        LE_FATAL("App's install dir path too long!");
    }

    if (le_dir_IsDir(pathBuff))
    {
        if (AddToImportList(&importList,
                            appRef,
                            pathBuff,
                            "/")
            != LE_OK)
        {
            LE_FATAL("Failed to import app's own lib directory!");
        }
    }

    // Create an iterator for our app.
    le_cfg_IteratorRef_t appCfg = le_cfg_CreateReadTxn(app_GetConfigPath(appRef));

    // Add the bundled dirs.
    le_cfg_GoToNode(appCfg, CFG_NODE_BUNDLES);
    le_cfg_GoToNode(appCfg, CFG_NODE_DIRS);

    if (AddCfgToImportList(appRef, appCfg, &importList) != LE_OK)
    {
        goto done;
    }

    // Add the bundled files.
    le_cfg_GoToParent(appCfg);
    le_cfg_GoToNode(appCfg, CFG_NODE_FILES);

    if (AddCfgToImportList(appRef, appCfg, &importList) != LE_OK)
    {
        goto done;
    }

    // Add the required dirs.
    le_cfg_GoToParent(appCfg);
    le_cfg_GoToParent(appCfg);
    le_cfg_GoToNode(appCfg, CFG_NODE_REQUIRES);
    le_cfg_GoToNode(appCfg, CFG_NODE_DIRS);

    if (AddCfgToImportList(appRef, appCfg, &importList) != LE_OK)
    {
        goto done;
    }

    // Add the required files.
    le_cfg_GoToParent(appCfg);
    le_cfg_GoToNode(appCfg, CFG_NODE_FILES);

    if (AddCfgToImportList(appRef, appCfg, &importList) != LE_OK)
    {
        goto done;
    }

    // Add the devices.
    le_cfg_GoToParent(appCfg);
    le_cfg_GoToNode(appCfg, CFG_NODE_DEVICES);

    if (AddCfgToImportList(appRef, appCfg, &importList) != LE_OK)
    {
        goto done;
    }

    result = Import(&importList, sandboxPath, smackLabelPtr);

done:

    le_cfg_CancelTxn(appCfg);
    ReleaseImportList(&importList);
    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets up a local file system for the application's sandbox.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t SetupFileSystem
(
    app_Ref_t appRef,               ///< [IN] The application to setup the sandbox for.
    const char* smackLabelPtr       ///< [IN] SMACK label to use for the created file system.
)
{
    const char* sandboxPath = app_GetSandboxPath(appRef);

    int fileSysLimit = (int)resLim_GetSandboxedAppTmpfsLimit(appRef);

    // Make the mount options.
    char opt[LIMIT_MAX_APP_NAME_BYTES*2 + 100];
    if (snprintf(opt, sizeof(opt), "size=%d,mode=%.4o,uid=%d,gid=%d,smackfsdef=%s,smackfsroot=%s",
                 (int)fileSysLimit, S_IRWXO, 0, 0,
                 smackLabelPtr, smackLabelPtr) >= sizeof(opt))
    {
        LE_ERROR("Mount options string is too long. '%s'", opt);

        return LE_FAULT;
    }

    // Mount the tmpfs for the sandbox.
    if (mount("none", sandboxPath, "tmpfs", MS_NOSUID, opt) == -1)
    {
        LE_ERROR("Could not create mount for sandbox '%s'.  %m.", app_GetName(appRef));

        return LE_FAULT;
    }

    return smack_SetLabel(sandboxPath, smackLabelPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Makes the application's sandbox root directory.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t MakeAppSandboxDir
(
    app_Ref_t appRef,               ///< [IN] The application.
    const char* smackLabelPtr       ///< [IN] SMACK label to use for the created directory.
)
{
    const char* appName = app_GetName(appRef);
    const char* sandboxPath = app_GetSandboxPath(appRef);

    // Make the directory
    le_result_t r = dir_MakeSmack(sandboxPath, S_IRWXO, smackLabelPtr);

    if (r == LE_FAULT)
    {
        return LE_FAULT;
    }

    if (r == LE_DUPLICATE)
    {
        // If the sandbox already exists then this was probably some garbage left over from a
        // previous creation of this sandbox.  Attempt to delete the sandbox first and then recreate
        // it.
        LE_WARN("Sandbox for application '%s' already exists.  Attempting to delete it and recreate it.",
                appName);
        sandbox_Remove(appRef);

        if (dir_MakeSmack(sandboxPath, S_IRWXU, smackLabelPtr) != LE_OK)
        {
            return LE_FAULT;
        }
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Makes the application's sandbox root directory.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t MakeAppTmpDir
(
    app_Ref_t appRef,               ///< [IN] The application.
    const char* smackLabelPtr       ///< [IN] SMACK label to use for the created directory.
)
{
    const char* sandboxPath = app_GetSandboxPath(appRef);

    // Create /tmp folder in the sandbox.
    char tmpPath[LIMIT_MAX_PATH_BYTES];

    if (snprintf(tmpPath, sizeof(tmpPath), "%s/tmp", sandboxPath) >= sizeof(tmpPath))
    {
        LE_ERROR("Path '%s' is too long.", tmpPath);
        return LE_FAULT;
    }

    return dir_MakeSmack(tmpPath, S_IRWXO, smackLabelPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets up an application's sandbox.  This function looks at the settings in the config tree and
 * sets up the application's sandbox area.
 *
 *  - Creates the sandbox directory.
 *  - Imports all needed files (libraries, executables, config files, socket files, device files).
 *  - Import syslog socket.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t sandbox_Setup
(
    app_Ref_t appRef                ///< [IN] The application to setup the sandbox for.
)
{
    // Make the sandboxes directory.
    if (le_dir_Make(SANDBOXES_DIR, S_IRWXU | S_IROTH | S_IXOTH ) == LE_FAULT)
    {
        return LE_FAULT;
    }

    // Get the SMACK label for the folders we create.
    char appDirLabel[LIMIT_MAX_SMACK_LABEL_BYTES];
    appSmack_GetAccessLabel(app_GetName(appRef),
                            APPSMACK_ACCESS_FLAG_READ |
                            APPSMACK_ACCESS_FLAG_WRITE |
                            APPSMACK_ACCESS_FLAG_EXECUTE,
                            appDirLabel,
                            sizeof(appDirLabel));

    // Make the app's sandbox directories and file system.
    if ( (MakeAppSandboxDir(appRef, appDirLabel) != LE_OK) ||
         (SetupFileSystem(appRef, appDirLabel) != LE_OK)  ||
         (MakeAppTmpDir(appRef, appDirLabel) != LE_OK) ||
         (ImportAllFiles(appRef, appDirLabel) != LE_OK) )
    {
        // Clean up the sandbox if there was an error creating it.
        sandbox_Remove(appRef);
        return LE_FAULT;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Modifies a string that contains a path in the first portion of the string by terminating the
 * string at the end of the path, truncating the string to only contain the path.
 */
//--------------------------------------------------------------------------------------------------
static void TruncateToPath
(
    char* str
)
{
    char* endPtr;

    if (str[0] == '"')
    {
        // The path is quoted so terminate the string at the next quote.
        endPtr = strchr(str + 1, '"');

        if (endPtr != NULL)
        {
            *(endPtr + 1) = '\0';
        }
    }
    else
    {
        // Find the first whitespace character.
        int i = 0;

        while (str[i] != '\0')
        {
            if (isspace(str[i]) != 0)
            {
                // Terminate the string at the first whitespace character.
                str[i] = '\0';
            }

            i++;
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Removes an application's sandbox.  Deletes everything in the sandbox area and the sandbox itself.
 * All processes in the sandbox must be killed prior to calling this function.
 *
 * @return
 *      LE_OK if successful.
 *      LE_FAULT if there was an error.
 */
//--------------------------------------------------------------------------------------------------
le_result_t sandbox_Remove
(
    app_Ref_t appRef                ///< [IN] The application to remove the sandbox for.
)
{
#define MAX_NUM_UNMNT_LOOPS         20

    const char* appName = app_GetName(appRef);
    const char* sandboxPath = app_GetSandboxPath(appRef);

    // Get a sandbox path with a '/' at the end that we can use for comparison when searching for
    // mount points in our sandbox.
    char sandboxPathRoot[LIMIT_MAX_PATH_BYTES];

    if (snprintf(sandboxPathRoot, sizeof(sandboxPathRoot), "%s/", sandboxPath) >= sizeof(sandboxPathRoot))
    {
        LE_ERROR("Sandbox path '%s' is too long.", sandboxPath);
        return LE_FAULT;
    }

    // Read the mounts and search for the ones in our sandbox.
    struct mntent mntEntry;
    char buf[LIMIT_MAX_MNT_ENTRY_BYTES];

    // Continue to search the mount points until no sandbox entries are found.  We do this because
    // several mounts may have been performed on the same mount point.
    bool foundEntryInSandbox;
    int numUnmountLoops = MAX_NUM_UNMNT_LOOPS;

    do
    {
        // Open /proc/mounts file to check where all the mounts are.  This sets the entry to the top
        // of the file.
        FILE* mntFilePtr = setmntent("/proc/mounts", "r");

        if (mntFilePtr == NULL)
        {
            LE_ERROR("Could not read '/proc/mounts'.");
            return LE_FAULT;
        }

        foundEntryInSandbox = false;

        while (getmntent_r(mntFilePtr, &mntEntry, buf, LIMIT_MAX_MNT_ENTRY_BYTES) != NULL)
        {
            // If neccessary modify the string to only contain the path.
            TruncateToPath(mntEntry.mnt_dir);

            // See if the path contains our sandbox path.  If it does this means that the mount
            // point is in our sandbox and should be unmounted.
            char* sandboxPathPtr = strstr(mntEntry.mnt_dir, sandboxPathRoot);

            if ( (sandboxPathPtr != NULL) &&
                 (strcmp(sandboxPathPtr, sandboxPath) != 0) )  // Do not unmount the sandbox root until after everything else is unmounted.
            {
                foundEntryInSandbox = true;

                // This mount point is in our sandbox so it needs to be unmounted.  This call could
                // fail if the file was previously deleted so we do not consider that an error.
                LE_DEBUG("Unmounting %s", mntEntry.mnt_dir);
                if ( (umount2(mntEntry.mnt_dir, MNT_DETACH) != 0) && (errno != ENOENT) )
                {
                    LE_ERROR("Failed to unmount '%s'.  %m.", mntEntry.mnt_dir);
                    return LE_FAULT;
                }

                // Delete the file.  This call could fail if the mount point still has mounts on it
                // or the file could have been previously deleted so we do not consider these errors.
                if ( (remove(mntEntry.mnt_dir) != 0) && (errno != EBUSY) && (errno != ENOENT) )
                {
                    LE_ERROR("Could not delete file '%s'.  %m.", mntEntry.mnt_dir);
                    return LE_FAULT;
                }
            }
        }

        // Close the file descriptor.
        endmntent(mntFilePtr);

        // Use this loop counter to protect against infinite loops.
        numUnmountLoops--;

    } while ( (foundEntryInSandbox) && (numUnmountLoops > 0) );

    LE_DEBUG("Unmounting %s", sandboxPath);
    if ( (umount2(sandboxPath, MNT_DETACH) != 0) && (errno != ENOENT) && (errno != EINVAL) )
    {
        LE_ERROR("Failed to unmount '%s'.  %m.", sandboxPath);
        return LE_FAULT;
    }

    // Delete apps sandbox directory.
    if (le_dir_RemoveRecursive(sandboxPath) != LE_OK)
    {
        LE_ERROR("Could not delete folder '%s'.  %m.", sandboxPath);
        return LE_FAULT;
    }

    LE_INFO("'%s' sandbox removed.", appName);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Confines the calling process into the sandbox.
 *
 * @note Kills the calling process if there is an error.
 */
//--------------------------------------------------------------------------------------------------
void sandbox_ConfineProc
(
    const char* sandboxRootPtr, ///< [IN] Path to the sandbox root.
    uid_t uid,                  ///< [IN] The user ID the process should be set to.
    gid_t gid,                  ///< [IN] The group ID the process should be set to.
    const gid_t* groupsPtr,     ///< [IN] List of supplementary groups for this process.
    size_t numGroups,           ///< [IN] The number of groups in the supplementary groups list.
    const char* workingDirPtr   ///< [IN] The directory the process's working directory will be set
                                ///       to relative to the sandbox root.
)
{
    // @Note: The order of the following statements is important and should not be changed carelessly.

    // Change working directory.
    char homeDir[LIMIT_MAX_PATH_BYTES];
    size_t s;

    if (workingDirPtr[0] == '/')
    {
        s = snprintf(homeDir, sizeof(homeDir), "%s%s", sandboxRootPtr, workingDirPtr);
    }
    else
    {
        s = snprintf(homeDir, sizeof(homeDir), "%s/%s", sandboxRootPtr, workingDirPtr);
    }

    if (s >= sizeof(homeDir))
    {
        LE_FATAL("Working directory is too long: '%s'", homeDir);
    }

    LE_FATAL_IF(chdir(homeDir) != 0, "Could not change working directory to '%s'.  %m", homeDir);

    // Chroot to the sandbox.
    LE_FATAL_IF(chroot(sandboxRootPtr) != 0, "Could not chroot to '%s'.  %m", sandboxRootPtr);

    // Clear our supplementary groups list.
    LE_FATAL_IF(setgroups(0, NULL) == -1, "Could not set the supplementary groups list.  %m.");

    // Populate our supplementary groups list with the provided list.
    LE_FATAL_IF(setgroups(numGroups, groupsPtr) == -1,
                "Could not set the supplementary groups list.  %m.");

    // Set our process's primary group ID.
    LE_FATAL_IF(setgid(gid) == -1, "Could not set the group ID.  %m.");

    // Set our process's user ID.  This sets all of our user IDs (real, effective, saved).  This
    // call also clears all cababilities.  This function in particular MUST be called after all
    // the previous system calls because once we make this call we will lose root priviledges.
    LE_FATAL_IF(setuid(uid) == -1, "Could not set the user ID.  %m.");
}

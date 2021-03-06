/** @file appCtrl.c
 *
 * Control Legato applications.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */

#include "legato.h"
#include "interfaces.h"


//--------------------------------------------------------------------------------------------------
/**
 * Prototype for command handler functions.
 */
//--------------------------------------------------------------------------------------------------
typedef void (*cmdHandlerFunc_t)
(
    void
);


//--------------------------------------------------------------------------------------------------
/**
 * The command handler function.
 */
//--------------------------------------------------------------------------------------------------
static cmdHandlerFunc_t CommandHandler = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * The path specified on the command line.
 */
//--------------------------------------------------------------------------------------------------
static char Path[SECSTOREADMIN_MAX_PATH_SIZE] = "/";


//--------------------------------------------------------------------------------------------------
/**
 * The input file specified on the command line.
 */
//--------------------------------------------------------------------------------------------------
static const char* InputFilePtr = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Flag to indicate whether the size of the entry should be listed.
 */
//--------------------------------------------------------------------------------------------------
static bool ListSizeFlag = false;


//--------------------------------------------------------------------------------------------------
/**
 * Prints help to stdout.
 */
//--------------------------------------------------------------------------------------------------
static void PrintHelp
(
    void
)
{
    puts(
        "NAME:\n"
        "    secstore - Used to perform administrative functions on secure storage.\n"
        "\n"
        "DESCRIPTION:\n"
        "    secstore ls [OPTIONS] <path>\n"
        "       List all the secure storage entries under <path>.  <path> is assumed to be absolute.\n"
        "\n"
        "       OPTIONS\n"
        "           -s  Include the size of each entry.\n"
        "\n"
        "    secstore read <path>\n"
        "       Reads the item specified by <path>.  <path> is assumed to be absolute and must not\n"
        "       end with a separator '/'.\n"
        "\n"
        "    secstore write <inputFile> <path>\n"
        "       Writes the data from <inputFile> into the item specified by <path>.  <path> is\n"
        "       assumed to be absolute and must not end with a separator '/'.  Writing will stop once the end of\n"
        "       the <inputFile> is reached or the maximum secure storage item size is reached.\n"
        "       Note that this write will not respect an application's secure storage limit.\n"
        "\n"
        "    secstore rm <path>\n"
        "       Deletes <path> and all items under it.  <path> is assumed to be absolute.\n"
        "\n"
        "    secstore size <path>\n"
        "       Gets the size of all items under <path>.  <path> is assumed to be absolute.\n"
        "\n"
        "    secstore total\n"
        "       Gets the total space and free space, in bytes, for all of secure storage.\n"
        "\n"
        );

    exit(EXIT_FAILURE);
}


//--------------------------------------------------------------------------------------------------
/**
 * Prints a generic message on stderr so that the user is aware there is a problem, logs the
 * internal error message and exit.
 */
//--------------------------------------------------------------------------------------------------
#define INTERNAL_ERR(formatString, ...)                                                 \
            { fprintf(stderr, "Internal error check logs for details.\n");              \
              LE_FATAL(formatString, ##__VA_ARGS__); }


//--------------------------------------------------------------------------------------------------
/**
 * If the condition is true, print a generic message on stderr so that the user is aware there is a
 * problem, log the internal error message and exit.
 */
//--------------------------------------------------------------------------------------------------
#define INTERNAL_ERR_IF(condition, formatString, ...)                                   \
        if (condition) { INTERNAL_ERR(formatString, ##__VA_ARGS__); }


//--------------------------------------------------------------------------------------------------
/**
 * List entries.
 */
//--------------------------------------------------------------------------------------------------
static void ListEntries
(
    void
)
{
    // Iterate over the Path and print the values to stdout.
    secStoreAdmin_IterRef_t iterRef = secStoreAdmin_CreateIter(Path);

    if (iterRef != NULL)
    {
        while (secStoreAdmin_Next(iterRef) == LE_OK)
        {
            bool isDir;
            char entryName[SECSTOREADMIN_MAX_PATH_SIZE];

            if (secStoreAdmin_GetEntry(iterRef, entryName, sizeof(entryName), &isDir) == LE_OK)
            {
                char sizeStr[100] = "";

                // See if we have to print the entry size.
                if (ListSizeFlag)
                {
                    // Get the entry size.
                    char fullPath[SECSTOREADMIN_MAX_PATH_SIZE] = "";

                    if (le_path_Concat("/", fullPath, sizeof(fullPath), Path, entryName, NULL) != LE_OK)
                    {
                        INTERNAL_ERR("Secure storage path for entry '%s' is too long.", entryName);
                    }

                    uint64_t size;
                    le_result_t r = secStoreAdmin_GetSize(fullPath, &size);
                    if (r != LE_OK)
                    {
                        LE_ERROR("Could not get size for secure storage item '%s'.  Result code %s.",
                                 fullPath, LE_RESULT_TXT(r));

                        snprintf(sizeStr, sizeof(sizeStr), "%s", "unknown");
                    }
                    else
                    {
                        snprintf(sizeStr, sizeof(sizeStr), "%" PRIu64, size);
                    }

                    printf("%-12s %s", sizeStr, entryName);
                }
                else
                {
                    printf("%s", entryName);
                }

                if (isDir)
                {
                    printf("/\n");
                }
                else
                {
                    printf("\n");
                }
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        fprintf(stderr, "Could not list entries.  Path may be malformed.\n");
        exit(EXIT_FAILURE);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print entry value.
 */
//--------------------------------------------------------------------------------------------------
static void PrintEntry
(
    void
)
{
    // Check path.
    if (Path[strlen(Path)-1] == '/')
    {
        fprintf(stderr, "Path must not end with a separator.\n");
        exit(EXIT_FAILURE);
    }

    // Read entry.
    uint8_t buf[LE_SECSTORE_MAX_ITEM_SIZE];
    size_t bufSize = sizeof(buf);

    le_result_t result = secStoreAdmin_Read(Path, buf, &bufSize);

    // Print entry.
    if (result == LE_OK)
    {
        printf("%.*s\n", (int)bufSize, (char*)buf);
    }
    else if (result == LE_NOT_FOUND)
    {
        fprintf(stderr, "Entry %s not found.\n", Path);
        exit(EXIT_FAILURE);
    }
    else
    {
        INTERNAL_ERR("Could not read item %s.  Result code %s.", Path, LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Write entry value into secure storage.
 */
//--------------------------------------------------------------------------------------------------
static void WriteEntry
(
    void
)
{
    // Check path.
    if (Path[strlen(Path)-1] == '/')
    {
        fprintf(stderr, "Path must not end with a separator.\n");
        exit(EXIT_FAILURE);
    }

    // Open input file.
    int fd;

    do
    {
        fd = open(InputFilePtr, O_RDONLY);
    }
    while ( (fd == -1) && (errno == EINTR) );

    if (fd == -1)
    {
        fprintf(stderr, "Could not open file %s.  %m.\n", Path);
        exit(EXIT_FAILURE);
    }

    // Read the contents of the input file.
    uint8_t buf[LE_SECSTORE_MAX_ITEM_SIZE];
    ssize_t numBytes;

    do
    {
        numBytes = read(fd, buf, sizeof(buf));
    }
    while ( (numBytes == -1) && (errno == EINTR) );

    if (numBytes > 0)
    {
        // Write the buffer to secure storage.
        le_result_t result = secStoreAdmin_Write(Path, buf, numBytes);

        if (result == LE_NO_MEMORY)
        {
            fprintf(stderr, "Out of secure storage space.\n");
            exit(EXIT_FAILURE);
        }
        else if (result == LE_BAD_PARAMETER)
        {
            fprintf(stderr, "Cannot write to the specified path.\n");
            exit(EXIT_FAILURE);
        }
        else if (result != LE_OK)
        {
            INTERNAL_ERR("Could not write to item %s.  Result code %s.",
                         Path, LE_RESULT_TXT(result));
        }
    }
    else if (numBytes == -1)
    {
        fprintf(stderr, "Could not read from %s.  %m.\n", InputFilePtr);
        exit(EXIT_FAILURE);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Recursively deletes a secure storage path.
 */
//--------------------------------------------------------------------------------------------------
static void DeletePath
(
    void
)
{
    le_result_t result = secStoreAdmin_Delete(Path);

    if (result == LE_NOT_FOUND)
    {
        fprintf(stderr, "Entry %s not found.\n", Path);
        exit(EXIT_FAILURE);
    }
    else if (result != LE_OK)
    {
        INTERNAL_ERR("Could not delete path %s.  Result code %s.", Path, LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Prints the total size of all entries under a secure storage path.
 */
//--------------------------------------------------------------------------------------------------
static void PrintSize
(
    void
)
{
    uint64_t size;

    le_result_t result = secStoreAdmin_GetSize(Path, &size);

    if (result == LE_OK)
    {
        printf("%" PRIu64 "\n", size);
    }
    else if (result == LE_NOT_FOUND)
    {
        fprintf(stderr, "Path %s not found.\n", Path);
        exit(EXIT_FAILURE);
    }
    else
    {
        INTERNAL_ERR("Could not get size for path %s.  Result code %s.",
                     Path,
                     LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Prints the total and free space in secure storage.
 */
//--------------------------------------------------------------------------------------------------
static void PrintTotalSizes
(
    void
)
{
    uint64_t totalSize, freeSize;

    le_result_t result = secStoreAdmin_GetTotalSpace(&totalSize, &freeSize);

    if (result == LE_OK)
    {
        printf("Total %" PRIu64 "\n", totalSize);
        printf("Free %" PRIu64 "\n", freeSize);
    }
    else
    {
        INTERNAL_ERR("Could not get available secure storage space.  Result code %s.",
                     LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets the path specified on the command-line.
 */
//--------------------------------------------------------------------------------------------------
static void SetPath
(
    const char* argPtr                  ///< [IN] Command-line argument.
)
{
    Path[0] = '/';

    if (le_path_Concat("/", Path, sizeof(Path), argPtr, NULL) != LE_OK)
    {
        fprintf(stderr, "Path is too long.\n");
        exit(EXIT_FAILURE);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets the input file specified on the command-line.
 */
//--------------------------------------------------------------------------------------------------
static void SetInputFile
(
    const char* argPtr                  ///< [IN] Command-line argument.
)
{
    InputFilePtr = argPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Sets the command handler to call depending on which command was specified on the command-line.
 */
//--------------------------------------------------------------------------------------------------
static void SetCommandHandler
(
    const char* argPtr                  ///< [IN] Command-line argument.
)
{
    if (strcmp(argPtr, "ls") == 0)
    {
        CommandHandler = ListEntries;
        le_arg_AddPositionalCallback(SetPath);
        le_arg_SetFlagVar(&ListSizeFlag, "s", NULL);
        le_arg_AllowLessPositionalArgsThanCallbacks();
    }
    else if (strcmp(argPtr, "read") == 0)
    {
        CommandHandler = PrintEntry;
        le_arg_AddPositionalCallback(SetPath);
    }
    else if (strcmp(argPtr, "write") == 0)
    {
        CommandHandler = WriteEntry;
        le_arg_AddPositionalCallback(SetInputFile);
        le_arg_AddPositionalCallback(SetPath);
    }
    else if (strcmp(argPtr, "rm") == 0)
    {
        CommandHandler = DeletePath;
        le_arg_AddPositionalCallback(SetPath);
    }
    else if (strcmp(argPtr, "size") == 0)
    {
        CommandHandler = PrintSize;
        le_arg_AddPositionalCallback(SetPath);
        le_arg_AllowLessPositionalArgsThanCallbacks();
    }
    else if (strcmp(argPtr, "total") == 0)
    {
        CommandHandler = PrintTotalSizes;
    }
    else
    {
        fprintf(stderr, "Unknown command.\n");
        exit(EXIT_FAILURE);
    }
}


//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    // Setup command-line argument handling.
    le_arg_SetFlagCallback(PrintHelp, "h", "help");

    le_arg_AddPositionalCallback(SetCommandHandler);

    le_arg_Scan();

    // Call the actual command handler.
    CommandHandler();

    exit(EXIT_FAILURE);
}

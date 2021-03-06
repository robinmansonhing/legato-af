//--------------------------------------------------------------------------------------------------
/**
 * @file mkexe.cpp  Implements the "mkexe" functionality of the "mk" tool.
 *
 * Run 'mkexe --help' for command-line options and usage help.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 */
//--------------------------------------------------------------------------------------------------

#include <iostream>

#include "mkTools.h"
#include "commandLineInterpreter.h"


namespace cli
{


/// Object that stores build parameters that we gather.
static mk::BuildParams_t BuildParams;

/// List of names of content items (specified on the command line) that are to be
/// included in this executable.   These could be source file names, component names,
/// or library names.
static std::list<std::string> ContentNames;

/// Path to the executable to be built.
static std::string ExePath;

/// Object representing the executable we are building.
static model::Exe_t* ExePtr = NULL;

// true if the build.ninja file should be ignored and everything should be regenerated, including
// a new build.ninja.
static bool DontRunNinja = false;


//--------------------------------------------------------------------------------------------------
/**
 * Parse the command-line arguments and update the static operating parameters variables.
 *
 * Throws a std::runtime_error exception on failure.
 **/
//--------------------------------------------------------------------------------------------------
static void GetCommandLineArgs
(
    int argc,
    const char** argv
)
//--------------------------------------------------------------------------------------------------
{
    // Lambda function that gets called once for each occurence of the --cflags (or -C)
    // option on the command line.
    auto cFlagsPush = [&](const char* arg)
        {
            BuildParams.cFlags += " ";
            BuildParams.cFlags += arg;
        };

    // Lambda function that gets called for each occurence of the --cxxflags, (or -X) option on
    // the command line.
    auto cxxFlagsPush = [&](const char* arg)
        {
            BuildParams.cxxFlags += " ";
            BuildParams.cxxFlags += arg;
        };

    // Lambda function that gets called once for each occurence of the --ldflags (or -L)
    // option on the command line.
    auto ldFlagsPush = [&](const char* arg)
        {
            BuildParams.ldFlags += " ";
            BuildParams.ldFlags += arg;
        };

    // Lambda function that gets called once for each occurence of the --interface-search (or -i)
    // option on the command line.
    auto interfaceDirPush = [&](const char* path)
        {
            BuildParams.interfaceDirs.push_back(path);
        };

    // Lambda function that gets called once for each occurence of the --source-search (or -s)
    // option on the command line.
    auto sourceDirPush = [&](const char* path)
        {
            BuildParams.sourceDirs.push_back(path);
        };

    // Lambda function that gets called once for each content item on the command line.
    auto contentPush = [&](const char* param)
        {
            ContentNames.push_back(param);
        };

    // Register all the command-line options with the argument parser.
    args::AddString(&ExePath,
                    'o',
                    "output",
                    "The path of the executable file to generate.");

    args::AddOptionalString(&BuildParams.libOutputDir,
                            ".",
                            'l',
                            "lib-output-dir",
                            "Specify the directory into which any generated runtime libraries"
                            " should be put.");

    args::AddOptionalString(&BuildParams.workingDir,
                            "./_build",
                            'w',
                            "object-dir",
                            "Specify the directory into which any intermediate build artifacts"
                            " (such as .o files and generated source code files) should be put.");

    args::AddOptionalString(&BuildParams.target,
                            "localhost",
                            't',
                            "target",
                            "Specify the target device to build for (localhost | ar7).");

    args::AddMultipleString('i',
                            "interface-search",
                            "Add a directory to the interface search path.",
                            interfaceDirPush);

    args::AddMultipleString('c',
                            "component-search",
                            "(DEPRECATED) Add a directory to the source search path (same as -s).",
                            sourceDirPush);

    args::AddMultipleString('s',
                            "source-search",
                            "Add a directory to the source search path.",
                            sourceDirPush);

    args::AddOptionalFlag(&BuildParams.beVerbose,
                          'v',
                          "verbose",
                          "Set into verbose mode for extra diagnostic information.");

    args::AddMultipleString('C',
                            "cflags",
                            "Specify extra flags to be passed to the C compiler.",
                            cFlagsPush);

    args::AddMultipleString('X',
                            "cxxflags",
                            "Specify extra flags to be passed to the C++ compiler.",
                            cxxFlagsPush);

    args::AddMultipleString('L',
                            "ldflags",
                            "Specify extra flags to be passed to the linker when linking "
                            "executables.",
                            ldFlagsPush);


    args::AddOptionalFlag(&DontRunNinja,
                           'n',
                           "dont-run-ninja",
                           "Even if a build.ninja file exists, ignore it, parse all inputs, and"
                           " generate all output files, including a new copy of the build.ninja,"
                           " then exit without running ninja.  This is used by the build.ninja to"
                           " to regenerate itself and any other files that need to be regenerated"
                           " when the build.ninja finds itself out of date.");

    args::AddOptionalFlag(&BuildParams.codeGenOnly,
                          'g',
                          "generate-code",
                          "Only generate code, but don't compile or link anything."
                          " The interface definition (include) files will be generated, along"
                          " with component and executable main files."
                          " This is useful for supporting context-sensitive auto-complete and"
                          " related features in source code editors, for example.");

    // Any remaining parameters on the command-line are treated as content items to be included
    // in the executable.
    args::SetLooseArgHandler(contentPush);

    // Scan the arguments now.
    args::Scan(argc, argv);

    // Add the current working directory to the list of source search directories and the
    // list of interface search directories.
    BuildParams.sourceDirs.push_back(".");
    BuildParams.interfaceDirs.push_back(".");

    // Make the ExePath absolute, if it isn't already.
    ExePath = path::MakeAbsolute(ExePath);
}


//--------------------------------------------------------------------------------------------------
/**
 * Parse a component's .cdef, construct a conceptual model for the component and add it to
 * the exe.
 */
//--------------------------------------------------------------------------------------------------
static void AddComponentToExe
(
    const std::string& componentPath
)
//--------------------------------------------------------------------------------------------------
{
    // Get the component object.
    model::Component_t* componentPtr = modeller::GetComponent(componentPath, BuildParams);

    // Add an instance of the component to the executable.
    modeller::AddComponentInstance(ExePtr, componentPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove the executable name and component name parts from the service instance names of all
 * IPC API interfaces (both client and server).
 */
//--------------------------------------------------------------------------------------------------
static void MakeAllInterfacesExternal
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    for (auto componentInstancePtr : ExePtr->componentInstances)
    {
        for (auto ifInstancePtr : componentInstancePtr->clientApis)
        {
            ifInstancePtr->name = ifInstancePtr->ifPtr->internalName;
        }
        for (auto ifInstancePtr : componentInstancePtr->serverApis)
        {
            ifInstancePtr->name = ifInstancePtr->ifPtr->internalName;
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Check that there's at least one source code file in the executable.
 *
 * @throw mk::Exception_t if there are no source code files in the executable.
 */
//--------------------------------------------------------------------------------------------------
static void VerifyAtLeastOneSourceFile
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    // Check for C or C++ source files being built directly into the exe (outside of components).
    if (ExePtr->cObjectFiles.empty() && ExePtr->cxxObjectFiles.empty())
    {
        // Check all the components instantiated in this exe.
        for (auto componentInstancePtr : ExePtr->componentInstances)
        {
            auto componentPtr = componentInstancePtr->componentPtr;

            if (   (componentPtr->cObjectFiles.empty() == false)
                || (componentPtr->cxxObjectFiles.empty() == false)  )
            {
                return;
            }
        }

        throw mk::Exception_t("Executable doesn't contain any source code files.");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Identify content items and construct the object model.
 */
//--------------------------------------------------------------------------------------------------
static void ConstructObjectModel
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    bool errorFound = false;
    std::string componentPath;

    ExePtr = new model::Exe_t(ExePath, NULL, BuildParams.workingDir);

    if (BuildParams.beVerbose)
    {
        std::cout << "Making executable '" << ExePtr->path << "'" << std::endl;
    }

    // For each item of content, we have to figure out what type of content it is and
    // handle it accordingly.
    for (auto contentName: ContentNames)
    {
        // Is it a C source code file path?
        if (path::IsCSource(contentName))
        {
            if (BuildParams.beVerbose)
            {
                std::cout << "Adding C source file '" << contentName << "' to executable."
                          << std::endl;
            }

            auto sourceFilePath = file::FindFile(contentName, BuildParams.sourceDirs);
            if (sourceFilePath == "")
            {
                throw mk::Exception_t("Can't find file: '" + contentName + "'.");
            }
            sourceFilePath = path::MakeAbsolute(sourceFilePath);

            // Compute the relative directory
            auto objFilePath = "obj/" + md5(path::MakeCanonical(sourceFilePath)) + ".o";

            // Create an object file object for this source file.
            auto objFilePtr = new model::ObjectFile_t(objFilePath,
                                                      model::ProgramLang_t::LANG_C,
                                                      sourceFilePath);

            // Add the object file to the exe's list of C object files.
            ExePtr->cObjectFiles.push_back(objFilePtr);
        }
        // Is it a C++ source code file path?
        else if (path::IsCxxSource(contentName))
        {
            if (BuildParams.beVerbose)
            {
                std::cout << "Adding C++ source file '" << contentName << "' to executable."
                          << std::endl;
            }

            auto sourceFilePath = file::FindFile(contentName, BuildParams.sourceDirs);
            if (sourceFilePath == "")
            {
                throw mk::Exception_t("Can't find file: '" + contentName + "'.");
            }
            sourceFilePath = path::MakeAbsolute(sourceFilePath);

            // Compute the relative directory
            auto objFilePath = "obj/" + md5(path::MakeCanonical(sourceFilePath)) + ".o";

            // Create an object file object for this source file.
            auto objFilePtr = new model::ObjectFile_t(objFilePath,
                                                      model::ProgramLang_t::LANG_CXX,
                                                      sourceFilePath);

            // Add the object file to the exe's list of C++ object files.
            ExePtr->cxxObjectFiles.push_back(objFilePtr);
        }
        // Is it a library file path?
        else if (path::IsLibrary(contentName))
        {
            if (BuildParams.beVerbose)
            {
                std::cout << "Adding library '" << contentName << "' to executable." << std::endl;
            }

            BuildParams.ldFlags += " " + contentName;
        }
        // Is it a path to a component directory?
        else if ((componentPath = file::FindComponent(contentName, BuildParams.sourceDirs)) != "")
        {
            componentPath = path::MakeAbsolute(componentPath);

            if (BuildParams.beVerbose)
            {
                std::cout << "Adding component '" << componentPath << "' to executable." << std::endl;
            }

            AddComponentToExe(componentPath);
        }
        // It's none of the above.
        else
        {
            std::cerr << "*** ERROR: Couldn't identify content item '"
                      << contentName << "'." << std::endl;

            std::cerr << "Searched in the following locations:" << std::endl;
            for (auto path : BuildParams.sourceDirs)
            {
                std::cerr << "    " << path << std::endl;
            }

            errorFound = true;
        }
    }

    if (errorFound)
    {
        throw mk::Exception_t("Unable to identify one or more requested content items.");
    }

    // Make all interfaces "external", because the executable is outside of any app.
    // Effectively, this means remove the "exe.component." prefix from the service instance
    // names of all interfaces.
    MakeAllInterfacesExternal();

    // Check that there's at least one source code file in the executable.
    VerifyAtLeastOneSourceFile();
}


//--------------------------------------------------------------------------------------------------
/**
 * Implements the mkexe functionality.
 */
//--------------------------------------------------------------------------------------------------
void MakeExecutable
(
    int argc,           ///< Count of the number of command line parameters.
    const char** argv   ///< Pointer to an array of pointers to command line argument strings.
)
//--------------------------------------------------------------------------------------------------
{
    GetCommandLineArgs(argc, argv);

    // Set the target-specific environment variables (e.g., LEGATO_TARGET).
    envVars::SetTargetSpecific(BuildParams.target);

    // If we have not been asked to ignore any already existing build.ninja, and the command-line
    // arguments and environment variables we were given are the same as last time, just run ninja.
    if (!DontRunNinja)
    {
        if (args::MatchesSaved(BuildParams, argc, argv) && envVars::MatchesSaved(BuildParams))
        {
            RunNinja(BuildParams);
            // NOTE: If build.ninja exists, RunNinja() will not return.  If it doesn't it will.
        }
    }

    ConstructObjectModel();

    // Generate _main.c.
    code::GenerateExeMain(ExePtr, BuildParams);

    // For each component in the executable.
    for (auto componentInstancePtr : ExePtr->componentInstances)
    {
        auto componentPtr = componentInstancePtr->componentPtr;

        // Create a working directory to build the component in.
        file::MakeDir(path::Combine(BuildParams.workingDir, componentPtr->workingDir));

        // Generate a custom "interfaces.h" file for this component.
        code::GenerateInterfacesHeader(componentPtr, BuildParams);

        // Generate a custom "_componentMain.c" file for this component.
        code::GenerateComponentMainFile(componentPtr, BuildParams, false);
    }

    // Generate a build.ninja for the executable.
    ninja::Generate(ExePtr, BuildParams, argc, argv);

    // If we haven't been asked not to run ninja,
    if (!DontRunNinja)
    {
        // Save the command-line arguments and environment variables for future comparison.
        // Note: we don't need to do this if we have been asked not to run ninja, because
        // that only happens when ninja is already running and asking us to regenerate its
        // script for us, and that only happens if we just saved the args and env vars and
        // ran ninja.
        args::Save(BuildParams, argc, argv);
        envVars::Save(BuildParams);

        RunNinja(BuildParams);
    }
}


} // namespace cli

//--------------------------------------------------------------------------------------------------
/**
 * @file sdefParser.cpp  Implementation of the .sdef file parser.
 */
//--------------------------------------------------------------------------------------------------

#include "mkTools.h"


namespace parser
{

namespace sdef
{

namespace internal
{

//--------------------------------------------------------------------------------------------------
/**
 * Parses an entry in an app's override list.
 *
 * @return Pointer to the item.
 */
//--------------------------------------------------------------------------------------------------
static parseTree::CompoundItem_t* ParseAppOverride
(
    Lexer_t& lexer
)
//--------------------------------------------------------------------------------------------------
{
    // App overrides are all inside sections.

    // Pull the section name out of the file.
    auto sectionNameTokenPtr = lexer.Pull(parseTree::Token_t::NAME);

    const std::string& sectionName = sectionNameTokenPtr->text;

    // Branch based on the section name.
    if (   (sectionName == "cpuShare")
        || (sectionName == "maxCoreDumpFileBytes")
        || (sectionName == "maxFileBytes")
        || (sectionName == "maxFileDescriptors")
        || (sectionName == "maxFileSystemBytes")
        || (sectionName == "maxLockedMemoryBytes")
        || (sectionName == "maxMemoryBytes")
        || (sectionName == "maxMQueueBytes")
        || (sectionName == "maxQueuedSignals")
        || (sectionName == "watchdogTimeout")
        || (sectionName == "maxThreads")
        || (sectionName == "maxSecureStorageBytes") )
    {
        return ParseSimpleSection(lexer, sectionNameTokenPtr, parseTree::Token_t::INTEGER);
    }
    else if (sectionName == "faultAction")
    {
        return ParseFaultAction(lexer, sectionNameTokenPtr);
    }
    else if (sectionName == "groups")
    {
        return ParseTokenListSection(lexer, sectionNameTokenPtr, parseTree::Token_t::GROUP_NAME);
    }
    else if (sectionName == "maxPriority")
    {
        return ParsePriority(lexer, sectionNameTokenPtr);
    }
    else if (sectionName == "pools")
    {
        return ParseSimpleNamedItemListSection(lexer,
                                               sectionNameTokenPtr,
                                               parseTree::Content_t::POOL,
                                               parseTree::Token_t::NAME);
    }
    else if (sectionName == "sandboxed")
    {
        return ParseSimpleSection(lexer, sectionNameTokenPtr, parseTree::Token_t::BOOLEAN);
    }
    else if (sectionName == "start")
    {
        return ParseSimpleSection(lexer, sectionNameTokenPtr, parseTree::Token_t::NAME);
    }
    else if (sectionName == "preloaded")
    {
        return ParseSimpleSection(lexer, sectionNameTokenPtr, parseTree::Token_t::BOOLEAN);
    }
    else if (sectionName == "watchdogAction")
    {
        return ParseWatchdogAction(lexer, sectionNameTokenPtr);
    }
    else
    {
        lexer.ThrowException("Unrecognized app override section name '" + sectionName + "'.");
        return NULL;
    }
}



//--------------------------------------------------------------------------------------------------
/**
 * Parses an entry in the "apps:" section in a .sdef file.
 *
 * @return Pointer to the item.
 */
//--------------------------------------------------------------------------------------------------
static parseTree::CompoundItemList_t* ParseApp
(
    Lexer_t& lexer
)
//--------------------------------------------------------------------------------------------------
{
    // Each entry in the apps: subsection can be either just an file path, or it can be
    // a file path followed by overrides inside curlies.
    // Pull the app name out of the file and create a new object for it.
    auto itemPtr = new parseTree::App_t(lexer.Pull(parseTree::Token_t::FILE_PATH));

    SkipWhitespaceAndComments(lexer);

    // If there's a curly next,
    if (lexer.IsMatch(parseTree::Token_t::OPEN_CURLY))
    {
        // Pull the curly out of the token stream.
        (void)lexer.Pull(parseTree::Token_t::OPEN_CURLY);

        SkipWhitespaceAndComments(lexer);

        // Until we find a closing '}', keep parsing overrides.
        while (!lexer.IsMatch(parseTree::Token_t::CLOSE_CURLY))
        {
            if (lexer.IsMatch(parseTree::Token_t::END_OF_FILE))
            {
                std::stringstream msg;
                msg << "Unexpected end-of-file before end of application override list for app '"
                    << itemPtr->firstTokenPtr->text
                    << "' starting at line " << itemPtr->firstTokenPtr->line
                    << " character " << itemPtr->firstTokenPtr->column << ".";
                lexer.ThrowException(msg.str());
            }

            itemPtr->AddContent(ParseAppOverride(lexer));

            SkipWhitespaceAndComments(lexer);
        }

        // Pull out the '}' and make that the last token in the app.
        itemPtr->lastTokenPtr = lexer.Pull(parseTree::Token_t::CLOSE_CURLY);
    }

    return itemPtr;
}



//--------------------------------------------------------------------------------------------------
/**
 * Parse a binding.
 *
 * @return Pointer to the item.
 */
//--------------------------------------------------------------------------------------------------
static parseTree::Binding_t* ParseBinding
(
    Lexer_t& lexer
)
//--------------------------------------------------------------------------------------------------
{
    parseTree::Binding_t* bindingPtr;

    // Client side of the binding must be one of the following forms:
    //      "clientApp.externalInterface
    //      "clientApp.exe.component.internalInterface
    //      "clientApp.*.internalInterface
    //      "<clientUser>.externalInterface

    // The first part is always an IPC Agent token, followed by a '.'.
    bindingPtr = new parseTree::Binding_t(lexer.Pull(parseTree::Token_t::IPC_AGENT));
    (void)lexer.Pull(parseTree::Token_t::DOT);

    // If a '*' comes next, then it's a wildcard binding.
    if (lexer.IsMatch(parseTree::Token_t::STAR))
    {
        // Check that the "IPC agent" is an app.
        if (bindingPtr->firstTokenPtr->text[0] == '<')
        {
            lexer.ThrowException("Wildcard bindings not permitted for non-app users.");
        }

        // Expect a "*.interfaceName" to follow.
        bindingPtr->AddContent(lexer.Pull(parseTree::Token_t::STAR));
        (void)lexer.Pull(parseTree::Token_t::DOT);
        bindingPtr->AddContent(lexer.Pull(parseTree::Token_t::NAME));
    }
    // If a '*' does not come next, expect a name.
    else
    {
        bindingPtr->AddContent(lexer.Pull(parseTree::Token_t::NAME));

        // If the next thing is a '.', then this must be an internal interface binding override.
        // Otherwise, we are done the client-side part.
        if (lexer.IsMatch(parseTree::Token_t::DOT))
        {
            // Check that the "IPC agent" is an app.
            if (bindingPtr->firstTokenPtr->text[0] == '<')
            {
                lexer.ThrowException("Too many parts to client-side interface specification for"
                                     " non-app user '" + bindingPtr->firstTokenPtr->text + "'."
                                     " Can only override internal interface bindings for apps.");
            }

            // For the internal interface binding override, we have already pulled the token
            // for the exe name.  Now we expect ".component.internalInterface".
            (void)lexer.Pull(parseTree::Token_t::DOT);
            bindingPtr->AddContent(lexer.Pull(parseTree::Token_t::NAME));
            (void)lexer.Pull(parseTree::Token_t::DOT);
            bindingPtr->AddContent(lexer.Pull(parseTree::Token_t::NAME));
        }
    }

    // ->
    SkipWhitespaceAndComments(lexer);
    (void)lexer.Pull(parseTree::Token_t::ARROW);
    SkipWhitespaceAndComments(lexer);

    // Server side of the binding must be one of the following forms:
    //      serverApp.externalInterface"
    //      <serverUser>.externalInterface"
    bindingPtr->AddContent(lexer.Pull(parseTree::Token_t::IPC_AGENT));
    (void)lexer.Pull(parseTree::Token_t::DOT);
    // If a '*' comes next, then it's an (illegal) attempt to do a server-side wildcard binding.
    if (lexer.IsMatch(parseTree::Token_t::STAR))
    {
        lexer.ThrowException("Wildcard bindings not permitted for server-side interfaces.");
    }
    bindingPtr->AddContent(lexer.Pull(parseTree::Token_t::NAME));

    // Expect closing curly to end section or whitespace to separate bindings.
    // If there is another '.' here, the user probably is trying to bind to an internal
    // interface on the server side.
    if (lexer.IsMatch(parseTree::Token_t::DOT))
    {
        lexer.ThrowException("Too many parts to server-side interface specification."
                             " Can only bind to external interfaces in .sdef files.");
    }

    return bindingPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Parse a command.
 *
 * @return Pointer to the item.
 */
//--------------------------------------------------------------------------------------------------
static parseTree::Command_t* ParseCommand
(
    Lexer_t& lexer
)
//--------------------------------------------------------------------------------------------------
{
    parseTree::Command_t* commandPtr;

    // The first part is always the command name.  Paths are not allowed.
    commandPtr = new parseTree::Command_t(lexer.Pull(parseTree::Token_t::NAME));

    // '='
    SkipWhitespaceAndComments(lexer);
    (void)lexer.Pull(parseTree::Token_t::EQUALS);
    SkipWhitespaceAndComments(lexer);

    // App name
    commandPtr->AddContent(lexer.Pull(parseTree::Token_t::NAME));

    // ':'
    SkipWhitespaceAndComments(lexer);
    lexer.Pull(parseTree::Token_t::COLON);
    SkipWhitespaceAndComments(lexer);

    // Path to executable within app.
    commandPtr->AddContent(lexer.Pull(parseTree::Token_t::FILE_PATH));

    return commandPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Parses a section in a .sdef file.
 *
 * @return Pointer to the item.
 */
//--------------------------------------------------------------------------------------------------
static parseTree::CompoundItem_t* ParseSection
(
    Lexer_t& lexer
)
//--------------------------------------------------------------------------------------------------
{
    // Create a new Section object for the parse tree.  Pull the section name out of the file.
    auto sectionNameTokenPtr = lexer.Pull(parseTree::Token_t::NAME);

    const std::string& sectionName = sectionNameTokenPtr->text;

    // Branch based on the section name.
    if (sectionName == "apps")
    {
        return ParseComplexSection(lexer, sectionNameTokenPtr, ParseApp);
    }
    else if (sectionName == "bindings")
    {
        return ParseComplexSection(lexer, sectionNameTokenPtr, ParseBinding);
    }
    else if (sectionName == "commands")
    {
        return ParseComplexSection(lexer, sectionNameTokenPtr, ParseCommand);
    }
    else
    {
        lexer.ThrowException("Unrecognized section name '" + sectionName + "'.");
        return NULL;
    }
}


} // namespace internal


//--------------------------------------------------------------------------------------------------
/**
 * Parses a .sdef file in version 1 format.
 *
 * @return Pointer to a fully populated SdefFile_t object.
 *
 * @throw mk::Exception_t if an error is encountered.
 */
//--------------------------------------------------------------------------------------------------
parseTree::SdefFile_t* Parse
(
    const std::string& filePath,    ///< Path to .adef file to be parsed.
    bool beVerbose                  ///< true if progress messages should be printed.
)
//--------------------------------------------------------------------------------------------------
{
    parseTree::SdefFile_t* filePtr = new parseTree::SdefFile_t(filePath);

    ParseFile(filePtr, beVerbose, internal::ParseSection);

    return filePtr;
}



} // namespace adef

} // namespace parser


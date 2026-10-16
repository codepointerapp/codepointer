/**
 * \file GlobalCommands.hpp
 * \brief Definitions for commands constants
 * \author Diego Iastrubni diegoiast@gmail.com
 */

// SPDX-License-Identifier: MIT

#pragma once

namespace GlobalCommands {
// request to open a file
inline constexpr const char *OpenFile = "OpenFile";

// Broadcast - a new file has been loaded. The payload will contain the filename
inline constexpr const char *LoadedFile = "LoadedFile";

// Broadcast - a file has been saved. The payload will contain the filename
inline constexpr const char *SavedFile = "SavedFile";

// Broadcast - a file has been closed
inline constexpr const char *ClosedFile = "ClosedFile";

// Broadcast - build succeeded.
inline constexpr const char *BuildFinished = "BuildFinished";

// Broadcast - a new project has been loaded (ctags plugin uses to load a tags file)
inline constexpr const char *ProjectLoaded = "ProjectLoaded";

// Broadcast - a new project has been removed (ctags plugin uses to clear memory)
inline constexpr const char *ProjectRemoved = "ProjectRemoved";

// Broadcast - the project's codepointer.json config file changed on disk
inline constexpr const char *ProjectConfigChanged = "ProjectConfigChanged";

// User clicked on an issue, scroll the build output to the line passed in payload
inline constexpr const char *ScrollOutput = "ScrollOutput";

// Editor requests information about variable below mouse
inline constexpr const char *VariableInfo = "VariableInfo";

// Editor requests information about variable below mouse
inline constexpr const char *KeywordTooltip = "KeywordTooltip";

// Editor requests function signature help (call tips) at the cursor. The result
// is a rendered tooltip under GlobalArguments::Tooltip.
inline constexpr const char *SignatureHelp = "SignatureHelp";

// Editor requests a rename of the symbol at the given position. The editor owns
// the prompt and passes the chosen new name in GlobalArguments::NewName; the
// plugin applies it through the server and, on rejection, returns the reason
// under GlobalArguments::ErrorMessage.
inline constexpr const char *RenameSymbol = "RenameSymbol";

inline constexpr const char *FindReferences = "FindReferences";

// Editor asks for refactorings and fixes at the caret: "Fix available" entries
// for the diagnostics on this line, plus the quick fixes and refactorings the
// server offers for the range. The result is a list of {Name, Type, Tags} under
// GlobalArguments::Actions, where Type is the CodeActionKind and Tags holds the
// edits. Actions that need workspace/executeCommand are omitted rather than
// listed as dead entries - see docs/lsp-code-actions-proposal.md.
inline constexpr const char *CodeAction = "CodeAction";

// Editor picked one of the actions CodeAction returned and wants it applied.
// The payload is the same {Name, Type, Tags} entry, with Tags the edits. Applied
// plugin-side rather than in the editor so a fix that spans files works the same
// way a cross-file rename does.
inline constexpr const char *ApplyCodeAction = "ApplyCodeAction";

// Hand a list of reference locations to the references dock and show it. Sent by
// the editor when the user asks for the results the context menu could not fit.
inline constexpr const char *ShowReferences = "ShowReferences";

// Open a new editor, with the attached document
inline constexpr const char *DisplayText = "DisplayText";

// Used when pressing F4 on the editor
inline constexpr const char *FindMatchingFile = "FindMatchingFile";

// Used when pressing F4 on the editor
inline constexpr const char *ReformatCode = "ReformatCode";

// Check if a file belongs to any loaded project and return its details
inline constexpr const char *GetProjectForFile = "GetProjectForFile";

// Used to get symbols for completion
inline constexpr const char *ListSymbols = "ListSymbols";

} // namespace GlobalCommands

namespace GlobalArguments {
inline constexpr const char *ExactMatch = "ExactMatch";
inline constexpr const char *ProjectName = "ProjectName";
inline constexpr const char *RequestedSymbol = "RequestedSymbol";
inline constexpr const char *SourceDirectory = "SourceDirectory";
inline constexpr const char *BuildDirectory = "BuildDirectory";
inline constexpr const char *FileName = "FileName";
inline constexpr const char *Client = "Client";
inline constexpr const char *Name = "Name";
inline constexpr const char *TaskName = "TaskName";
inline constexpr const char *LineNumber = "LineNumber";
inline constexpr const char *ColumnNumber = "ColumnNumber";
inline constexpr const char *Type = "Type";
inline constexpr const char *Value = "Value";
// The new name a RenameSymbol request asks the server to apply.
inline constexpr const char *NewName = "NewName";
inline constexpr const char *Raw = "Raw";
inline constexpr const char *Tags = "Tags";
inline constexpr const char *Tooltip = "Tooltip";
inline constexpr const char *Symbol = "Symbol";
inline constexpr const char *Source = "Source";
inline constexpr const char *PreviousWord = "PreviousWord";
inline constexpr const char *Separator = "Separator";
inline constexpr const char *Content = "Content";
inline constexpr const char *ReadOnly = "ReadOnly";
inline constexpr const char *Position = "Position";
// End of the range a command applies to. Absent means the caret, so a caller
// with no selection does not have to spell out a zero-width range.
inline constexpr const char *EndLineNumber = "EndLineNumber";
inline constexpr const char *EndColumnNumber = "EndColumnNumber";
// CodeActionKinds the caller wants, narrowing the LSP `context.only` field.
// Absent or empty asks for everything, which is right for the menu but wrong for
// the margin marker: a refactoring such as "extract to function" is available on
// most lines and would light every one.
inline constexpr const char *OnlyCodeActionKinds = "OnlyCodeActionKinds";
inline constexpr const char *FoldTopLevel = "FoldTopLevel";
inline constexpr const char *IsDefinition = "IsDefinition";
inline constexpr const char *ExitCode = "ExitCode";
inline constexpr const char *ErrorMessage = "ErrorMessage";
// Entries are CommandArgs: Name is the action title, Type the CodeActionKind,
// and Tags a QVariantList of edits. Each edit is FileName, LineNumber /
// ColumnNumber for the start, EndLineNumber / EndColumnNumber for the end, and
// NewName - all 0-based, end-exclusive, as LSP states them. All six are needed:
// an edit missing its end arrives as an inverted range and is silently refused.
inline constexpr const char *Actions = "Actions";
} // namespace GlobalArguments

namespace GlobalResults {
constexpr const int ExecutableNotFound = -1;
constexpr const int ExecutableError = -2;
constexpr const int Crashed = -3;
constexpr const int NotSupported = -4;
} // namespace GlobalResults

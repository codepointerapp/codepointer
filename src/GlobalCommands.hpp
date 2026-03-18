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

// User clicked on an issue, scroll the build output to the line passed in payload
inline constexpr const char *ScrollOutput = "ScrollOutput";

// Editor requests information about variable below mouse
inline constexpr const char *VariableInfo = "VariableInfo";

// Editor requests information about variable below mouse
inline constexpr const char *KeywordTooltip = "KeywordTooltip";

// Open a new editor, with the attached document
inline constexpr const char *DisplayText = "DisplayText";

// Used when pressing F4 on the editor
inline constexpr const char *FindMatchingFile = "FindMatchingFile";

// Used when pressing F4 on the editor
inline constexpr const char *ReformatCode = "ReformatCode";

// Check if a file belongs to any loaded project and return its details
inline constexpr const char *GetProjectForFile = "GetProjectForFile";

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
inline constexpr const char *Raw = "Raw";
inline constexpr const char *Tags = "Tags";
inline constexpr const char *Tooltip = "Tooltip";
inline constexpr const char *Symbol = "Symbol";
inline constexpr const char *Content = "Content";
inline constexpr const char *ReadOnly = "ReadOnly";
inline constexpr const char *Position = "Position";
inline constexpr const char *FoldTopLevel = "FoldTopLevel";
inline constexpr const char *ExitCode = "ExitCode";
inline constexpr const char *ErrorMessage = "ErrorMessage";
} // namespace GlobalArguments

namespace GlobalResults {
constexpr const int ExecutableNotFound = -1;
constexpr const int ExecutableError = -2;
constexpr const int Crashed = -3;
constexpr const int NotSupported = -4;
} // namespace GlobalResults

**Full Changelog**: https://github.com/codepointerapp/codepointer/compare/v0.0.16...v0.1.0

# January 2026 - release 0.1.0 - Instant Flavor

Application got renamed to CodePointer. Releases, also have
a cool fun name, which really has no meaning.

Application got a new UI, by default all docks will be hidden.
The application also has a new main menu, on the side of the tabs.

A new plugin for git integration is work in progress, so far
you can see git log (for project, or single file). Integration will
get better on newer versions.

Important keyboard shortcuts you need to know:

1. `control+shift+p` brings up the command pallete. You can choose which
    menu actions to execute. Think of it, as a command line for this UI.
2. `control+p` brings up the list of files in the currently  open project.
3. `alt+control+m` - toggles the new UI, and a traditional one.
4. `control+b` - executes the active task of the current project.
5. `alt+#` - `#` is a number, will select the corresponding tab.
6. `control+#` - will select, or hide a dock.
7. `control+shift+f` - opens the search panel.
8. `control+g, f` - show git log from current file
9. `control+g, l` - show git log from repository, of current file
10. `control+g, d` - show git diff of current file

## Changelog
* qmdiEditor: better saves - https://github.com/codepointerapp/codepointer/issues/140
* ProjectManager: when opening a project, ensure the dock is visible - https://github.com/codepointerapp/codepointer/issues/136
* Filemanager: file properties not working on ZorinOS - https://github.com/diegoiast/qmdilib/issues/29
* Qutepart/Theme: changing themes on the fly no longer works #59 https://github.com/diegoiast/qutepart-cpp/issues/59
* Search: when searching globally - if selected, use text - https://github.com/codepointerapp/codepointer/issues/141
* ProjectManager: hello.sh - https://github.com/codepointerapp/codepointer/issues/137
* Wayland is broken - github.com/codepointerapp/codepointer/issues/142
* Fix deprecation warning: https://github.com/diegoiast/command-palette-widget/issues/6
* Qutepart: Indentation is broken if we indent from start of line #62 - https://github.com/diegoiast/qutepart-cpp/issues/62
* Started working on flatpack releases. Not upstream yet.
* New welcome page
* git: git integration

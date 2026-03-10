**Full Changelog**: https://github.com/diegoiast/codepointer/codepointer/v0.1.1...v0.1.2

# March 2026 - release 0.1.2 - Very superstition

![CodePointer with the new commit form](codepointer-v0.1.2-commit.png)

This short month brings up 3 major things:

1. Initial git commit dialog. You can trigger it via `control g`, `c`. Most
   of the commits in this cycle. Note that all git commits can be done by
   pressing `control+g` and the leaving `control` and pressing another command
   (commit, log, "file log", diff with more to come).
2. clang format support. If in the route of a saved file there is a
   `.clang-format` file - the IDE will call clang format after saving. This is
   configurable (never, only for files in loaded projects or always). This
   defaults to **never**, but I am testing it locally with loaded projects.
3. As I continue using the IDE as my main IDE, I setup my computer to use
   dark themes on nights. I found out that color changes were not properly
   applied. This was not trivial to find out - but this should not be a problem
   from now on.

Some other good cleanups were done (comments are properly supported on nested
syntaxes, clicking diff will open the corresponding file a the modified line,
markings are cleared when rebuilding, completion will preserve the case of the
text. See details in each commit.


## Changelog
* Delete empty line not possible - https://github.com/diegoiast/qutepart-cpp/issues/65
* Selection: moving selection moves a line too much - https://github.com/diegoiast/qutepart-cpp/issues/64
* Git-commit - https://github.com/codepointerapp/codepointer/issues/170
* HexViewer: use inline search - https://github.com/codepointerapp/codepointer/issues/160
* editor: clicking on a diff should open the current file - https://github.com/codepointerapp/codepointer/issues/163
* Line edit operations unit test is broken - https://github.com/diegoiast/qutepart-cpp/issues/49
* Python unit test - data driven - is not working - https://github.com/diegoiast/qutepart-cpp/issues/50
* CStyle indenter - if12 broken - https://github.com/diegoiast/qutepart-cpp/issues/52
* CStyleIndenter - for1 unit test broken - https://github.com/diegoiast/qutepart-cpp/issues/53
* Editor: auto reload - https://github.com/codepointerapp/codepointer/issues/168
* qutepart: marking are not properly cleared - https://github.com/diegoiast/qutepart-cpp/issues/66
* Completion does not honor case - https://github.com/diegoiast/qutepart-cpp/issues/67
* clang-format - https://github.com/codepointerapp/codepointer/issues/172
* Toggle comment - https://github.com/diegoiast/qutepart-cpp/issues/9
* The IDE does not adopt to new themes change - https://github.com/codepointerapp/codepointer/issues/166
* Closing a tab before content loaded, causes a crash - https://github.com/codepointerapp/codepointer/issues/162

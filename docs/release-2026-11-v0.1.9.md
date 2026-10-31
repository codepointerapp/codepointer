**Full Changelog**: <https://github.com/codepointerapp/codepointer/compare/v0.1.8.2..v0.1.9>

# Nov 2026 - release v0.1.9 - Picard when the walls fell

Skipped last month release to fully commit on a working LSP enabled release. This
was a big working branch, opened for more than a year. Internally it uses
<https://github.com/leon-bckl/lsp-framework/>.

Major changes:

* LSP support. You can get proper completions, calltips and references. I will
  work on actions for next iteration. Work will continue - see <https://github.com/codepointerapp/codepointer/issues/232>
* Flatpack is built, but not usable yet. I have other problems with buildig it
  upstream in Fluthub, those will take time.
* macOS builds are done for every release. I know that the IDE works, but looks
  will be miproved. Qt is not doing justice to this platform.

Next phases:

* I am working on a rust project, so the IDE will have better support for Rust.
* I am working on Scintilla support.
* Windows theme see <https://github.com/codepointerapp/codepointer/tree/winui3-style> and <https://github.com/JulienMaille/WinUI3Style>
* Git support will improve, see <https://github.com/codepointerapp/codepointer/issues/231>
* Terminal plugin is beeng updated with multi-tab support.
* Redesign the project manager plugin, in hope to fix some issues and code quality.

## Changelog

* Git: command tabs should note the repository dir - <https://github.com/codepointerapp/codepointer/issues/228>
* qmdilib: Open non existing files - <https://github.com/diegoiast/qmdilib/issues/37>
* qutepart: Indenter does not work on single line comments - <https://github.com/diegoiast/qutepart-cpp/issues/80>
* Help: about shows platform icons - 9b0fdf48fadff076edb3f154ea3e71a7f9ab9afc
* ProjectManager/KitDetector: several fixes for Windows - 8aafdc58342934a0a2940273dbadd71140ae4225
* macOS support (WIP) - <https://github.com/codepointerapp/codepointer/issues/229>
* kitdetector: lots of fixes for windows - 8aafdc58342934a0a2940273dbadd71140ae4225
* qutepart: Minimap: wrong scroll direction, no scroll at all - <https://github.com/diegoiast/qutepart-cpp/issues/77>
* AppImage: AppImage is X11 - <https://github.com/codepointerapp/codepointer/issues/233>
* AppImage: Contains no update information - <https://github.com/codepointerapp/codepointer/issues/227>
* Windows: use windows 11 style, and not vista -<https://github.com/codepointerapp/codepointer/commit/71acc2e2f25017c4dee407786f17e4b107245102>
* CommitForm: reduce margins - <https://github.com/codepointerapp/codepointer/commit/2943ce2967da4a017e7e1c8af65381254031b765>

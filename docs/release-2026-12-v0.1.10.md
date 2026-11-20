**Full Changelog**: <https://github.com/codepointerapp/codepointer/compare/v0.1.8.2..v0.1.9>

# December 2026 - release v0.1.10 - Las malvinas tocan el bajo

No major features, just a stability release.

Major changes:

* New theme, build specially for Windows. Follows the WinUI3 convention, but also
   available on Linux (default on Windows).
* Follow symbol and reference got key shortcuts.
* Small fixes for rust/cargo.

Next phases:

* I am working on Scintilla support.
* Git support will improve, see <https://github.com/codepointerapp/codepointer/issues/231>
* Terminal plugin is beeng updated with multi-tab support.
* Redesign the project manager plugin, in hope to fix some issues and code quality.

## Changelog

* Alternative theme support - https://github.com/codepointerapp/codepointer/issues/127
* LSP: rename fixes - https://github.com/codepointerapp/codepointer/issues/239
* Project manager: rust kit not-auto detected on windows https://github.com/codepointerapp/codepointer/issues/237
* Project manager: need "system" kit - https://github.com/codepointerapp/codepointer/issues/236
* cargo: auto detected apps have non native separators - https://github.com/codepointerapp/codepointer/issues/235
* IDE changes to random branchs on load - https://github.com/codepointerapp/codepointer/issues/241
* ProjectManagerPlg.cpp: fix 100% usage on building/running apps - 5a6c2853a5d420c843635075af8cf221c455e061
* git: commit will no longer ask for password - 6260f99990b29c9ab4575caf01a02acf0fa6bbb9, 45103ec0d1446411a177b7717f7ba736a0c463cf
* git: push force - disable when finished - 9a9c685eb7ffe31c48bedb611b822bf2c9686639
* ProjectManagerPlg.cpp: do not load already opened projects - b961cc7ce1642f59a804ade479a8cff13d8dd7c1
* Qutepart: fix line numbers issues -
* kitdetector.cpp: add system and rust only kits (237) - 86e4764b167de721bfee567efe67fbb66fd17b38
* ProjectBuildConfig.cpp : cargo fixes for windows - 68fd7304c23def53c365ae7d916672c2832423f8
* ProjectManagerPlg.cpp: fix running batch files (240) - 03c2d64528d819af632e41f1e6273b534840756d
* cargo: fix display of exes (235) - 1247dba2b95c4ed69e649a6f69e740005f68c7ef

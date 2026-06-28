**Full Changelog**: <https://github.com/codepointerapp/codepointer/compare/v0.1.5...v0.1.6>

# July 2026 - release v0.1.6 - The mother of all funk chords

While dark themes are supported (the editor has dark themes) and the system
theme was .... existing, it was not ideal. Now the system editor theme will be more
readable.

One epic win this IDE has over QtCreator (for example) is that the IDE detects
when the system theme changes - and it starts using a drak theme. In QtCreator
the IDE becomes dark, but editors keep white.

A new indentation algorythm has been added. This makes writing markdown files
a little easier. As git commits are markdown on this IDE - this improves the
git commit experience (I was not waiting for this as well!).

## Changelog

* git: Commit filenames are hidden on small displays - <https://github.com/codepointerapp/codepointer/issues/209>
* ProjectManager: Running the same task 2 times fails - <https://github.com/codepointerapp/codepointer/issues/208>
* Treesitter: shows completions from other projects - <https://github.com/codepointerapp/codepointer/issues/210>
* Treesitter: reduce duplication- <https://github.com/codepointerapp/codepointer/commit/0e198aa9b7a4374c50986ae2d938e01493c101ad>
* git: Commit form does not save message - <https://github.com/codepointerapp/codepointer/issues/211>
* CommitForm: use markdown intenter for commit message #212 - <https://github.com/codepointerapp/codepointer/issues/212>
* Qutepart: Style: better dark system colors - <https://github.com/diegoiast/qutepart-cpp/commit/9946f975ae1f870d2f96aeb53cf81fb7d5d8ae39>
* ProjectManager: Project/FileList: display native filenames <https://github.com/codepointerapp/codepointer/commit/0c946df0d91a991af0c2993a9fd4ecb30a223a43>
* ProjectManagerPlugin: fix clicking on error filenames on Windows - <https://github.com/codepointerapp/codepointer/commit/16ecf05d2c7e3bff62d930174e517048c65100f6>
* CommitForm: show the filename when elided - <https://github.com/codepointerapp/codepointer/commit/ff9f90028f173b491403715c254f03e059210aa2>

**Full Changelog**: <https://github.com/codepointerapp/codepointer/compare/v0.1.6...v0.1.7>

# Aug 2026 - release v0.1.7 - Hi Bob

Git commit updates, async completions, editor loading reduced, new command palette

- git commit form got many updates
  - Auto hide diff on small displays.
  - New checkobox to hide/show diff.
  - Double click the filename - and diff will be opened in a new tab.
  - Better support for staged files (you can diff them!)
  - Delete untracked, staged files and dirs
  - Completion will also show modified files
- [qutepart](https://github.com/diegoiast/qutepart-cpp/): Major speedups
  - On my laptop, a file 90kb sized file with 50,000 lines: loading time
    got reduced from 1400msec to 1050msec.
- [Command palette](https://github.com/diegoiast/command-palette-widget):
  - Was updated with a new look
  - It should provide better results (better sorting of display results)
- TreeSitter is now properly async which should make the editing much comfortable

## Changelog

- git: commit window - hide diff #202, #213 b177cf89877f8028dc339d6c2d631e7705013db7
- Qutepart: new upstream release: <https://github.com/diegoiast/qutepart-cpp/commit/604f79a277810f18e5fdb532e92984e25adb711e>
- git: delete untrcacked/unstashed directory #207 5348d6def7b80451003a38264ad1d5a8ec203b1b
- git: commit files - as completion (#206) 75138bd30ff5143ab6cf37f96269bb24456f2045
- CommandPaltte: new upstream release: <https://github.com/diegoiast/command-palette-widget/commit/6eec999d0a6f4c295daa4ac4a7ea78cf8c74e5f0>
- TreeSitterPlugin: make the result truely async 56246b4243fbed555a17097f837f4c3b36741a27

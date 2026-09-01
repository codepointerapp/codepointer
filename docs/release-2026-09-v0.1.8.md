**Full Changelog**: <https://github.com/codepointerapp/codepointer/compare/v0.1.7...v0.1.8>

# Sep 2026 - release v0.1.8 - Dolly parton

The main work this month again, is performance. I managed to get the qutepart benchmark
I use down to 890msec (loading a 944kb file with 200k lines). Just a reminder:

- https://github.com/diegoiast/qutepart-cpp/commit/c1674ea4fef355d1c2c2d10ae53e226eb5e52593
- https://github.com/diegoiast/qutepart-cpp/commit/bac875192a98fc1a7378f96d20531892c8e44b1e
- https://github.com/diegoiast/qutepart-cpp/commit/ab227d43732f1d0d15c17887ff6cbb1cdc0797c0
- https://github.com/diegoiast/qutepart-cpp/commit/ae6b93b10da44669314bfbb58f6e0cf0cc01d4ee

I also tested that benchmark against [upstream's](https://github.com/KDE/syntax-highlighting) QSyntaxHighlighter
test, and my code is in par with that (using slightly different definitions). This means that the 
bottle neck is not the highlighter, but [QPlainTextDocumentLayout](https://doc.qt.io/qt-6/qplaintextdocumentlayout.html)
or maybe [QPlainTextEdit](https://doc.qt.io/qt-6/qplaintextedit.html) itself. 

Building inside the IDE should be better - on my machine it was triggering OOM. It was a bug
in how I implemented parallel builds under cmake. see https://github.com/codepointerapp/codepointer/issues/219 .

## The next phases

1. I am tring to publish this application as a flatpak, see https://github.com/codepointerapp/codepointer/issues/24
   I found out that due to sandbox, the application will not even see `gcc`. I can prefix the commands to overcome this
   I am unsure how the flatpack community will like this, since this basically escapes the sandbox. 
   I am also using an ugly trick to build - as I am allowing network, which is not default. Solution is
   to properly move all dependencies to a package manager (vcpkg or conan).
2. I am working on making the terminal support better. See https://github.com/codepointerapp/codepointer/issues/218
   Not only I am adding multiple tabs interface, but I would like to make the terminals more "full screen"
   and drive the application output through it (currently it uses a plain text viewer). This will allow
   to properly run interactive command line utilities on the internal terminal.
3. [LSP support is comming](https://github.com/codepointerapp/codepointer/issues/55). I have a working branch - which on Unix works OK. I am working on making
   it work as needed on Windows. Next will be https://microsoft.github.io/debug-adapter-protocol/ - but this
   will more time, as I will need to implement it from scratch.
4. [Qutepart is working to have spell checker enabled](https://github.com/diegoiast/qutepart-cpp/issues/60).
   Problems I need to fix are:

   4.1 I am using a sonnet "port" which I pull the sources at build time. This will break package
       manager builds and flatpack.
       
   4.2 Dictionaries need to be downloaded on runtime. 
   
   4.3 Current implementation spell checks on all the document, and not in regions defined by the
       syntax highlighter. 
   
I am also toying with the idea of using scintilla.     

## Changelog

- Cannot modify theme #216 - https://github.com/codepointerapp/codepointer/issues/216
- Qutepart: Makrdown indenter - line wrap in markdown is broken #74 - https://github.com/diegoiast/qutepart-cpp/issues/74
- Qutepart: Lisp indenter - find brace unit test fails 51 - https://github.com/diegoiast/qutepart-cpp/issues/51
- Building consumes too much processes and memory 219 - https://github.com/codepointerapp/codepointer/issues/219
- Copy relative path action is added sereral times to the tab widget's right click menu 222 - https://github.com/codepointerapp/codepointer/issues/222
~~~- Sometimes I cannot run tasks or projects - 220 - https://github.com/codepointerapp/codepointer/issues/220~~~
- Qutepart: make syntax highlighting 2.5x faster - https://github.com/diego   iast/qutepart-cpp/commit/221d73b866faeba843378c153f6876642ea8fa18
- Formatting does not always work - https://github.com/codepointerapp/codepointer/issues/221
- UI: make buttons on the project manger searchable - https://github.com/codepointerapp/codepointer/commit/dc36aae8dba28dfb2b640479f813746259fd1536
- LTO build: https://github.com/codepointerapp/codepointer/commit/f4437f37cbe767cba298308d11020a00b5f3042f
- Terminal: default dir is wrong - 224 - https://github.com/codepointerapp/codepointer/issues/224
- MSVC: comilation error warnings are off by one - 226 - https://github.com/codepointerapp/codepointer/issues/226
- Qt: mingw+msvc mixtured kit - 223 - https://github.com/codepointerapp/codepointer/issues/223


Issue 220 - this triggered a fww minor revisions. Seeme like that code is completlly broken.


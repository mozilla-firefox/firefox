# Formatting C++ Code With clang-format

Mozilla uses the Google coding style for whitespace, which is enforced
using [clang-format](https://clang.llvm.org/docs/ClangFormat.html). A
specific version of the binary will be installed when
`./mach bootstrap` is run. We build our own binaries and update them
as needed.

Options are explicitly defined [in clang-format
itself](https://github.com/llvm-mirror/clang/blob/e8a55f98df6bda77ee2eaa7f7247bd655f79ae0e/lib/Format/Format.cpp#L856).
If the options are changed in clang upstream, this might cause some
changes in the Firefox tree. For this reason, it is best to use the
mozilla-provided binaries.

## Manual formatting

We provide mach subcommands for running [clang-format] from the
command-line. These wrappers handle ensuring the correct version of
[clang-format] is installed and run.

If [clang-format] isn't installed, the binaries will be automatically
downloaded from taskcluster and installed into ~/.mozbuild. We build our
own [clang-format] binaries.

### Formatting local changes

```
$ ./mach lint -l clang-format
```

When run without path arguments, it will lint the files you have
modified.

To automatically fix formatting issues:

```
$ ./mach format -l clang-format
```

Or equivalently:

```
$ ./mach lint -l clang-format --fix
```

### Formatting specific paths

```
$ ./mach format <path>   # Format <path> in-place, picks the right formatter
```

The command accepts paths to reformat specific directories or files,
automatically selecting the appropriate formatter.

### Configuring the clang-format commit hook

To run clang-format at commit phase, the configuration is the following:

```
# From the root git directory:
$ ln -s $(pwd)/tools/lint/hooks_clang_format.py .git/hooks/pre-commit
```

## Editor integration

It is possible to configure many editors to support running
`clang-format` automatically on save, or when run from within the
editor.

### Editor plugins

- [Atom](https://atom.io/packages/clang-format)

- [BBEdit](http://clang.llvm.org/docs/ClangFormat.html#bbedit-integration)

  - [clang-format-bbedit.applescript](https://raw.githubusercontent.com/llvm-mirror/clang/master/tools/clang-format/clang-format-bbedit.applescript)

- Eclipse

  - Install the
    [CppStyle](https://marketplace.eclipse.org/content/cppstyle)
    plugin
  - In Preferences -> C/C++ -> CppStyle, set the clang-format path to
    \~/.mozbuild/clang-tools/clang-tidy/bin/clang-format
  - (Optional) check "Run clang-format on file save"

- [Emacs](http://clang.llvm.org/docs/ClangFormat.html#emacs-integration)

  - [clang-format.el](https://raw.githubusercontent.com/llvm-mirror/clang/master/tools/clang-format/clang-format.el)
    (Or install
    [clang-format](http://melpa.org/#/clang-format) from MELPA)
  - [google-c-style](http://melpa.org/#/google-c-style) from MELPA

- [Sublime Text](https://packagecontrol.io/packages/Clang%20Format)

  - [alternative
    tool](https://github.com/rosshemsley/SublimeClangFormat)

- [Vim](http://clang.llvm.org/docs/ClangFormat.html#vim-integration)

  - [clang-format.py](https://raw.githubusercontent.com/llvm-mirror/clang/master/tools/clang-format/clang-format.py)
  - [vim-clang-format](https://github.com/rhysd/vim-clang-format)

- [Visual
  Studio](https://marketplace.visualstudio.com/items?itemName=LLVMExtensions.ClangFormat)

  - [llvm.org plugin](http://llvm.org/builds/)
  - [Integrated support in Visual Studio
    2017](https://blogs.msdn.microsoft.com/vcblog/2018/03/13/clangformat-support-in-visual-studio-2017-15-7-preview-1/)

- [Visual Studio
  Code](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format)

- [XCode](https://github.com/travisjeffery/ClangFormat-Xcode)

- [Script for patch
  reformatting](http://clang.llvm.org/docs/ClangFormat.html#script-for-patch-reformatting)

  - [clang-format-diff.py](https://raw.githubusercontent.com/llvm-mirror/clang/master/tools/clang-format/clang-format-diff.py)

### Configuration

These tools generally run clang-format themselves, and won't use
`./mach lint`. The binary installed by our tooling will be
located at `~/.mozbuild/clang-tools/clang-tidy/bin/clang-format`.

You typically shouldn't need to specify any other special configuration
in your editor besides the clang-format binary. Most of the
configuration that clang-format relies on for formatting is stored
inside our source tree. More specifically, using the .clang-format file
located in the root of the repository. Please note that this doesn't
include the list of ignored files and directories (provided by
.clang-format-ignore which is a feature provided by the mach command
wrapper).

Coding style configuration is done within clang-format itself. When we
change the configuration (incorrect configuration, new feature in clang,
etc), we use {searchfox}`local overrides <mozilla-central/rev/501eb4718d73870892d28f31a99b46f4783efaa0:.clang-format>`.

### Ignored files & directories

We maintain a {searchfox}`list of ignored directories and files <mozilla-central/rev/501eb4718d73870892d28f31a99b46f4783efaa0:.clang-format-ignore>`,
which is used by `./mach lint -l clang-format`. This is generally only used
for code broken by clang-format, and third-party code.

### Ignored code hunks

Sections of code may have formatting disabled using comments. If a
section must not be formatted, the following comments will disable the
reformat:

```
// clang-format off
my code which should not be reformatted
// clang-format on
```

You can find an {searchfox}`example of code not formatted <mozilla-central/rev/501eb4718d73870892d28f31a99b46f4783efaa0:xpcom/io/nsEscape.cpp#22>`.

## Ignore lists

To make sure that the blame feature of git isn't affected, the reformatting
commits are listed in
<https://searchfox.org/firefox-main/source/.git-blame-ignore-revs>.

[clang-format]: https://clang.llvm.org/docs/ClangFormat.html

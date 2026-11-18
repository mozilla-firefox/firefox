# Debugging Firefox with GDB

This page details how you can more easily debug Firefox with gdb. {doc}`rr <debugging_firefox_with_rr>` is most often a better choice to debug a problem,
but sometimes it isn't possible to use it, such as attempting to reproduce a
race condition or when performance is important to reproduce an issue. `rr`
chaos mode allows reproducing a lot of issues though, and should be tried.

## Where can I find general gdb documentation?

Using GDB is beyond the scope of this document. Documentation is likely
available on your system if you have GDB installed, in the form of
**info,** **man** pages, or the gnome help browser. Additionally, you
can use a graphical front-end to GDB like
[ddd](https://www.gnu.org/software/ddd/) or
[insight](https://sourceware.org/insight/). For more information see
<https://sourceware.org/gdb/current/onlinedocs/gdb/>

## How to debug Firefox with gdb?

Firefox is a multiprocess application, with a parent process, and several child
processes, each specialized and sandboxed differently.

```bash
./mach run --debugger=gdb
```

allows running Firefox in gdb, and debug the parent process. You can substitute
`run` with another action, such as `test`, `mochitest`, `xpcshell-test`,
etc.

Debugging child processes can be done by attaching the debugger.

```bash
gdb --pid <pid>
```

There's a number of ways to find the PID of a process: hovering over a tab for a
content process, opening `about:processes`, using `ps ... | grep firefox` on
the command line, etc.

Sometimes, it is desirable to attach to a child process at startup, to diagnose
something very close to the start of the process. Setting the environment
variable `MOZ_DEBUG_CHILD_PROCESS=10` will make each new process print an few
informative lines, including the process type and its PID. The process with then
sleep for a number of seconds equal to the value of the environment variable,
allowing to attach a debugger.

```bash
$ MOZ_DEBUG_CHILD_PROCESS=10 ./mach run
...
...
...

CHILDCHILDCHILDCHILD (process type tab)
debug me @ 65230

...
...
...
```

Attaching gdb to Firefox might fail on Linux distributions that enable common
kernel hardening features such as the Yama security module. If you encounter the
following error when attaching:

```bash
$ gdb --pid <pid>
...
...

Attaching to process <pid>
ptrace: Operation not permitted.
```

Check the contents of `/proc/sys/kernel/yama/ptrace_scope`. If it set to `1`
you won't be able to attach to processes, set it to `0` from a root shell:

```bash
\# echo 0 > /proc/sys/kernel/yama/ptrace_scope
```

If you still can't attach check your setup carefully. Do not, under any
circumstances, run gdb as root. Since gdb can execute arbitrary code and spawn
shells it can be extremely dangerous to use it with root permissions.

## Advanced gdb configuration

The preferred method, is using the
{doc}`/mach/index` command-line tool to run the
debugger, which can bypass several optional defaults. Use "mach help
run" to get more details. If inside the source directory, you would use
"./mach". Please note that
{doc}`mach is aware of mozconfigs </setup/configuring_build_options>`.

```bash
./mach run --debug [arguments to pass to firefox]
```

If you need to direct arguments to gdb, you can use '--debugger-args'
options via the command line parser, taking care to adhere to shell
splitting rules. For example, if you wanted to run the command 'show
args' when gdb starts, you would use:

```bash
./mach run --debug --debugger-args "-ex 'show args'"
```

Alternatively, you can run gdb directly against Firefox. However, you
won't get some of the more useful capabilities this way. For example,
mach sets an environment variable (see below) to stop the JS engine from
generating synthetic segfaults to support the slower script dialoging
mechanism.

```
(gdb) $OBJDIR/dist/bin/firefox
```

## How to debug a Firefox in the field (not compiled on the host)

If you need to attach to a Firefox process live on a machine, and this Firefox
was built by Mozilla, or by certain Linux distros, it's possible to get symbols
and sources using the Mozilla symbol server: it's just a matter of sourcing a
python script in `.gdbinit`.

Debugging then works as usual, except the build probably has a very high
optimization level.

## How do I pass arguments in prun?

Set the arguments in GDB before calling prun. Here's an example on how
to do that:

```
(gdb) set args https://www.mozilla.org
(gdb) prun
```

## Why breakpoints seem to not be hit?

The most likely cause is that `gdb` hasn't been attached to the process in which
the code to diagnose is ran. Enabling the relevant `MOZ_LOG` modules can help,
since by default it prints the process type and pid of all logging statements.

`break list` will display a list of breakpoints, and whether or not they're
enabled. C++ namespaces need to be specified entirely, and it's sometimes hard
to break in lambda. Breaking by line number is an alternative strategy that
often works in this case.

## How do I display an nsString?

```
(gdb) p ToNewCString(string);
```

This leaks a bit of memory but it doesn't really matter.

## How do I determine the concrete type of an object pointed to by an interface pointer?

You can determine the concrete type of any object pointed to, by an
XPCOM interface pointer, by looking at the mangled name of the symbol
for the object's vtable:

```
(gdb) p aKidFrame
$1 = (nsIFrame *) 0x85058d4
(gdb) x/wa *(void**)aKidFrame
0x4210d380 <__vt_14nsRootBoxFrame>: 0x0
(gdb) p *(nsRootBoxFrame*)aKidFrame
 [ all the member variables of aKidFrame ]
```

Or use the gdb command `set print object on`.

## How can I debug JavaScript from gdb?

If you have JavaScript Engine code on the stack, you'll probably want a
JS stack in addition to the C++ stack.

```
(gdb) call DumpJSStack()
```

Please note that if `gdb` has been attached to a process, the stack might be
printed in the terminal window in which Firefox was started.

See {doc}`debugging_on_windows` for more JS debugging tricks.

## How can I debug race conditions

Try {doc}`rr <debugging_firefox_with_rr>` first. If this doesn't work, good
luck, maybe try {doc}`logging </xpcom/logging>` or sprinkling assertions.

## I keep getting a SIGSYS, or SIGSEGV in JS/JIT code under gdb even though there is no crash when gdb is not attached. How do I fix it?

Allow gdb to read the Firefox repository's .gdbinit, located at `build/.gdbinit`. In
your own `.gdbinit`, add the line:

```
add-auto-load-safe-path /path/to/your/firefox/clone
```

## How do I get useful stack traces inside system libraries?

Many Linux distributions provide separate packages with debugging
information for system libraries, such as gdb, Valgrind, profiling
tools, etc., to give useful stack traces via system libraries.

The modern way to do this is to enable `debuginfod`. This can be done by adding:

```
set debuginfod enabled on
```

in your `.gdbinit`, but there might be distro-specific instructions.
Alternatively, you can install the packages that contain the debug symbols for
the libraries you want to debug.

When using `debuginfod`, the correct information will be downloaded
automatically when needed (and subsequently cached).

If you're not sure what to use, there's a federated debuginfod server that
provides debug information for most mainstream distributions. You can use it
by adding the following line to your `.gdbinit` file:

```
set debuginfod urls "https://debuginfod.elfutils.org/"
```

Keep in mind that it might take a while to download debug information the
very first time. This queries all the servers of multiple distributions
sequentially and debug information tends to be large. It will be cached for the
next run though.

### Fedora

On Fedora, you need to enable the debuginfo repositories, as the
packages are in separate repositories. Enable them permanently, so when
you get updates you also get security updates for these packages. A way
to do this is edit `/etc/yum.repos.d/fedora.repo` and
`fedora-updates.repo` to change the `enabled=0` line in the
debuginfo section to `enabled=1`. This may then flag a conflict when
upgrading to a new distribution version. You would the need to perform
this edit again.

You can finally install debuginfo packages with yum or other package
management tools. The best way is install the `yum-utils` package, and
then use the `debuginfo-install` command to install all the debuginfo:

```bash
yum install yum-utils
debuginfo-install firefox
```

This can be done manually using:

```bash
$ yum install GConf2-debuginfo ORBit2-debuginfo atk-debuginfo \
cairo-debuginfo dbus-debuginfo expat-debuginfo \
fontconfig-debuginfo freetype-debuginfo gcc-debuginfo glib2-debuginfo \
glibc-debuginfo gnome-vfs2-debuginfo gtk2-debuginfo gtk2-engines-debuginfo \
hal-debuginfo libX11-debuginfo libXcursor-debuginfo libXext-debuginfo \
libXfixes-debuginfo libXft-debuginfo libXi-debuginfo libXinerama-debuginfo \
libXrender-debuginfo libbonobo-debuginfo libgnome-debuginfo \
libselinux-debuginfo pango-debuginfo popt-debuginfo scim-bridge-debuginfo
```

## Disabling multiprocess

`mach run` and `mach test` both accept a `--disable-e10s` argument. Some
debuggers can't catch child-process crashes without it. This is sometimes a
viable alternative to attaching, but these days it changes enough thing that
it's not always a usable option.

## See also

- [Mike Conley's blog post](https://mikeconley.ca/blog/2014/04/25/electrolysis-debugging-child-processes-of-content-for-make-benefit-glorious-browser-of-firefox)
- [Performance tools](https://wiki.mozilla.org/Performance:Tools)
- [Fun with
  gdb](https://blog.mozilla.com/sfink/2011/02/22/fun-with-gdb/) by
  Steve Fink

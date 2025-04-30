.. _mach_commands:

=====================
Implementing Commands
=====================

Mach commands are defined via Python decorators.

All the relevant decorators are defined in the *mach.decorators* module.
The important decorators are as follows:

:py:func:`Command <mach.decorators.Command>`
  A function decorator that denotes that the function should be called when
  the specified command is requested. The decorator takes a command name
  as its first argument and a number of additional arguments to
  configure the behavior of the command. The decorated function must take a
  ``command_context`` argument as its first.
  ``command_context`` is a properly configured instance of a ``MozbuildObject``
  subclass, meaning it can be used for accessing things like the current config
  and running processes.

:py:func:`CommandArgument <mach.decorators.CommandArgument>`
  A function decorator that defines an argument to the command. Its
  arguments are essentially proxied to ArgumentParser.add_argument()

:py:func:`SubCommand <mach.decorators.SubCommand>`
  A function decorator that denotes that the function should be a
  sub-command to an existing ``@Command``. The decorator takes the
  parent command name as its first argument and the sub-command name
  as its second argument.

  ``@CommandArgument`` can be used on ``@SubCommand`` instances just
  like they can on ``@Command`` instances.


Here is a complete example:

.. rstcheck: ignore-languages=python
.. code-block:: python

   from mach.decorators import Command, CommandArgument

    @Command('doit', category='testing', description='Do ALL OF THE THINGS.')
    @CommandArgument('--force', '-f', action='store_true',
        help='Force doing it.')
    def doit(command_context, force=False):
        # Do stuff here.
        print("hello world")

All paths are relative to the root source folder.

Save your file somewhere e.g. ``testing/doit.py``.

Add an entry for your command in ``python/mach/mach/command_util.py`` in
the `MACH_COMMANDS` dictionary. Maintain the alphabetical order.

e.g

.. code-block:: python

    MACH_COMMANDS = {
        # ... previous entries here
        "doctor": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
        "doit": MachCommandReference("testing/doit.py"),
        "environment": MachCommandReference("python/mozbuild/mozbuild/mach_commands.py"),
        # ... rest of entries here
    }

When the module is loaded, the decorators tell mach about all handlers.
When mach runs, it takes the assembled metadata from these handlers and
hooks it up to the command line driver. Under the hood, arguments passed
to the decorators are being used to help mach parse command arguments,
formulate arguments to the methods, etc. See the documentation in the
:py:mod:`mach.base` module for more.

The Python modules defining mach commands do not need to live inside the
main mach source tree.

Conditionally Filtering Commands
================================

Sometimes it might only make sense to run a command given a certain
context. For example, running tests only makes sense if the product
they are testing has been built, and said build is available. To make
sure a command is only runnable from within a correct context, you can
define a series of conditions on the
:py:func:`Command <mach.decorators.Command>` decorator.

A condition is simply a function that takes an instance of the
:py:func:`mozbuild.base.MachCommandBase` class as an argument, and
returns ``True`` or ``False``. If any of the conditions defined on a
command return ``False``, the command will not be runnable. The
docstring of a condition function is used in error messages, to explain
why the command cannot currently be run.

Here is an example:

.. rstcheck: ignore-languages=python
.. code-block:: python

  from mach.decorators import (
      Command,
  )

  def build_available(cls):
      """The build needs to be available."""
      return cls.build_path is not None

   @Command('run_tests', category='testing', description='A description.' conditions=[build_available])
   def run_tests(command_context):
       # Do stuff here.

By default all commands without any conditions applied will be runnable,
but it is possible to change this behaviour by setting
``require_conditions`` to ``True``:

.. code-block:: python

   m = mach.main.Mach()
   m.require_conditions = True

Minimizing Code in Commands
===========================

Mach command modules, classes, and methods work best when they are
minimal dispatchers. The reason is import bloat. Currently, the mach
core needs to import every Python file potentially containing mach
commands for every command invocation. If you have dozens of commands or
commands in modules that import a lot of Python code, these imports
could slow mach down and waste memory.

It is thus recommended that mach modules, classes, and methods do as
little work as possible. Ideally the module should only import from
the :py:mod:`mach` package. If you need external modules, you should
import them from within the command method.

To keep code size small, the body of a command method should be limited
to:

1. Obtaining user input (parsing arguments, prompting, etc)
2. Calling into some other Python package
3. Formatting output

Of course, these recommendations can be ignored if you want to risk
slower performance.

In the future, the mach driver may cache the dispatching information or
have it intelligently loaded to facilitate lazy loading.

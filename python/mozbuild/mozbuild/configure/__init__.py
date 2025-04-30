# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import codecs
import inspect
import logging
import os
import re
import sys
import types
from collections import OrderedDict
from contextlib import contextmanager
from functools import wraps

import mozpack.path as mozpath
import six
from six.moves import builtins as __builtin__

from mozbuild.configure.help import HelpFormatter
from mozbuild.configure.options import (
    HELP_OPTIONS_CATEGORY,
    CommandLineHelper,
    ConflictingOptionError,
    InvalidOptionError,
    Option,
    OptionValue,
)
from mozbuild.configure.util import ConfigureOutputHandler, LineIO, getpreferredencoding
from mozbuild.util import (
    ReadOnlyDict,
    ReadOnlyNamespace,
    memoize,
    memoized_property,
    system_encoding,
)

# TRACE logging level, below (thus more verbose than) DEBUG
TRACE = 5


class ConfigureError(Exception):
    pass


class SandboxDependsFunction:
    """Sandbox-visible representation of @depends functions."""

    def __init__(self, unsandboxed):
        self._or = unsandboxed.__or__
        self._and = unsandboxed.__and__
        self._invert = unsandboxed.__invert__
        self._getattr = unsandboxed.__getattr__

    def __call__(self, *arg, **kwargs):
        raise ConfigureError("The `%s` function may not be called" % self.__name__)

    def __or__(self, other):
        if not isinstance(other, SandboxDependsFunction):
            raise ConfigureError(
                "Can only do binary arithmetic operations "
                "with another @depends function."
            )
        return self._or(other).sandboxed

    def __and__(self, other):
        if not isinstance(other, SandboxDependsFunction):
            raise ConfigureError(
                "Can only do binary arithmetic operations "
                "with another @depends function."
            )
        return self._and(other).sandboxed

    def __invert__(self):
        return self._invert().sandboxed

    def __cmp__(self, other):
        raise ConfigureError("Cannot compare @depends functions.")

    def __eq__(self, other):
        raise ConfigureError("Cannot compare @depends functions.")

    def __hash__(self):
        return object.__hash__(self)

    def __ne__(self, other):
        raise ConfigureError("Cannot compare @depends functions.")

    def __lt__(self, other):
        raise ConfigureError("Cannot compare @depends functions.")

    def __le__(self, other):
        raise ConfigureError("Cannot compare @depends functions.")

    def __gt__(self, other):
        raise ConfigureError("Cannot compare @depends functions.")

    def __ge__(self, other):
        raise ConfigureError("Cannot compare @depends functions.")

    def __getattr__(self, key):
        return self._getattr(key).sandboxed

    def __nonzero__(self):
        raise ConfigureError("Cannot do boolean operations on @depends functions.")


class DependsFunction:
    __slots__ = (
        "_func",
        "_name",
        "dependencies",
        "when",
        "sandboxed",
        "sandbox",
        "_result",
    )

    def __init__(self, sandbox, func, dependencies, when=None):
        assert isinstance(sandbox, ConfigureSandbox)
        assert not inspect.isgeneratorfunction(func)
        # Allow non-functions when there are no dependencies. This is equivalent
        # to passing a lambda that returns the given value.
        assert inspect.isroutine(func) or not dependencies
        self._func = func
        self._name = getattr(func, "__name__", None)
        self.dependencies = dependencies
        self.sandboxed = wraps(func)(SandboxDependsFunction(self))
        self.sandbox = sandbox
        self.when = when
        sandbox._depends[self.sandboxed] = self

        # Only @depends functions with a dependency on '--help' are executed
        # immediately. Everything else is queued for later execution.
        if sandbox._help_option in dependencies:
            sandbox._value_for(self)
        elif not sandbox._help:
            sandbox._execution_queue.append((sandbox._value_for, (self,)))

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, value):
        self._name = value

    @property
    def sandboxed_dependencies(self):
        return [
            d.sandboxed if isinstance(d, DependsFunction) else d
            for d in self.dependencies
        ]

    @memoize
    def result(self):
        if self.when and not self.sandbox._value_for(self.when):
            return None

        if inspect.isroutine(self._func):
            resolved_args = [self.sandbox._value_for(d) for d in self.dependencies]
            return self._func(*resolved_args)
        return self._func

    def __repr__(self):
        return "<%s %s(%s)>" % (
            self.__class__.__name__,
            self.name,
            ", ".join(repr(d) for d in self.dependencies),
        )

    def __or__(self, other):
        if isinstance(other, SandboxDependsFunction):
            other = self.sandbox._depends.get(other)
        assert isinstance(other, DependsFunction)
        assert self.sandbox is other.sandbox
        return CombinedDependsFunction(self.sandbox, self.or_impl, (self, other))

    @staticmethod
    def or_impl(iterable):
        # Applies "or" to all the items of iterable.
        # e.g. if iterable contains a, b and c, returns `a or b or c`.
        for i in iterable:
            if i:
                return i
        return i

    def __and__(self, other):
        if isinstance(other, SandboxDependsFunction):
            other = self.sandbox._depends.get(other)
        assert isinstance(other, DependsFunction)
        assert self.sandbox is other.sandbox
        return CombinedDependsFunction(self.sandbox, self.and_impl, (self, other))

    def __invert__(self):
        return TrivialDependsFunction(self.sandbox, lambda x: not x, [self])

    @staticmethod
    def and_impl(iterable):
        # Applies "and" to all the items of iterable.
        # e.g. if iterable contains a, b and c, returns `a and b and c`.
        for i in iterable:
            if not i:
                return i
        return i

    def __getattr__(self, key):
        if key.startswith("_"):
            return super(DependsFunction, self).__getattr__(key)
        # Our function may return None or an object that simply doesn't have
        # the wanted key. In that case, just return None.
        return TrivialDependsFunction(
            self.sandbox, lambda x: getattr(x, key, None), [self], self.when
        )


class TrivialDependsFunction(DependsFunction):
    """Like a DependsFunction, but the linter won't expect it to have a
    dependency on --help ever."""


class CombinedDependsFunction(DependsFunction):
    def __init__(self, sandbox, func, dependencies):
        flatten_deps = []
        for d in dependencies:
            if isinstance(d, CombinedDependsFunction) and d._func is func:
                for d2 in d.dependencies:
                    if d2 not in flatten_deps:
                        flatten_deps.append(d2)
            elif d not in flatten_deps:
                flatten_deps.append(d)

        super(CombinedDependsFunction, self).__init__(sandbox, func, flatten_deps)

    @memoize
    def result(self):
        resolved_args = (self.sandbox._value_for(d) for d in self.dependencies)
        return self._func(resolved_args)

    def __eq__(self, other):
        return (
            isinstance(other, self.__class__)
            and self._func is other._func
            and set(self.dependencies) == set(other.dependencies)
        )

    def __hash__(self):
        return object.__hash__(self)

    def __ne__(self, other):
        return not self == other


class SandboxedGlobal(dict):
    """Identifiable dict type for use as function global"""


def forbidden_import(*args, **kwargs):
    raise ImportError("Importing modules is forbidden")


class ConfigureSandbox(dict):
    """Represents a sandbox for executing Python code for build configuration.
    This is a different kind of sandboxing than the one used for moz.build
    processing.

    The sandbox has 9 primitives:
    - option
    - depends
    - template
    - imports
    - include
    - set_config
    - set_define
    - imply_option
    - only_when

    `option`, `include`, `set_config`, `set_define` and `imply_option` are
    functions. `depends`, `template`, and `imports` are decorators. `only_when`
    is a context_manager.

    These primitives are declared as name_impl methods to this class and
    the mapping name -> name_impl is done automatically in __getitem__.

    Additional primitives should be frowned upon to keep the sandbox itself as
    simple as possible. Instead, helpers should be created within the sandbox
    with the existing primitives.

    The sandbox is given, at creation, a dict where the yielded configuration
    will be stored.

        config = {}
        sandbox = ConfigureSandbox(config)
        sandbox.run(path)
        do_stuff(config)
    """

    # The default set of builtins. We expose unicode as str to make sandboxed
    # files more python3-ready.
    BUILTINS = ReadOnlyDict(
        {
            b: getattr(__builtin__, b, None)
            for b in (
                "AssertionError",
                "False",
                "None",
                "True",
                "__build_class__",  # will be None on py2
                "all",
                "any",
                "bool",
                "dict",
                "enumerate",
                "getattr",
                "hasattr",
                "int",
                "isinstance",
                "len",
                "list",
                "max",
                "min",
                "range",
                "reversed",
                "set",
                "sorted",
                "tuple",
                "zip",
            )
        },
        __import__=forbidden_import,
        str=str,
    )

    # Expose a limited set of functions from os.path
    OS = ReadOnlyNamespace(
        path=ReadOnlyNamespace(
            **{
                k: getattr(mozpath, k, getattr(os.path, k))
                for k in (
                    "abspath",
                    "basename",
                    "dirname",
                    "isabs",
                    "join",
                    "normcase",
                    "normpath",
                    "realpath",
                    "relpath",
                )
            }
        )
    )

    def __init__(
        self,
        config,
        environ=os.environ,
        argv=sys.argv,
        stdout=sys.stdout,
        stderr=sys.stderr,
        logger=None,
    ):
        dict.__setitem__(self, "__builtins__", self.BUILTINS)

        self._environ = dict(environ)

        self._paths = []
        self._all_paths = set()
        self._templates = set()
        # Associate SandboxDependsFunctions to DependsFunctions.
        self._depends = OrderedDict()
        self._seen = set()
        # Store the @imports added to a given function.
        self._imports = {}

        self._options = OrderedDict()
        # Store raw option (as per command line or environment) for each Option
        self._raw_options = OrderedDict()

        # Store options added with `imply_option`, and the reason they were
        # added (which can either have been given to `imply_option`, or
        # inferred. Their order matters, so use a list.
        self._implied_options = []

        # Store all results from _prepare_function
        self._prepared_functions = set()

        # Queue of functions to execute, with their arguments
        self._execution_queue = []

        # Store the `when`s associated to some options.
        self._conditions = {}

        # A list of conditions to apply as a default `when` for every *_impl()
        self._default_conditions = []

        self._helper = CommandLineHelper(environ, argv)

        assert isinstance(config, dict)
        self._config = config

        # Tracks how many templates "deep" we are in the stack.
        self._template_depth = 0

        logging.addLevelName(TRACE, "TRACE")
        if logger is None:
            logger = moz_logger = logging.getLogger("moz.configure")
            logger.setLevel(logging.DEBUG)
            formatter = logging.Formatter("%(levelname)s: %(message)s")
            handler = ConfigureOutputHandler(stdout, stderr)
            handler.setFormatter(formatter)
            queue_debug = handler.queue_debug
            logger.addHandler(handler)

        else:
            assert isinstance(logger, logging.Logger)
            moz_logger = None

            @contextmanager
            def queue_debug():
                yield

        self._logger = logger

        # Some callers will manage to log a bytestring with characters in it
        # that can't be converted to ascii. Make our log methods robust to this
        # by detecting the encoding that a producer is likely to have used.
        encoding = getpreferredencoding()

        def wrapped_log_method(logger, key):
            method = getattr(logger, key)

            def wrapped(*args, **kwargs):
                out_args = [
                    (
                        six.ensure_text(arg, encoding=encoding or "utf-8")
                        if isinstance(arg, bytes)
                        else arg
                    )
                    for arg in args
                ]
                return method(*out_args, **kwargs)

            return wrapped

        log_namespace = {
            k: wrapped_log_method(logger, k)
            for k in ("debug", "info", "warning", "error")
        }
        log_namespace["queue_debug"] = queue_debug
        self.log_impl = ReadOnlyNamespace(**log_namespace)

        self._help = None
        self._help_option = self.option_impl(
            "--help", help="Print this message", category=HELP_OPTIONS_CATEGORY
        )
        self._seen.add(self._help_option)

        self._always = DependsFunction(self, lambda: True, [])
        self._never = DependsFunction(self, lambda: False, [])

        if self._value_for(self._help_option):
            self._help = HelpFormatter(argv[0])
            self._help.add(self._help_option)
        elif moz_logger:
            handler = logging.FileHandler(
                "config.log", mode="w", delay=True, encoding="utf-8"
            )
            handler.setFormatter(formatter)
            logger.addHandler(handler)

    def include_file(self, path):
        """Include one file in the sandbox. Users of this class probably want
        to use `run` instead.

        Note: this will execute all template invocations, as well as @depends
        functions that depend on '--help', but nothing else.
        """

        if self._paths:
            path = mozpath.join(mozpath.dirname(self._paths[-1]), path)
            path = mozpath.normpath(path)
            if not mozpath.basedir(path, (mozpath.dirname(self._paths[0]),)):
                raise ConfigureError(
                    "Cannot include `%s` because it is not in a subdirectory "
                    "of `%s`" % (path, mozpath.dirname(self._paths[0]))
                )
        else:
            path = mozpath.realpath(mozpath.abspath(path))
        if path in self._all_paths:
            raise ConfigureError(
                "Cannot include `%s` because it was included already." % path
            )
        self._paths.append(path)
        self._all_paths.add(path)

        with open(path, "rb") as fh:
            source = fh.read()

        code = self.get_compiled_source(source, path)
        exec(code, self)

        self._paths.pop(-1)

    @staticmethod
    @memoize
    def get_compiled_source(source, path):
        return compile(source, path, "exec")

    def run(self, path=None):
        """Executes the given file within the sandbox, as well as everything
        pending from any other included file, and ensure the overall
        consistency of the executed script(s)."""
        if path:
            self.include_file(path)

        for option in self._options.values():
            # All options must be referenced by some @depends function
            if option not in self._seen:
                raise ConfigureError(
                    "Option `%s` is not handled ; reference it with a @depends"
                    % option.option
                )

            self._value_for(option)

        # All implied options should exist.
        for implied_option in self._implied_options:
            value = self._resolve(implied_option.value)
            if value is not None:
                # There are two ways to end up here: either the implied option
                # is unknown, or it's known but there was a dependency loop
                # that prevented the implication from being applied.
                option = self._options.get(implied_option.name)
                if not option:
                    raise ConfigureError(
                        "`%s`, emitted from `%s` line %d, is unknown."
                        % (
                            implied_option.option,
                            implied_option.caller[0],
                            implied_option.caller[1],
                        )
                    )
                # If the option is known, check that the implied value doesn't
                # conflict with what value was attributed to the option.
                if implied_option.when and not self._value_for(implied_option.when):
                    continue
                option_value = self._value_for_option(option)
                if value != option_value:
                    reason = implied_option.reason
                    if isinstance(reason, Option):
                        reason = self._raw_options.get(reason) or reason.option
                        reason = reason.split("=", 1)[0]
                    value = OptionValue.from_(value)
                    raise InvalidOptionError(
                        "'%s' implied by '%s' conflicts with '%s' from the %s"
                        % (
                            value.format(option.option),
                            reason,
                            option_value.format(option.option),
                            option_value.origin,
                        )
                    )

        # All options should have been removed (handled) by now.
        for arg in self._helper:
            without_value = arg.split("=", 1)[0]
            msg = "Unknown option: %s" % without_value
            if self._help:
                self._logger.warning(msg)
            else:
                raise InvalidOptionError(msg)

        # Run the execution queue
        for func, args in self._execution_queue:
            func(*args)

        if self._help:
            with LineIO(self.log_impl.info) as out:
                self._help.usage(out)

    def __getitem__(self, key):
        impl = "%s_impl" % key
        func = getattr(self, impl, None)
        if func:
            return func

        return super(ConfigureSandbox, self).__getitem__(key)

    def __setitem__(self, key, value):
        if (
            key in self.BUILTINS
            or key == "__builtins__"
            or hasattr(self, "%s_impl" % key)
        ):
            raise KeyError("Cannot reassign builtins")

        if inspect.isfunction(value) and value not in self._templates:
            value = self._prepare_function(value)

        elif (
            not isinstance(value, SandboxDependsFunction)
            and value not in self._templates
            and not (inspect.isclass(value) and issubclass(value, Exception))
        ):
            raise KeyError(
                "Cannot assign `%s` because it is neither a "
                "@depends nor a @template" % key
            )

        if isinstance(value, SandboxDependsFunction):
            self._depends[value].name = key

        return super(ConfigureSandbox, self).__setitem__(key, value)

    def _resolve(self, arg):
        if isinstance(arg, SandboxDependsFunction):
            return self._value_for_depends(self._depends[arg])
        return arg

    def _value_for(self, obj):
        if isinstance(obj, SandboxDependsFunction):
            assert obj in self._depends
            return self._value_for_depends(self._depends[obj])

        elif isinstance(obj, DependsFunction):
            return self._value_for_depends(obj)

        elif isinstance(obj, Option):
            return self._value_for_option(obj)

        assert False

    @memoize
    def _value_for_depends(self, obj):
        value = obj.result()
        self._logger.log(TRACE, "%r = %r", obj, value)
        return value

    @memoize
    def _value_for_option(self, option):
        implied = {}
        matching_implied_options = [
            o for o in self._implied_options if o.name in (option.name, option.env)
        ]
        # Update self._implied_options before going into the loop with the non-matching
        # options.
        self._implied_options = [
            o for o in self._implied_options if o.name not in (option.name, option.env)
        ]

        for implied_option in matching_implied_options:
            if implied_option.when and not self._value_for(implied_option.when):
                continue

            value = self._resolve(implied_option.value)

            if value is not None:
                value = OptionValue.from_(value)
                opt = value.format(implied_option.option)
                self._helper.add(opt, "implied")
                implied[opt] = implied_option

        try:
            value, option_string = self._helper.handle(option)
        except ConflictingOptionError as e:
            reason = implied[e.arg].reason
            if isinstance(reason, Option):
                reason = self._raw_options.get(reason) or reason.option
                reason = reason.split("=", 1)[0]
            raise InvalidOptionError(
                "'%s' implied by '%s' conflicts with '%s' from the %s"
                % (e.arg, reason, e.old_arg, e.old_origin)
            )

        if value.origin == "implied":
            recursed_value = getattr(self, "__value_for_option").get((option,))
            if recursed_value is not None:
                filename, line = implied[value.format(option.option)].caller
                raise ConfigureError(
                    "'%s' appears somewhere in the direct or indirect dependencies when "
                    "resolving imply_option at %s:%d" % (option.option, filename, line)
                )

        if option_string:
            self._raw_options[option] = option_string

        when = self._conditions.get(option)
        # If `when` resolves to a false-ish value, we always return None.
        # This makes option(..., when='--foo') equivalent to
        # option(..., when=depends('--foo')(lambda x: x)).
        if when and not self._value_for(when) and value is not None:
            # If the option was passed explicitly, we throw an error that
            # the option is not available. Except when the option was passed
            # from the environment, because that would be too cumbersome.
            if value.origin not in ("default", "environment"):
                raise InvalidOptionError(
                    "%s is not available in this configuration"
                    % option_string.split("=", 1)[0]
                )
            self._logger.log(TRACE, "%r = None", option)
            return None

        self._logger.log(TRACE, "%r = %r", option, value)
        return value

    def _dependency(self, arg, callee_name, arg_name=None):
        if isinstance(arg, str):
            prefix, name, values = Option.split_option(arg)
            if values != ():
                raise ConfigureError("Option must not contain an '='")
            if name not in self._options:
                raise ConfigureError(
                    "'%s' is not a known option. " "Maybe it's declared too late?" % arg
                )
            arg = self._options[name]
            self._seen.add(arg)
        elif isinstance(arg, SandboxDependsFunction):
            assert arg in self._depends
            arg = self._depends[arg]
        else:
            raise TypeError(
                "Cannot use object of type '%s' as %sargument to %s"
                % (
                    type(arg).__name__,
                    "`%s` " % arg_name if arg_name else "",
                    callee_name,
                )
            )
        return arg

    def _normalize_when(self, when, callee_name):
        if when is True:
            when = self._always
        elif when is False:
            when = self._never
        elif when is not None:
            when = self._dependency(when, callee_name, "when")

        if self._default_conditions:
            # Create a pseudo @depends function for the combination of all
            # default conditions and `when`.
            dependencies = [when] if when else []
            dependencies.extend(self._default_conditions)
            if len(dependencies) == 1:
                return dependencies[0]
            return CombinedDependsFunction(self, all, dependencies)
        return when

    @contextmanager
    def only_when_impl(self, when):
        """Implementation of only_when()

        `only_when` is a context manager that essentially makes calls to
        other sandbox functions within the context block ignored.
        """
        when = self._normalize_when(when, "only_when")
        if when and self._default_conditions[-1:] != [when]:
            self._default_conditions.append(when)
            yield
            self._default_conditions.pop()
        else:
            yield

    def option_impl(self, *args, **kwargs):
        """Implementation of option()
        This function creates and returns an Option() object, passing it the
        resolved arguments (uses the result of functions when functions are
        passed). In most cases, the result of this function is not expected to
        be used.
        Command line argument/environment variable parsing for this Option is
        handled here.
        """
        when = self._normalize_when(kwargs.get("when"), "option")
        args = [self._resolve(arg) for arg in args]
        kwargs = {k: self._resolve(v) for k, v in kwargs.items() if k != "when"}
        # The Option constructor needs to look up the stack to infer a category
        # for the Option, since the category is based on the filename where the
        # Option is defined. However, if the Option is defined in a template, we
        # want the category to reference the caller of the template rather than
        # the caller of the option() function.
        kwargs["define_depth"] = self._template_depth * 3
        option = Option(*args, **kwargs)
        if when:
            self._conditions[option] = when
        if option.name in self._options:
            raise ConfigureError("Option `%s` already defined" % option.option)
        if option.env in self._options:
            raise ConfigureError("Option `%s` already defined" % option.env)
        if option.name:
            self._options[option.name] = option
        if option.env:
            self._options[option.env] = option

        if self._help and (when is None or self._value_for(when)):
            self._help.add(option)

        return option

    def depends_impl(self, *args, **kwargs):
        """Implementation of @depends()
        This function is a decorator. It returns a function that subsequently
        takes a function and returns a dummy function. The dummy function
        identifies the actual function for the sandbox, while preventing
        further function calls from within the sandbox.

        @depends() takes a variable number of option strings or dummy function
        references. The decorated function is called as soon as the decorator
        is called, and the arguments it receives are the OptionValue or
        function results corresponding to each of the arguments to @depends.
        As an exception, when a HelpFormatter is attached, only functions that
        have '--help' in their @depends argument list are called.

        The decorated function is altered to use a different global namespace
        for its execution. This different global namespace exposes a limited
        set of functions from os.path.
        """
        for k in kwargs:
            if k != "when":
                raise TypeError(
                    "depends_impl() got an unexpected keyword argument '%s'" % k
                )

        when = self._normalize_when(kwargs.get("when"), "@depends")

        if not when and not args:
            raise ConfigureError("@depends needs at least one argument")

        dependencies = tuple(self._dependency(arg, "@depends") for arg in args)

        conditions = [
            self._conditions[d]
            for d in dependencies
            if d in self._conditions and isinstance(d, Option)
        ]
        for c in conditions:
            if c != when:
                raise ConfigureError(
                    "@depends function needs the same `when` "
                    "as options it depends on"
                )

        def decorator(func):
            if inspect.isgeneratorfunction(func):
                raise ConfigureError(
                    "Cannot decorate generator functions with @depends"
                )
            if inspect.isroutine(func):
                if func in self._templates:
                    raise TypeError("Cannot use a @template function here")
                func = self._prepare_function(func)
            elif isinstance(func, SandboxDependsFunction):
                raise TypeError("Cannot nest @depends functions")
            elif dependencies:
                raise TypeError(
                    "Cannot wrap literal values in @depends with dependencies"
                )
            depends = DependsFunction(self, func, dependencies, when=when)
            return depends.sandboxed

        return decorator

    def include_impl(self, what, when=None):
        """Implementation of include().
        Allows to include external files for execution in the sandbox.
        It is possible to use a @depends function as argument, in which case
        the result of the function is the file name to include. This latter
        feature is only really meant for --enable-application/--enable-project.
        """
        with self.only_when_impl(when):
            what = self._resolve(what)
            if what:
                if not isinstance(what, str):
                    raise TypeError("Unexpected type: '%s'" % type(what).__name__)
                self.include_file(what)

    def template_impl(self, func):
        """Implementation of @template.
        This function is a decorator. Template functions are called
        immediately. They are altered so that their global namespace exposes
        a limited set of functions from os.path, as well as `depends` and
        `option`.
        Templates allow to simplify repetitive constructs, or to implement
        helper decorators and somesuch.
        """

        def update_globals(glob):
            glob.update(
                (k[: -len("_impl")], getattr(self, k))
                for k in dir(self)
                if k.endswith("_impl") and k != "template_impl"
            )
            glob.update((k, v) for k, v in self.items() if k not in glob)

        template = self._prepare_function(func, update_globals)

        # Any function argument to the template must be prepared to be sandboxed.
        # If the template itself returns a function (in which case, it's very
        # likely a decorator), that function must be prepared to be sandboxed as
        # well.
        def wrap_template(template):
            isfunction = inspect.isfunction

            def maybe_prepare_function(obj):
                if isfunction(obj):
                    return self._prepare_function(obj)
                return obj

            # The following function may end up being prepared to be sandboxed,
            # so it mustn't depend on anything from the global scope in this
            # file. It can however depend on variables from the closure, thus
            # maybe_prepare_function and isfunction are declared above to be
            # available there.
            @self.wraps(template)
            def wrapper(*args, **kwargs):
                args = [maybe_prepare_function(arg) for arg in args]
                kwargs = {k: maybe_prepare_function(v) for k, v in kwargs.items()}
                self._template_depth += 1
                ret = template(*args, **kwargs)
                self._template_depth -= 1
                if isfunction(ret):
                    # We can't expect the sandboxed code to think about all the
                    # details of implementing decorators, so do some of the
                    # work for them. If the function takes exactly one function
                    # as argument and returns a function, it must be a
                    # decorator, so mark the returned function as wrapping the
                    # function passed in.
                    if len(args) == 1 and not kwargs and isfunction(args[0]):
                        ret = self.wraps(args[0])(ret)
                    return wrap_template(ret)
                return ret

            return wrapper

        wrapper = wrap_template(template)
        self._templates.add(wrapper)
        return wrapper

    def wraps(self, func):
        return wraps(func)

    RE_MODULE = re.compile(r"^[a-zA-Z0-9_.]+$")

    def imports_impl(self, _import, _from=None, _as=None):
        """Implementation of @imports.
        This decorator imports the given _import from the given _from module
        optionally under a different _as name.
        The options correspond to the various forms for the import builtin.

            @imports('sys')
            @imports(_from='mozpack', _import='path', _as='mozpath')
        """
        for value, required in ((_import, True), (_from, False), (_as, False)):
            if not isinstance(value, str) and (required or value is not None):
                raise TypeError("Unexpected type: '%s'" % type(value).__name__)
            if value is not None and not self.RE_MODULE.match(value):
                raise ValueError("Invalid argument to @imports: '%s'" % value)
        if _as and "." in _as:
            raise ValueError("Invalid argument to @imports: '%s'" % _as)

        def decorator(func):
            if func in self._templates:
                raise ConfigureError("@imports must appear after @template")
            if func in self._depends:
                raise ConfigureError("@imports must appear after @depends")
            # For the imports to apply in the order they appear in the
            # .configure file, we accumulate them in reverse order and apply
            # them later.
            imports = self._imports.setdefault(func, [])
            imports.insert(0, (_from, _import, _as))
            return func

        return decorator

    def _apply_imports(self, func, glob):
        for _from, _import, _as in self._imports.pop(func, ()):
            self._get_one_import(_from, _import, _as, glob)

    def _handle_wrapped_import(self, _from, _import, _as, glob):
        """Given the name of a module, "import" a mocked package into the glob
        iff the module is one that we wrap (either for the sandbox or for the
        purpose of testing). Applies if the wrapped module is exposed by an
        attribute of `self`.

        For example, if the import statement is `from os import environ`, then
        this function will set
        glob['environ'] = self._wrapped_os.environ.

        Iff this function handles the given import, return True.
        """
        module = (_from or _import).split(".")[0]
        attr = "_wrapped_" + module
        wrapped = getattr(self, attr, None)
        if wrapped:
            if _as or _from:
                obj = self._recursively_get_property(
                    module, (_from + "." if _from else "") + _import, wrapped
                )
                glob[_as or _import] = obj
            else:
                glob[module] = wrapped
            return True
        else:
            return False

    def _recursively_get_property(self, module, what, wrapped):
        """Traverse the wrapper object `wrapped` (which represents the module
        `module`) and return the property represented by `what`, which may be a
        series of nested attributes.

        For example, if `module` is 'os' and `what` is 'os.path.join',
        return `wrapped.path.join`.
        """
        if what == module:
            return wrapped
        assert what.startswith(module + ".")
        attrs = what[len(module + ".") :].split(".")
        for attr in attrs:
            wrapped = getattr(wrapped, attr)
        return wrapped

    @memoized_property
    def _wrapped_os(self):
        wrapped_os = {}
        exec("from os import *", {}, wrapped_os)
        # Special case os and os.environ so that os.environ is our copy of
        # the environment.
        wrapped_os["environ"] = self._environ
        # Also override some os.path functions with ours.
        wrapped_path = {}
        exec("from os.path import *", {}, wrapped_path)
        wrapped_path.update(self.OS.path.__dict__)
        wrapped_os["path"] = ReadOnlyNamespace(**wrapped_path)
        return ReadOnlyNamespace(**wrapped_os)

    @memoized_property
    def _wrapped_subprocess(self):
        wrapped_subprocess = {}
        exec("from subprocess import *", {}, wrapped_subprocess)

        def wrap(function):
            def wrapper(*args, **kwargs):
                if kwargs.get("env") is None and self._environ:
                    kwargs["env"] = dict(self._environ)

                return function(*args, **kwargs)

            return wrapper

        for f in ("call", "check_call", "check_output", "Popen", "run"):
            # `run` is new to python 3.5. In case this still runs from python2
            # code, avoid failing here.
            if f in wrapped_subprocess:
                wrapped_subprocess[f] = wrap(wrapped_subprocess[f])

        return ReadOnlyNamespace(**wrapped_subprocess)

    @memoized_property
    def _wrapped_six(self):
        if six.PY3:
            return six
        wrapped_six = {}
        exec("from six import *", {}, wrapped_six)
        wrapped_six_moves = {}
        exec("from six.moves import *", {}, wrapped_six_moves)
        wrapped_six_moves_builtins = {}
        exec("from six.moves.builtins import *", {}, wrapped_six_moves_builtins)

        # Special case for the open() builtin, because otherwise, using it
        # fails with "IOError: file() constructor not accessible in
        # restricted mode". We also make open() look more like python 3's,
        # decoding to unicode strings unless the mode says otherwise.
        def wrapped_open(name, mode=None, buffering=None):
            args = (name,)
            kwargs = {}
            if buffering is not None:
                kwargs["buffering"] = buffering
            if mode is not None:
                args += (mode,)
                if "b" in mode:
                    return open(*args, **kwargs)
            kwargs["encoding"] = system_encoding
            return codecs.open(*args, **kwargs)

        wrapped_six_moves_builtins["open"] = wrapped_open
        wrapped_six_moves["builtins"] = ReadOnlyNamespace(**wrapped_six_moves_builtins)
        wrapped_six["moves"] = ReadOnlyNamespace(**wrapped_six_moves)

        return ReadOnlyNamespace(**wrapped_six)

    def _get_one_import(self, _from, _import, _as, glob):
        """Perform the given import, placing the result into the dict glob."""
        if not _from and _import == "__builtin__":
            raise Exception("Importing __builtin__ is forbidden")
        if _from == "__builtin__":
            _from = "six.moves.builtins"
        # The special `__sandbox__` module gives access to the sandbox
        # instance.
        if not _from and _import == "__sandbox__":
            glob[_as or _import] = self
            return
        if self._handle_wrapped_import(_from, _import, _as, glob):
            return
        # If we've gotten this far, we should just do a normal import.
        # Until this proves to be a performance problem, just construct an
        # import statement and execute it.
        import_line = "%simport %s%s" % (
            ("from %s " % _from) if _from else "",
            _import,
            (" as %s" % _as) if _as else "",
        )
        exec(import_line, {}, glob)

    def _resolve_and_set(self, data, name, value, when=None):
        # Don't set anything when --help was on the command line
        if self._help:
            return
        if when and not self._value_for(when):
            return
        name = self._resolve(name)
        if name is None:
            return
        if not isinstance(name, str):
            raise TypeError("Unexpected type: '%s'" % type(name).__name__)
        if name in data:
            raise ConfigureError(
                "Cannot add '%s' to configuration: Key already " "exists" % name
            )
        value = self._resolve(value)
        if value is not None:
            if self._logger.isEnabledFor(TRACE):
                if data is self._config:
                    self._logger.log(TRACE, "set_config(%s, %r)", name, value)
                elif data is self._config.get("DEFINES"):
                    self._logger.log(TRACE, "set_define(%s, %r)", name, value)
            data[name] = value

    def set_config_impl(self, name, value, when=None):
        """Implementation of set_config().
        Set the configuration items with the given name to the given value.
        Both `name` and `value` can be references to @depends functions,
        in which case the result from these functions is used. If the result
        of either function is None, the configuration item is not set.
        """
        when = self._normalize_when(when, "set_config")

        self._execution_queue.append(
            (self._resolve_and_set, (self._config, name, value, when))
        )

    def set_define_impl(self, name, value, when=None):
        """Implementation of set_define().
        Set the define with the given name to the given value. Both `name` and
        `value` can be references to @depends functions, in which case the
        result from these functions is used. If the result of either function
        is None, the define is not set. If the result is False, the define is
        explicitly undefined (-U).
        """
        when = self._normalize_when(when, "set_define")

        defines = self._config.setdefault("DEFINES", {})
        self._execution_queue.append(
            (self._resolve_and_set, (defines, name, value, when))
        )

    def imply_option_impl(self, option, value, reason=None, when=None):
        """Implementation of imply_option().
        Injects additional options as if they had been passed on the command
        line. The `option` argument is a string as in option()'s `name` or
        `env`. The option must be declared after `imply_option` references it.
        The `value` argument indicates the value to pass to the option.
        It can be:
        - True. In this case `imply_option` injects the positive option

          (--enable-foo/--with-foo).
              imply_option('--enable-foo', True)
              imply_option('--disable-foo', True)

          are both equivalent to `--enable-foo` on the command line.

        - False. In this case `imply_option` injects the negative option

          (--disable-foo/--without-foo).
              imply_option('--enable-foo', False)
              imply_option('--disable-foo', False)

          are both equivalent to `--disable-foo` on the command line.

        - None. In this case `imply_option` does nothing.
              imply_option('--enable-foo', None)
              imply_option('--disable-foo', None)

        are both equivalent to not passing any flag on the command line.

        - a string or a tuple. In this case `imply_option` injects the positive
          option with the given value(s).

              imply_option('--enable-foo', 'a')
              imply_option('--disable-foo', 'a')

          are both equivalent to `--enable-foo=a` on the command line.
              imply_option('--enable-foo', ('a', 'b'))
              imply_option('--disable-foo', ('a', 'b'))

          are both equivalent to `--enable-foo=a,b` on the command line.

        Because imply_option('--disable-foo', ...) can be misleading, it is
        recommended to use the positive form ('--enable' or '--with') for
        `option`.

        The `value` argument can also be (and usually is) a reference to a
        @depends function, in which case the result of that function will be
        used as per the descripted mapping above.

        The `reason` argument indicates what caused the option to be implied.
        It is necessary when it cannot be inferred from the `value`.
        """

        when = self._normalize_when(when, "imply_option")

        # Don't do anything when --help was on the command line
        if self._help:
            return
        if not reason and isinstance(value, SandboxDependsFunction):
            deps = self._depends[value].dependencies
            possible_reasons = [d for d in deps if d != self._help_option]
            if len(possible_reasons) == 1:
                if isinstance(possible_reasons[0], Option):
                    reason = possible_reasons[0]
        frame = inspect.currentframe()
        line = frame.f_back.f_lineno
        filename = frame.f_back.f_code.co_filename
        if not reason and isinstance(value, (bool, tuple, str)):
            # A reason can be provided automatically when imply_option
            # is called with an immediate value.
            reason = "imply_option at %s:%s" % (filename, line)

        if not reason:
            raise ConfigureError(
                "Cannot infer what implies '%s'. Please add a `reason` to "
                "the `imply_option` call." % option
            )

        prefix, name, values = Option.split_option(option)
        if values != ():
            raise ConfigureError("Implied option must not contain an '='")

        self._implied_options.append(
            ReadOnlyNamespace(
                option=option,
                prefix=prefix,
                name=name,
                value=value,
                caller=(filename, line),
                reason=reason,
                when=when,
            )
        )

    def _prepare_function(self, func, update_globals=None):
        """Alter the given function global namespace with the common ground
        for @depends, and @template.
        """
        if not inspect.isfunction(func):
            raise TypeError("Unexpected type: '%s'" % type(func).__name__)
        if func in self._prepared_functions:
            return func

        glob = SandboxedGlobal(
            (k, v)
            for k, v in func.__globals__.items()
            if (isinstance(v, types.FunctionType) and v not in self._templates)
            or (isinstance(v, type) and issubclass(v, Exception))
        )
        glob.update(
            __builtins__=self.BUILTINS,
            __file__=self._paths[-1] if self._paths else "",
            __name__=self._paths[-1] if self._paths else "",
            os=self.OS,
            log=self.log_impl,
            namespace=ReadOnlyNamespace,
        )
        if update_globals:
            update_globals(glob)

        # The execution model in the sandbox doesn't guarantee the execution
        # order will always be the same for a given function, and if it uses
        # variables from a closure that are changed after the function is
        # declared, depending when the function is executed, the value of the
        # variable can differ. For consistency, we force the function to use
        # the value from the earliest it can be run, which is at declaration.
        # Note this is not entirely bullet proof (if the value is e.g. a list,
        # the list contents could have changed), but covers the bases.
        closure = None
        if func.__closure__:

            def makecell(content):
                def f():
                    content

                return f.__closure__[0]

            closure = tuple(makecell(cell.cell_contents) for cell in func.__closure__)

        new_func = self.wraps(func)(
            types.FunctionType(
                func.__code__, glob, func.__name__, func.__defaults__, closure
            )
        )

        @self.wraps(new_func)
        def wrapped(*args, **kwargs):
            if func in self._imports:
                self._apply_imports(func, glob)
            return new_func(*args, **kwargs)

        self._prepared_functions.add(wrapped)
        return wrapped

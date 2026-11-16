# {mod}`mozlog` --- Structured logging for test output

{py:mod}`mozlog` is a library designed for logging the
execution and results of test harnesses. The internal data model is a
stream of JSON-compatible objects, with one object per log entry. The
default output format is line-based, with one JSON object serialized
per line.

{py:mod}`mozlog` is *not* based on the stdlib logging
module, although it shares several concepts with it.

One notable difference between this module and the standard logging
module is the way that loggers are created. The structured logging
module does not require that loggers with a specific name are
singleton objects accessed through a factory function. Instead the
`StructuredLogger` constructor may be used directly. However all
loggers with the same name share the same internal state (the "Borg"
pattern). In particular the list of handler functions is the same for
all loggers with the same name.

Typically, you would only instantiate one logger object per
program. Two convenience methods are provided to set and get the
default logger in the program.

Logging is threadsafe, with access to handlers protected by a
`threading.Lock`. However it is `not` process-safe. This means that
applications using multiple processes, e.g. via the
`multiprocessing` module, should arrange for all logging to happen in
a single process.

## Data Format

Structured loggers produce messages in a simple format designed to be
compatible with the JSON data model. Each message is a single object,
with the type of message indicated by the `action` key. It is
intended that the set of `action` values be closed; where there are
use cases for additional values they should be integrated into this
module rather than extended in an ad-hoc way. The set of keys present
on on all messages is:

`action`

: The type of the message (string).

`time`

: The timestamp of the message in ms since the epoch (int).

`thread`

: The name of the thread emitting the message (string).

`pid`

: The pid of the process creating the message (int).

`source`

: Name of the logger creating the message (string).

For each `action` there are is a further set of specific fields
describing the details of the event that caused the message to be
emitted:

`suite_start`

: Emitted when the testsuite starts running.

  `tests`

  : A dict of test ids keyed by group. Groups are any logical grouping
    of tests, for example a manifest, directory or tag. For convenience,
    a list of test ids can be used instead. In this case all tests will
    automatically be placed in the 'default' group name. Test ids can
    either be strings or lists of strings (an example of the latter is
    reftests where the id has the form [test_url, ref_type, ref_url]).
    Test ids are assumed to be unique within a given testsuite. In cases
    where the test list is not known upfront an empty dict or list may
    be passed (dict).

  `name`

  : An optional string to identify the suite by.

  `run_info`

  : An optional dictionary describing the properties of the
    build and test environment. This contains the information provided
    by {doc}`mozinfo <mozinfo>`, plus a boolean `debug` field indicating
    whether the build under test is a debug build.

`suite_end`

: Emitted when the testsuite is finished and no more results will be produced.

`test_start`

: Emitted when a test is being started.

  `test`

  : A unique id for the test (string or list of strings).

  `path`

  : Optional path to the test relative to some base (typically the root of the
    source tree). Mainly used when `test` id is not a path (string).

`test_status`

: Emitted for a test which has subtests to record the result of a
  single subtest.

  `test`

  : The same unique id for the test as in the `test_start` message.

  `subtest`

  : Name of the subtest (string).

  `status`

  : Result of the test (string enum; `PASS`, `FAIL`,
    `PRECONDITION_FAILED`, `TIMEOUT`, `NOTRUN`)

  `expected`

  : Expected result of the test. Omitted if the expected result is the
    same as the actual result (string enum, same as `status`).

  `known_intermittent`

  : A list of known intermittent statuses for that test. Omitted if there are
    no intermittent statuses expected. (items in the list are string enum, same as `status`)

  `will_retry`

  : True when the harness will retry the test, so that this result does not
    count towards the run. Omitted otherwise.

`test_end`

: Emitted to give the result of a test with no subtests, or the status
  of the overall file when there are subtests.

  `test`

  : The same unique id for the test as in the `test_start` message.

  `status`

  : Either result of the test (if there are no subtests) in which case
    (string enum `PASS`, `FAIL`, `PRECONDITION_FAILED`,
    `TIMEOUT`, `CRASH`, `ASSERT`, , `SKIP`) or the status of
    the overall file where there are subtests (string enum `OK`,
    `PRECONDITION_FAILED`, `ERROR`, `TIMEOUT`, `CRASH`,
    `ASSERT`, `SKIP`).

  `expected`

  : The expected status, or omitted if the expected status matches the
    actual status (string enum, same as `status`).

  `known_intermittent`

  : A list of known intermittent statuses for that test. Omitted if there are
    no intermittent statuses expected. (items in the list are string enum, same as `status`)

  `will_retry`

  : True when the harness will retry the test, so that this result does not
    count towards the run. Omitted otherwise.

`process_output`

: Output from a managed subprocess.

  `process`
  pid of the subprocess.

  `command`
  Command used to launch the subprocess.

  `data`
  Data output by the subprocess.

`log`

: General human-readable logging message, used to debug the harnesses
  themselves rather than to provide input to other tools.

  `level`

  : Level of the log message (string enum `CRITICAL`, `ERROR`,
    `WARNING`, `INFO`, `DEBUG`).

  `message`

  : Text of the log message.

`shutdown`

: This is a special action that can only be logged once per logger state.
  It is sent when calling {meth}`StructuredLogger.shutdown` or implicitly
  when exiting the context manager.

## Testsuite Protocol

When used for testsuites, the following structured logging messages must be emitted:

- One `suite_start` message before any `test_*` messages
- One `test_start` message per test that is run
- One `test_status` message per subtest that is run. This might be
  zero if the test type doesn't have the notion of subtests.
- One `test_end` message per test that is run, after the
  `test_start` and any `test_status` messages for that same test.
- One `suite_end` message after all `test_*` messages have been
  emitted.

The above mandatory events may be interspersed with `process_output`
and `log` events, as required.

### Subtests

The purpose of subtests is to deal with situations where a single test
produces more than one result, and the exact details of the number of
results is not known ahead of time. For example consider a test
harness that loads JavaScript-based tests in a browser. Each url
loaded would be a single test, with corresponding `test_start` and
`test_end` messages. If there can be more than one JS-defined test
on a page, however, it it useful to track the results of those tests
separately. Therefore each of those tests is a subtest, and one
`test_status` message must be generated for each subtest result.

Subtests must have a name that is unique within their parent test.

Whether or not a test has subtests changes the meaning of the
`status` property on the test itself. When the test does not have
any subtests, this property is the actual test result such as `PASS`
or `FAIL` . When a test does have subtests, the test itself does not
have a result as-such; it isn't meaningful to describe it as having a
`PASS` result, especially if the subtests did not all pass. Instead
this property is used to hold information about whether the test ran
without error. If no errors were detected the test must be given the
status `OK`. Otherwise the test may get the status `ERROR` (for
e.g. uncaught JS exceptions), `TIMEOUT` (if no results were reported
in the allowed time) or `CRASH` (if the test caused the process
under test to crash).

## StructuredLogger Objects

```{eval-rst}
.. automodule:: mozlog.structuredlog
  :members: set_default_logger, get_default_logger, LoggerShutdownError
```

```{eval-rst}
.. autoclass:: StructuredLogger
   :members: add_handler, remove_handler, handlers, suite_start,
             suite_end, test_start, test_status, test_end,
             process_output, critical, error, warning, info, debug,
             shutdown
```

```{eval-rst}
.. autoclass:: StructuredLogFileLike
  :members:
```

## ProxyLogger Objects

Since {func}`mozlog.structuredlog.get_default_logger` return None when
the default logger is not initialized, it is not possible to directly
use it at the module level.

With ProxyLogger, it is possible to write the following code:

```
from mozlog import get_proxy_logger

LOG = get_proxy_logger('component_name')


def my_function():
    LOG.info('logging with a module level object')
```

:::{note}
mozlog still needs to be initialized before the first call occurs
to a ProxyLogger instance, for example with
{func}`mozlog.commandline.setup_logging`.
:::

```{eval-rst}
.. automodule:: mozlog.proxy
  :members: get_proxy_logger, ProxyLogger
```

## Handlers

A handler is a callable that is called for each log message produced
and is responsible for handling the processing of that
message. The typical example of this is a `StreamHandler` which takes
a log message, invokes a formatter which converts the log to a string,
and writes it to a file.

```{eval-rst}
.. automodule:: mozlog.handlers
```

```{eval-rst}
.. autoclass:: BaseHandler
  :members:
```

```{eval-rst}
.. autoclass:: StreamHandler
  :members:
```

```{eval-rst}
.. autoclass:: LogLevelFilter
  :members:
```

```{eval-rst}
.. autoclass:: BufferHandler
  :members:
```

## Formatters

Formatters are callables that take a log message, and return either a
string representation of that message, or `None` if that message
should not appear in the output. This allows formatters to both
exclude certain items and create internal buffers of the output so
that, for example, a single string might be returned for a
`test_end` message indicating the overall result of the test,
including data provided in the `test_status` messages.

Formatter modules are written so that they can take raw input on stdin
and write formatted output on stdout. This allows the formatters to be
invoked as part of a command line for post-processing raw log files.

```{eval-rst}
.. automodule:: mozlog.formatters.base
```

```{eval-rst}
.. autoclass:: BaseFormatter
  :members:
```

```{eval-rst}
.. automodule:: mozlog.formatters.unittest
```

```{eval-rst}
.. autoclass:: UnittestFormatter
  :members:
```

```{eval-rst}
.. automodule:: mozlog.formatters.xunit
```

```{eval-rst}
.. autoclass:: XUnitFormatter
  :members:
```

```{eval-rst}
.. automodule:: mozlog.formatters.html
```

```{eval-rst}
.. autoclass:: HTMLFormatter
  :members:
```

```{eval-rst}
.. automodule:: mozlog.formatters.machformatter
```

```{eval-rst}
.. autoclass:: MachFormatter
  :members:
```

```{eval-rst}
.. automodule:: mozlog.formatters.tbplformatter
```

```{eval-rst}
.. autoclass:: TbplFormatter
  :members:
```

## Processing Log Files

The `mozlog.reader` module provides utilities for working
with structured log files.

```{eval-rst}
.. automodule:: mozlog.reader
  :members:
```

## Integration with argparse

The `mozlog.commandline` module provides integration with the `argparse`
module to provide uniform logging-related command line arguments to programs
using `mozlog`. Each known formatter gets a command line argument of the form
`--log-{name}`, which takes the name of a file to log to with that format,
or `-` to indicate stdout.

```{eval-rst}
.. automodule:: mozlog.commandline
  :members:
```

## Simple Examples

Log to stdout:

```
from mozlog import structuredlog
from mozlog import handlers, formatters
logger = structuredlog.StructuredLogger("my-test-suite")
logger.add_handler(handlers.StreamHandler(sys.stdout,
                                          formatters.JSONFormatter()))
logger.suite_start(["test-id-1"])
logger.test_start("test-id-1")
logger.info("This is a message with action='LOG' and level='INFO'")
logger.test_status("test-id-1", "subtest-1", "PASS")
logger.test_end("test-id-1", "OK")
logger.suite_end()
```

Log with a context manager:

```
from mozlog.structuredlog import StructuredLogger
from mozlog.handlers import StreamHandler
from mozlog.formatters import JSONFormatter

with StructuredLogger("my-test-suite") as logger:
    logger.add_handler(StreamHandler(sys.stdout,
                                     JSONFormatter()))
    logger.info("This is an info message")
```

Populate an `argparse.ArgumentParser` with logging options, and
create a logger based on the value of those options, defaulting to
JSON output on stdout if nothing else is supplied:

```
import argparse
from mozlog import commandline

parser = argparse.ArgumentParser()
# Here one would populate the parser with other options
commandline.add_logging_group(parser)

args = parser.parse_args()
logger = commandline.setup_logging("testsuite-name", args, {"raw": sys.stdout})
```

Count the number of tests that timed out in a testsuite:

```
from mozlog import reader

count = 0

def handle_test_end(data):
    global count
    if data["status"] == "TIMEOUT":
        count += 1

reader.each_log(reader.read("my_test_run.log"),
                {"test_end": handle_test_end})

print count
```

## More Complete Example

This example shows a complete toy testharness set up to used
structured logging. It is available as [structured_example.py](_static/structured_example.py):

```{literalinclude} _static/structured_example.py
```

Each global function with a name starting
`test_` represents a test. A passing test returns without
throwing. A failing test throws a {py:class}`TestAssertion` exception
via the {py:func}`assert_equals` function. Throwing anything else is
considered an error in the test. There is also a {py:func}`expected`
decorator that is used to annotate tests that are expected to do
something other than pass.

The main entry point to the test runner is via that {py:func}`main`
function. This is responsible for parsing command line
arguments, and initiating the test run. Although the test harness
itself does not provide any command line arguments, the
{py:class}`ArgumentParser` object is populated by
{py:meth}`commandline.add_logging_group`, which provides a generic
set of structured logging arguments appropriate to all tools producing
structured logging.

The values of these command line arguments are used to create a
{py:class}`mozlog.StructuredLogger` object populated with the
specified handlers and formatters in
{py:func}`commandline.setup_logging`. The third argument to this
function is the default arguments to use. In this case the default
is to output raw (i.e. JSON-formatted) logs to stdout.

The main test harness is provided by the {py:class}`TestRunner`
class. This class is responsible for scheduling all the tests and
logging all the results. It is passed the {py:obj}`logger` object
created from the command line arguments. The {py:meth}`run` method
starts the test run. Before the run is started it logs a
`suite_start` message containing the id of each test that will run,
and after the testrun is done it logs a `suite_end` message.

Individual tests are run in the {py:meth}`run_test` method. For each
test this logs a `test_start` message. It then runs the test and
logs a `test_end` message containing the test name, status, expected
status, and any informational message about the reason for the
result. In this test harness there are no subtests, so the
`test_end` message has the status of the test and there are no
`test_status` messages.

### Example Output

When run without providing any command line options, the raw
structured log messages are sent to stdout:

```
$ python structured_example.py

{"source": "structured-example", "tests": ["test_that_has_an_error", "test_that_fails", "test_expected_fail", "test_that_passes"], "thread": "MainThread", "time": 1401446682787, "action": "suite_start", "pid": 18456}
{"source": "structured-example", "thread": "MainThread", "time": 1401446682787, "action": "log", "message": "Running tests", "level": "INFO", "pid": 18456}
{"source": "structured-example", "test": "test_that_has_an_error", "thread": "MainThread", "time": 1401446682787, "action": "test_start", "pid": 18456}
{"status": "ERROR", "thread": "MainThread", "pid": 18456, "source": "structured-example", "test": "test_that_has_an_error", "time": 1401446682788, "action": "test_end", "message": "Traceback (most recent call last):\n  File \"structured_example.py\", line 61, in run_test\n    func()\n  File \"structured_example.py\", line 31, in test_that_has_an_error\n    assert_equals(2, 1 + \"1\")\nTypeError: unsupported operand type(s) for +: 'int' and 'str'\n", "expected": "PASS"}
{"source": "structured-example", "test": "test_that_fails", "thread": "MainThread", "time": 1401446682788, "action": "test_start", "pid": 18456}
{"status": "FAIL", "thread": "MainThread", "pid": 18456, "source": "structured-example", "test": "test_that_fails", "time": 1401446682788, "action": "test_end", "message": "1 not equal to 2", "expected": "PASS"}
{"source": "structured-example", "test": "test_expected_fail", "thread": "MainThread", "time": 1401446682788, "action": "test_start", "pid": 18456}
{"status": "FAIL", "thread": "MainThread", "pid": 18456, "source": "structured-example", "test": "test_expected_fail", "time": 1401446682788, "action": "test_end", "message": "4 not equal to 5"}
{"source": "structured-example", "test": "test_that_passes", "thread": "MainThread", "time": 1401446682788, "action": "test_start", "pid": 18456}
{"status": "PASS", "source": "structured-example", "test": "test_that_passes", "thread": "MainThread", "time": 1401446682789, "action": "test_end", "pid": 18456}
{"source": "structured-example", "test": "test_with_known_intermittent", "thread": "MainThread", "time": 1401446682789, "action": "test_start", "pid": 18456}
{"status": "FAIL", thread": "MainThread", "pid": 18456, "source": "structured-example", "test": "test_with_known_intermittent", "time": 1401446682790, "action": "test_end", "expected": "PASS", "known_intermittent": ["FAIL", "TIMEOUT"]}
{"action": "suite_end", "source": "structured-example", "pid": 18456, "thread": "MainThread", "time": 1401446682790}
```

The structured logging module provides a number of command line
options:

```
$ python structured_example.py --help

usage: structured_example.py [-h] [--log-unittest LOG_UNITTEST]
                             [--log-raw LOG_RAW] [--log-html LOG_HTML]
                             [--log-xunit LOG_XUNIT]
                             [--log-mach LOG_MACH]

optional arguments:
  -h, --help            show this help message and exit

Output Logging:
  Options for logging output. Each option represents a possible logging
  format and takes a filename to write that format to, or '-' to write to
  stdout.

  --log-unittest LOG_UNITTEST
                        Unittest style output
  --log-raw LOG_RAW     Raw structured log messages
  --log-html LOG_HTML   HTML report
  --log-xunit LOG_XUNIT
                        xUnit compatible XML
  --log-mach LOG_MACH   Human-readable output
```

In order to get human-readable output on stdout and the structured log
data to go to the file `structured.log`, we would run:

```
$ python structured_example.py --log-mach=- --log-raw=structured.log

0:00.00 SUITE_START: MainThread 4
0:01.00 LOG: MainThread INFO Running tests
0:01.00 TEST_START: MainThread test_that_has_an_error
0:01.00 TEST_END: MainThread Harness status ERROR, expected PASS. Subtests passed 0/0. Unexpected 1
0:01.00 TEST_START: MainThread test_that_fails
0:01.00 TEST_END: MainThread Harness status FAIL, expected PASS. Subtests passed 0/0. Unexpected 1
0:01.00 TEST_START: MainThread test_expected_fail
0:02.00 TEST_END: MainThread Harness status FAIL. Subtests passed 0/0. Unexpected 0
0:02.00 TEST_START: MainThread test_that_passes
0:02.00 TEST_END: MainThread Harness status PASS. Subtests passed 0/0. Unexpected 0
0:02.00 SUITE_END: MainThread
```

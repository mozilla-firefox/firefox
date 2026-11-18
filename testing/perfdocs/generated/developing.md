# Developing in MozPerftest

## Architecture overview

`MozPerftest` implements a mach command that is a thin wrapper on the
top of `runner.py`, which allows us to run the tool without having to go through
a mach call. Command arguments are prepared in `argparser.py` and then made
available for the runner.

The runner creates a `MachEnvironment` instance (see `environment.py`) and a
`Metadata` instance (see `metadata.py`). These two objects are shared during the
whole test and used to share data across all parts.

The runner then calls `MachEnvironment.run`, which is in charge of running the test.
The `MachEnvironment` instance runs a sequence of **layers**.

Layers are classes responsible for one single aspect of a performance test. They
are organized in three categories:

- **system**: anything that sets up and tears down some resources or services
  on the system. Existing system layers: **android**, **proxy**
- **test**: layers that are in charge of running a test to collect metrics.
  Existing test layers: **browsertime** and **androidlog**
- **metrics**: all layers that process the metrics to turn them into usable
  metrics. Existing system layers: **perfherder** and **console**

The MachEnvironment instance collects a series of layers for each category and
runs them sequentially.

The goal of this organization is to allow adding new performance tests runners
that will be based on a specific combination of layers. To avoid messy code,
we need to make sure that each layer represents a single aspect of the process
and that is completely independent of other layers (besides sharing the data
through the common environment.)

For instance, we could use `perftest` to run a C++ benchmark by implementing a
new **test** layer.

## Layer

A layer is a class that inherits from `mozperftest.layers.Layer` and implements
a few methods and class variables.

List of methods and variables:

- `name`: name of the layer (class variable, mandatory)
- `activated`: boolean to activate by default the layer (class variable, False)
- `user_exception`: will trigger the `on_exception` hook when an exception occurs
- `arguments`: dict containing arguments. Each argument is following
  the `argparser` standard
- `run(self, metadata)`: called to execute the layer
- `setup(self)`: called when the layer is about to be executed
- `teardown(self)`: called when the layer is exiting

Example:

```
class EmailSender(Layer):
    """Sends an email with the results.
    """
    name = "email"
    activated = False

    arguments = {
        "recipient": {
            "type": str,
            "default": "tarek@mozilla.com",
            "help": "Recipient",
        },
    }

    def setup(self):
        self.server = smtplib.SMTP(smtp_server,port)

    def teardown(self):
        self.server.quit()

    def __call__(self, metadata):
        self.server.send_email(self.get_arg("recipient"), metadata.results())
```

It can then be added to one of the top functions that are used to create a list
of layers for each category:

- **mozperftest.metrics.pick_metrics** for the metrics category
- **mozperftest.system.pick_system** for the system category
- **mozperftest.test.pick_browser** for the test category

And also added in each `get_layers` function in each of those categories.
The `get_layers` functions are invoked when building the argument parser.

In our example, adding the `EmailSender` layer will add two new options:

- **--email** a flag to activate the layer
- **--email-recipient**

## Important layers

**MozPerftest** can be used to run performance tests against browsers using the
**browsertime** test layer. It leverages the [browsertime.js](https://www.sitespeed.io/documentation/browsertime/) framework and provides
a full integration into Mozilla's build and CI systems.

Browsertime uses the Selenium Webdriver client to drive the browser, and
provides some metrics to measure performance during a user journey.

## Coding style

For the coding style, we want to:

- Follow [PEP 257](https://www.python.org/dev/peps/pep-0257/) for docstrings
- Avoid complexity as much as possible
- Use modern Python 3 code (for instance `pathlib` instead of `os.path`)
- Avoid dependencies on Mozilla build projects and frameworks as much as possible
  (mozharness, mozbuild, etc), or make sure they are isolated and documented

## Landing patches

:::{warning}
It is mandatory for each patch to have a test. Any change without a test
will be rejected.
:::

Before landing a patch for MozPerftest, make sure you run `perftest-test`:

```
% ./mach perftest-test
=> remove old coverage data [OK]
=> running tests [OK]
=> coverage
Name                                             Stmts   Miss  Cover   Missing
------------------------------------------------------------------------------------------
mozperftest/metrics/notebook/analyzer.py         29      20     31%    26-36, 39-42, 45-51
...
mozperftest/system/proxy.py                      37      0     100%
------------------------------------------------------------------------------------------
TOTAL                                            1614    240    85%

[OK]
```

The command will run the tests and also make sure that the test coverage has not regressed.
Run `./mach lint python/mozperftest` separately to lint your changes.

You can also pass the name of one single test module, which skips the coverage check.

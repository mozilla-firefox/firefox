# {mod}`mozperfcommon` --- Code shared between the performance test harnesses

Mozperfcommon contains code shared between the performance test harnesses,
such as Raptor and Talos. It currently provides extreme regression
detection which compares the results of a test run with the recent history of the
same tests in Perfherder and raises these regressions as failures.

## Extreme regression detection

{class}`~mozperfcommon.detect.XtremeRegression` compares a perfherder data
blob, following `performance-artifact-schema.json`, with the historical data of
its suites and subtests on a Treeherder repository (`autoland` by default).

Initialize the class with a platform (optionally the branch too), then call `detect`
with the perfherder data that was produced to compare it to historical data. By default,
it compares the suites and subtests that alert in Perfherder with all the pushes of the
last 7 days, and flags regressions when the given data exceeds 10 standard deviations of the
historical data. This returns a list of comparisons, and entries with `exceeds_variance`
set to True are extreme regressions. Errors can then be raised for them and
`XtremeRegressionError` is provided for this.

In CI, the revision being tested is taken from the `GECKO_HEAD_REV` environment
variable. Outside of CI, the history up to now is used.

### Basic usage

```python
from mozperfcommon.detect import XtremeRegression

detector = XtremeRegression(
    "windows11-64-24h2-shippable", revision_repository="autoland"
)
results = detector.detect(perfherder_data)

for result in results:
    if result["exceeds_variance"]:
        print(result["suite"], result["test"], result["percentage"])
```

`detect` returns one dict per compared value, with the statistics of the
comparison:

```python
{
    "suite": "speedometer3",
    "test": "Charts-chartjs/total",  # None for the suite value
    "value": 53.26,
    "new_count": 50,  # Number of new values (replicates)
    "count": 2400,  # Number of historical values
    "signature_id": 5257402,
    "expected_change": None,  # Bug of the matching expected change
    "new_mean": 53.26,
    "historical_mean": 35.73,
    "historical_stdev": 1.31,
    "difference": 17.53,
    "percentage": 49.06,
    "exceeds_variance": True,
    "new_outliers": 0,  # Number of new values ignored as outliers
}
```

`exceeds_variance` is `None` when there isn't enough history to check the
variance, or when an expected change matches.

Suite and subtest values are only compared when the run produced replicates
for them, i.e. at least 2 values. A single value says nothing about the
variance of the run, so those values are left out of the results.

At least 15 historical values are needed to check the variance, since a
comparison with fewer values isn't useful. Tests that don't reach 15 values
within the last 7 days are never flagged. This is
expected for tests that run infrequently or that produce few values per run.

Failures to get the history from Treeherder, e.g. when it is down or when the
revision isn't found on its repository, are logged and the values are then
compared without history.

The signatures are cached per framework, so an `XtremeRegression` instance can
be reused for all the results of a test run.

### Failing on regressions

Pass `raise_for_regressions=True` to `detect` to raise an
{class}`~mozperfcommon.detect.XtremeRegressionError` when any value is flagged.
Harnesses that want to finish their run first can collect the results and call
{meth}`~mozperfcommon.detect.XtremeRegression.raise_for_regressions` later for
the same behaviour.

The first line of the error gives the number of regressions and the 3 largest
ones by percentage, and the following lines give the details of all of them:

```
2 extreme regression(s) detected, top 2: speedometer3 Charts-chartjs/total (+49.06%), speedometer3 (-20.07%)
  speedometer3: difference -5.44 (-20.07%)
  speedometer3 Charts-chartjs/total: difference 17.53 (+49.06%)
```

### Comparisons

The comparison and its threshold are options of `detect`, so they can differ
between perfherder-data blobs:

```python
results = detector.detect(perfherder_data, comparison="median", threshold=4)
```

Two comparisons are available:

- `mean` (default): compares the means, with a threshold in historical standard
  deviations. New values that are more than 5 median absolute deviations worse
  than the median of the new values are ignored as outliers, so that a few bad
  replicates in an otherwise normal run aren't flagged. Their number is
  returned in `new_outliers`. The historical values are all used.
- `median`: compares the medians, with a threshold in historical median
  absolute deviations, which is less sensitive to outliers.

Other comparisons can be added with
{meth}`~mozperfcommon.comparisons.Comparisons.add`. A comparison receives the
new values, the historical values (possibly empty), and the `threshold` and
`lower_is_better` keyword arguments. It returns the fields to add to the
result, which must include `exceeds_variance`. The `exceeds_variance` should be
`True` when something was detected, `False` when something was not, and `None` if
a comparison was not possible for any reason.

```python
from mozperfcommon.comparisons import Comparisons


@Comparisons.add("maximum")
def compare_to_maximum(new_values, historical_values, threshold, lower_is_better):
    ...
    return {"historical_maximum": maximum, "exceeds_variance": exceeds}
```

### Expected changes

Regressions caused on purpose are listed in
`testing/performance/common/expected_changes.yml`. A matching value is still
compared, but never flagged (will be changed in the future). Add an entry
in the patch causing the regression to prevent the regression failures:

```yaml
- bug: 1234567
  # fnmatch pattern of the suite.
  suite: speedometer3
  # Optional fnmatch pattern of the subtests. Without it, the suite value and
  # all the subtests of the suite match.
  test: "TodoMVC-*"
  # Optional fnmatch pattern of the Treeherder machine platform. Without it,
  # all the platforms match.
  platform: "windows11-*"
  # Optional extra options of the suite, which must all match, in any order.
  # Without it, the suite matches whatever its extra options are.
  extra_options: ["fission", "webrender"]
  # Optional fnmatch pattern of the application. Without it, all the
  # applications match.
  application: firefox
  # The entry is ignored for revisions pushed after this date.
  expires: 2026-11-01
```

Remove entries once they have expired. The file is validated when it is
loaded, and an invalid entry raises an
{class}`~mozperfcommon.expected_changes.InvalidExpectedChangeError`.

Outside of a source checkout, e.g. from the test archive in CI, the path of the
file must be given in the `PERF_EXPECTED_CHANGES_PATH` environment variable.
Mozharness sets it for Raptor and Talos.

### Usage in the harnesses

Raptor runs the detection on the results of its browsertime tests, when running
in CI (`RAPTOR_TASK_LABEL` is set) and when the results validate. The platform
comes from the task label, and the repository of the tested revision from
`GECKO_HEAD_REPOSITORY`. Other harnesses will also need the task label so that it
can be used to find historical data. It's up to the harness to determine how to
call the detection and raise the failures, some may want to complete some work
prior to failing (such as Raptor).

## API reference

```{eval-rst}
.. automodule:: mozperfcommon.detect
   :members: XtremeRegression, XtremeRegressionError, should_alert

.. automodule:: mozperfcommon.comparisons
   :members: Comparisons, compare_to_mean, compare_to_median, median_absolute_deviation, UnknownComparisonError

.. automodule:: mozperfcommon.expected_changes
   :members: load_expected_changes, find_expected_change, get_expected_changes_path, InvalidExpectedChangeError

.. automodule:: mozperfcommon.perfherder
   :members: PerfherderClient, PushNotFoundError

.. automodule:: mozperfcommon.utils
   :members: get_ci_revision, get_framework_id, format_date, signature_key, UnknownFrameworkError
```

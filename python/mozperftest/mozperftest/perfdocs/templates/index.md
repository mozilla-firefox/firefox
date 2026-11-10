# Performance Testing

```{toctree}
:glob: true
:hidden: true
:maxdepth: 2

{toctree_documentation}
```

Performance tests are designed to catch performance regressions before they reach our
end users. At this time, there is no unified approach for these types of tests,
but [MozPerftest](/testing/perfdocs/mozperftest.md) aims to provide this in the future.

For more detailed information about each test suite and project, see their documentation:

{test_documentation}

Here are the active PerfTest components/modules and their respective owners:

> - AWFY (Are We Fast Yet) -
>   : - Owner: Beatrice A.
>     - Description: A public dashboard comparing Firefox and Chrome performance metrics
> - AWSY (Are We Slim Yet)
>   : - Owner: Alexandru F.
>     - Co-owner: Andrej
>     - Description: Project that tracks memory usage across builds
> - Raptor
>   : - Owner: Sparky
>     - Co-owner: Kash
>     - Description: Test harness that uses Browsertime (based on webdriver) as the underlying engine to run performance tests
> - CondProf (Conditioned profiles)
>   : - Owner: Sparky
>     - Co-owner: Jmaher
>     - Description: Provides tooling to build, and obtain profiles that are preconditioned in some way.
> - Infrastructure
>   : - Owner: Sparky
>     - Co-owners: Kash, Andrej
>     - Description: All things involving: TaskCluster, Youtube Playback, Bitbar, Mobile Configs, etc...
> - MozPerftest
>   : - Owner: Sparky
>     - Co-owners: Kash, Andrej
>     - Description: Testing framework used to run performance tests
> - MozPerftest Tools
>   : - Owner: Sparky
>     - Co-owner: Kash, Andrej
>     - Description: Various tools used by performance testing team
> - Mozproxy
>   : - Owner: Kash
>     - Co-owner: Sparky
>     - Description: An http proxy used to run tests against third-party websites in a reliable and reproducible way
> - PerfCompare
>   : - Owner: Carla S.
>     - Co-owner: Beatrice A.
>     - Description: Performance comparison tool used to compare performance of different commits within a repository
> - PerfDocs
>   : - Owner: Sparky
>     - Co-owner: Kash, Andrej
>     - Description: Automatically generated performance test engineering documentation
> - PerfHerder
>   : - Owner: Beatrice A.
>     - Co-owner: Andra A.
>     - Description: The framework used by the performance sheriffs to find performance regressions and for storing, and visualizing our performance data.
> - Performance Sheriffing
>   : - Owner: Andra A.
>     - Co-owner: Beatrice A.
>     - Description: Performance sheriffs are responsible for finding commits that cause performance regressions and getting fixes from devs or backing out the changes
> - Talos
>   : - Owner: Sparky
>     - Co-owner: Kash, Andrej
>     - Description: Testing framework used to run Firefox-specific performance tests

You can additionally reach out to our team on
the [#perftest](https://matrix.to/#/#perftest:mozilla.org) channel on matrix

For more information about the performance testing team,
[visit the wiki page](https://wiki.mozilla.org/TestEngineering/Performance).

## Critical Performance Tests

Some of our performance tests are marked as critical tests. These are ones where we do not want to regress their metric under any circumstances unless the regression is explicitly approved by appropriate parties. When one of these tests regress, the regressor patch is backed out immediately by sheriffs after it has been found and confirmed.

The following list contains all of our existing critical tests:

> - [Speedometer 3 on Windows 11](https://treeherder.mozilla.org/perfherder/graphs?highlightAlerts=1&highlightChangelogData=1&highlightCommonAlerts=0&replicates=0&series=autoland,5257591,1,13&timerange=2592000)
> - [NewsSite Applink Startup on Android A55](https://treeherder.mozilla.org/perfherder/graphs?highlightAlerts=1&highlightChangelogData=1&highlightCommonAlerts=0&replicates=0&series=autoland,5310509,1,15&timerange=2592000)

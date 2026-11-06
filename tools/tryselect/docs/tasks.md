# Task Generation

Many selectors (including `chooser`, `coverage` and `fuzzy`) source their available tasks
directly from the {ref}`taskgraph <taskcluster-task-graph-generation>` module by building the taskgraph
locally. This means that the list of available tasks will never be stale. While this is very
powerful, it comes with a large enough performance cost to get annoying (around twenty seconds).

The result of the taskgraph generation will be cached, so this penalty will only be incurred
whenever a file in the `/taskcluster` directory is modified. Unfortunately this directory changes
pretty frequently, so developers can expect to rebuild the cache each time they pull in
`mozilla-central`. Developers who regularly work on `/taskcluster` can expect to rebuild even
more frequently.

## Configuring Watchman

It's possible to bypass this penalty completely by using the file watching service [watchman]. If
you use the `fsmonitor` mercurial extension, you already have `watchman` installed.

:::{note}
If you aren't using [fsmonitor] but end up installng watchman anyway, you
might as well enable it for a faster Mercurial experience.
:::

Otherwise, [install watchman]. If using Linux you'll likely run into the [inotify limits] outlined
on that page due to the size of `mozilla-central`. You can [read this page] for more information
on how to bump the limits permanently.

Next run the following commands:

```shell
cd path/to/mozilla-central
watchman watch .
watchman -j < tools/tryselect/watchman.json
```

:::{note}
PowerShell on Windows does not support the `<` redirection operator. From
PowerShell or `cmd`, pipe the file in instead:

```shell
more tools\tryselect\watchman.json | watchman -j
```
:::

You should see output like:

```json
{
    "triggerid": "rebuild-taskgraph-cache",
    "disposition": "created",
    "version": "20200920.192359.0"
}
```

That's it. Now anytime a file under `/taskcluster` is modified (either by your editor, or by
updating version control), the taskgraph cache will be rebuilt in the background, allowing you to
skip the wait the next time you run `mach try`.

:::{note}
Watchman triggers are persistent and don't need to be added more than once.
See [Managing Triggers] for how to remove a trigger.
:::

You can test that everything is working by running these commands:

```shell
statedir=`mach python -c "from mach.util import get_state_dir; print(get_state_dir(specific_to_topsrcdir=True))"`
rm -rf $statedir/cache/taskgraph
touch taskcluster/mach_commands.py
# wait a minute for generation to trigger and finish
ls $statedir/cache/taskgraph
```

If the `target_task_set` file exists, you are good to go. If not you can look at the `watchman`
log to see if there were any errors. This typically lives somewhere like
`/usr/local/var/run/watchman/$USER-state/log`. In this case please file a bug under `Firefox
Build System :: Try` and include the relevant portion of the log.

### Running Watchman on Startup

Watchman is both a client and a service all in one. When running a `watchman` command, the client
binary will start the service in the background if it isn't running. This means on reboot the
service won't be running and you'll need to start the service each time by invoking the client
binary (e.g by running `watchman version`).

If you'd like this to happen automatically, you can use your favourite platform specific way of
running commands at startup (`crontab`, `rc.local`, etc.). Watchman stores separate state for
each user, so be sure you run the command as the user that set up the triggers.

#### Setting up a systemd Service

If `systemd` is an option you can create a service:

```ini
[Unit]
Description=Watchman for %i
After=network.target

[Service]
Type=simple
User=%i
ExecStart=/usr/local/bin/watchman --log-level 1 watch-list -f
ExecStop=/usr/local/bin/watchman shutdown-server

[Install]
WantedBy=multi-user.target
```

Save this to a file called `/etc/systemd/system/watchman@.service`. Then run:

```shell
sudo systemctl enable watchman@$USER.service
sudo systemctl start watchman@$USER.service
```

The next time you reboot, the watchman service should start automatically.

### Managing Triggers

When adding a trigger watchman writes it to disk. Typically it'll be a path similar to
`/usr/local/var/run/watchman/$USER-state/state`. While editing that file by hand would work, the
watchman binary provides an interface for managing your triggers.

To see all directories you are currently watching:

```shell
watchman watch-list
```

To view triggers that are active in a specified watch:

```shell
watchman trigger-list <path>
```

To delete a trigger from a specified watch:

```shell
watchman trigger-del <path> <name>
```

In the above two examples, replace `<path>` with the path of the watch, presumably
`mozilla-central`. Using `.` works as well if that is already your working directory. For more
information on managing triggers and a reference of other commands, see the [official docs].

[fsmonitor]: https://www.mercurial-scm.org/wiki/FsMonitorExtension
[inotify limits]: https://facebook.github.io/watchman/docs/install.html#linux-inotify-limits
[install watchman]: https://facebook.github.io/watchman/docs/install.html
[official docs]: https://facebook.github.io/watchman/docs/cmd/trigger.html
[read this page]: https://github.com/guard/listen/blob/master/README.md#increasing-the-amount-of-inotify-watchers
[this hint]: https://github.com/facebook/watchman/commit/2985377eaf8c8538b28fae9add061b67991a87c2
[watchman]: https://facebook.github.io/watchman/

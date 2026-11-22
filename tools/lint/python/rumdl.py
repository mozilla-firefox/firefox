# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import re
import signal
import subprocess

from mozlint import result

here = os.path.abspath(os.path.dirname(__file__))
CONFIG_PATH = os.path.join(os.path.dirname(here), "rumdl.toml")

FIXED_RE = re.compile(r"Fixed: (\d+)/\d+ issues?")
VERSION_RE = re.compile(r"rumdl[^\d]*([0-9][0-9.]*)")


def get_rumdl_version(binary):
    try:
        output = subprocess.check_output(
            [binary, "--version"],
            stderr=subprocess.STDOUT,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        output = e.output

    match = VERSION_RE.search(output)
    if match:
        return match.group(1)
    print(f"Error: Could not parse the version '{output}'")


def run_process(cmd, log):
    orig = signal.signal(signal.SIGINT, signal.SIG_IGN)
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    signal.signal(signal.SIGINT, orig)
    try:
        stdout, stderr = proc.communicate()
    except KeyboardInterrupt:
        proc.kill()
        return "", -1

    for line in stderr.splitlines():
        if line:
            log.debug(line)
    return stdout, proc.returncode


def lint(paths, config, log, **lintargs):
    if not paths:
        return {"results": [], "fixed": 0}

    log.debug(f"rumdl version {get_rumdl_version('rumdl')}")

    base_args = ["rumdl", "check", f"--config={CONFIG_PATH}"]
    if config.get("exclude"):
        base_args.append(f"--exclude={','.join(config['exclude'])}")

    fixed = 0
    if lintargs.get("fix"):
        fix_args = base_args + ["--fix"] + paths
        log.debug(f"Running --fix: {fix_args}")
        output, _ = run_process(fix_args, log)
        match = FIXED_RE.search(output)
        if match:
            fixed = int(match.group(1))

    args = base_args + ["--output-format=json"] + paths
    log.debug(f"Running with args: {args}")
    output, _ = run_process(args, log)
    output = output.strip()
    if not output:
        return {"results": [], "fixed": fixed}

    try:
        issues = json.loads(output)
    except json.JSONDecodeError:
        log.error(f"could not parse output: {output}")
        return {"results": [], "fixed": fixed}

    results = [
        result.from_config(
            config,
            path=issue.get("file"),
            lineno=issue.get("line", 0),
            column=issue.get("column", 0),
            message=issue.get("message", ""),
            rule=issue.get("rule"),
            level="warning" if issue.get("severity") == "warning" else "error",
        )
        for issue in issues
    ]
    return {"results": results, "fixed": fixed}

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re

from geckoprocesstypes import process_types


def _get_default_logger():
    from mozlog import get_default_logger

    log = get_default_logger(component="mozleak")

    if not log:
        import logging

        log = logging.getLogger(__name__)
    return log


def process_single_leak_file(
    leakLogFileName,
    processType,
    leakThreshold,
    ignoreMissingLeaks,
    log=None,
    stackFixer=None,
    scope=None,
    allowed=None,
):
    """Process a single leak log."""

    #     |              |Per-Inst  Leaked|     Total  Rem|
    #   0 |TOTAL         |      17     192| 419115886    2|
    # 833 |nsTimerImpl   |      60     120|     24726    2|
    # 930 |Foo<Bar, Bar> |      32       8|       100    1|
    lineRe = re.compile(
        r"^\s*\d+ \|"
        r"(?P<name>[^|]+)\|"
        r"\s*(?P<size>-?\d+)\s+(?P<bytesLeaked>-?\d+)\s*\|"
        r"\s*(?P<totalInstances>-?\d+)\s+(?P<numLeaked>-?\d+)"
    )
    # The class name can contain spaces. We remove trailing whitespace later.

    log = log or _get_default_logger()

    if allowed is None:
        allowed = {}

    processString = "%s process:" % processType
    crashedOnPurpose = False
    totalBytesLeaked = None
    leakedObjectAnalysis = []
    leakedObjectNames = []
    recordLeakedObjects = False
    header = []
    log.info("leakcheck | Processing leak log file %s" % leakLogFileName)

    with open(leakLogFileName) as leaks:
        for line in leaks:
            if line.find("purposefully crash") > -1:
                crashedOnPurpose = True
            matches = lineRe.match(line)
            if not matches:
                # eg: the leak table header row
                strippedLine = line.rstrip()
                logLine = stackFixer(strippedLine) if stackFixer else strippedLine
                if recordLeakedObjects:
                    log.info(logLine)
                else:
                    header.append(logLine)
                continue
            name = matches.group("name").rstrip()
            size = int(matches.group("size"))
            bytesLeaked = int(matches.group("bytesLeaked"))
            totalInstances = int(matches.group("totalInstances"))
            numLeaked = int(matches.group("numLeaked"))
            # Output the raw line from the leak log table if it is for an object
            # row that has been leaked.
            if numLeaked != 0:
                # If this is the TOTAL line, first output the header lines.
                if name == "TOTAL":
                    for logLine in header:
                        log.info(logLine)
                log.info(line.rstrip())
            # If this is the TOTAL line, we're done with the header lines,
            # whether or not it leaked.
            if name == "TOTAL":
                header = []
            # Analyse the leak log, but output later or it will interrupt the
            # leak table
            if name == "TOTAL":
                # Multiple default processes can end up writing their bloat views into a single
                # log, particularly on B2G. Eventually, these should be split into multiple
                # logs (bug 1068869), but for now, we report the largest leak.
                if totalBytesLeaked is not None:
                    log.warning(
                        "leakcheck | %s "
                        "multiple BloatView byte totals found" % processString
                    )
                else:
                    totalBytesLeaked = 0
                if bytesLeaked > totalBytesLeaked:
                    totalBytesLeaked = bytesLeaked
                    # Throw out the information we had about the previous bloat
                    # view.
                    leakedObjectNames = []
                    leakedObjectAnalysis = []
                    recordLeakedObjects = True
                else:
                    recordLeakedObjects = False
            if (size < 0 or bytesLeaked < 0 or numLeaked < 0) and leakThreshold >= 0:
                log.error(
                    "TEST-UNEXPECTED-FAIL | leakcheck | %s negative leaks caught!"
                    % processString
                )
                continue
            if name != "TOTAL" and numLeaked != 0 and recordLeakedObjects:
                leakedObjectNames.append(name)
                leakedObjectAnalysis.append((
                    numLeaked,
                    name,
                    size,
                    bytesLeaked,
                    totalInstances,
                ))

    for numLeaked, name, size, bytesLeaked, totalInstances in leakedObjectAnalysis:
        leak_allowed = False
        if name in allowed:
            limit = leak_allowed[name]
            leak_allowed = limit is None or numLeaked <= limit

        log.mozleak_object(
            processType,
            numLeaked,
            name,
            scope=scope,
            allowed=leak_allowed,
            bytes_per_inst=size,
            bytes_leaked=bytesLeaked,
            total_instances=totalInstances,
        )

    log.mozleak_total(
        processType,
        totalBytesLeaked,
        leakThreshold,
        leakedObjectNames,
        scope=scope,
        induced_crash=crashedOnPurpose,
        ignore_missing=ignoreMissingLeaks,
    )


def process_leak_log(
    leak_log_file,
    leak_thresholds=None,
    ignore_missing_leaks=None,
    log=None,
    stack_fixer=None,
    scope=None,
    allowed=None,
):
    """Process the leak log, including separate leak logs created
    by child processes.

    Use this function if you want an additional PASS/FAIL summary.
    It must be used with the |XPCOM_MEM_BLOAT_LOG| environment variable.

    The base of leak_log_file for a non-default process needs to end with
      _proctype_pid12345.log
    "proctype" is a string denoting the type of the process, which should
    be the result of calling XRE_GeckoProcessTypeToString(). 12345 is
    a series of digits that is the pid for the process. The .log is
    optional.

    All other file names are treated as being for default processes.

    leak_thresholds should be a dict mapping process types to leak thresholds,
    in bytes. If a process type is not present in the dict the threshold
    will be 0. If the threshold is a negative number we additionally ignore
    the case where there's negative leaks.

    allowed - A dictionary mapping process types to dictionaries containing
    the number of objects of that type which are allowed to leak.

    scope - An identifier for the set of tests run during the browser session
            (e.g. a directory name)

    ignore_missing_leaks should be a list of process types. If a process
    creates a leak log without a TOTAL, then we report an error if it isn't
    in the list ignore_missing_leaks.

    Returns a list of files that were processed. The caller is responsible for
    cleaning these up.
    """
    log = log or _get_default_logger()

    processed_files = []

    leakLogFile = leak_log_file
    if not os.path.exists(leakLogFile):
        log.warning("leakcheck | refcount logging is off, so leaks can't be detected!")
        return processed_files

    log.info(
        "leakcheck | Processing log file %s%s"
        % (leakLogFile, (" for scope %s" % scope) if scope is not None else "")
    )

    leakThresholds = leak_thresholds or {}
    ignoreMissingLeaks = ignore_missing_leaks or []

    # This list is based on XRE_GeckoProcessTypeToString. ipdlunittest processes likely
    # are not going to produce leak logs we will ever see.

    knownProcessTypes = [
        p.string_name for p in process_types if p.string_name != "ipdlunittest"
    ]

    for processType in knownProcessTypes:
        log.info(
            "TEST-INFO | leakcheck | %s process: leak threshold set at %d bytes"
            % (processType, leakThresholds.get(processType, 0))
        )

    for processType in leakThresholds:
        if processType not in knownProcessTypes:
            log.error(
                "TEST-UNEXPECTED-FAIL | leakcheck | "
                "Unknown process type %s in leakThresholds" % processType
            )

    (leakLogFileDir, leakFileBase) = os.path.split(leakLogFile)
    if leakFileBase[-4:] == ".log":
        leakFileBase = leakFileBase[:-4]
        fileNameRegExp = re.compile(r"_([a-z]*)_pid\d*.log$")
    else:
        fileNameRegExp = re.compile(r"_([a-z]*)_pid\d*$")

    for fileName in os.listdir(leakLogFileDir):
        if fileName.find(leakFileBase) != -1:
            thisFile = os.path.join(leakLogFileDir, fileName)
            m = fileNameRegExp.search(fileName)
            if m:
                processType = m.group(1)
            else:
                processType = "default"
            if processType not in knownProcessTypes:
                log.error(
                    "TEST-UNEXPECTED-FAIL | leakcheck | "
                    "Leak log with unknown process type %s" % processType
                )
            leakThreshold = leakThresholds.get(processType, 0)
            process_single_leak_file(
                thisFile,
                processType,
                leakThreshold,
                processType in ignoreMissingLeaks,
                log=log,
                stackFixer=stack_fixer,
                scope=scope,
                allowed=allowed,
            )
            processed_files.append(thisFile)
    return processed_files

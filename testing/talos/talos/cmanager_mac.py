# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""CounterManager for Mac OSX"""

import subprocess
import sys

from talos.cmanager_base import CounterManager


def GetProcessData(pid):
    """Runs a ps on the process identified by pid and returns the output line
    as a list (pid, vsz, rss)
    """
    command = ["ps -o pid,vsize,rss -p" + str(pid)]
    try:
        handle = subprocess.Popen(
            command, stdout=subprocess.PIPE, universal_newlines=True, shell=True
        )
        handle.wait()
        data = handle.stdout.readlines()
    except Exception:
        print(f"Unexpected error executing '{command}': {sys.exc_info()}")
        raise

    # First line is header output should look like:
    # PID      VSZ    RSS
    # 3210    75964    920
    line = data[1]
    line = line.split()
    if line[0] == str(pid):
        return line


def GetPrivateBytes(pid):
    """Calculate the amount of private, writeable memory allocated to a
    process.
    """
    psData = GetProcessData(pid)
    return int(psData[1]) * 1024  # convert to bytes


def GetResidentSize(pid):
    """Retrieve the current resident memory for a given process"""
    psData = GetProcessData(pid)
    return int(psData[2]) * 1024  # convert to bytes


class MacCounterManager(CounterManager):
    """This class manages the monitoring of a process with any number of
    counters.

    A counter can be any function that takes an argument of one pid and
    returns a piece of data about that process.
    Some examples are: CalcCPUTime, GetResidentSize, and GetPrivateBytes
    """

    counterDict = {"Private Bytes": GetPrivateBytes, "RSS": GetResidentSize}

    def __init__(self, process_name, process, counters):
        """Args:
        counters: A list of counters to monitor. Any counters whose name
        does not match a key in 'counterDict' will be ignored.
        """

        CounterManager.__init__(self)

        # the last process is the useful one
        self.pid = process.pid

        self._loadCounters()
        self.registerCounters(counters)

    def getCounterValue(self, counterName):
        """Returns the last value of the counter 'counterName'"""
        if counterName not in self.registeredCounters:
            print(
                f"Warning: attempting to collect counter {counterName} and it is not"
                " registered"
            )
            return

        try:
            return self.registeredCounters[counterName][0](self.pid)
        except Exception as e:
            print(
                f"Error in collecting counter: {counterName}, pid: {self.pid}, exception: {e}"
            )

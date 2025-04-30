# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import pathlib
import re
import sys
from datetime import datetime

MEM_MATCHER = re.compile("([\\d,]*)K:\\s([\\S]*)\\s\\(")


def make_differential_metrics(
    differential_name, base_measures, mem_measures, cpu_measures
):
    metrics = []

    # Setup memory differentials
    metrics.extend(
        [
            {
                "name": f"{mem_type}-{category}-{differential_name}",
                "unit": "Kb",
                "values": [
                    round(mem_usage - base_measures["mem"][mem_type][category], 2)
                ],
            }
            for mem_type, mem_info in mem_measures.items()
            for category, mem_usage in mem_info.items()
        ]
    )
    metrics.extend(
        [
            {
                "name": f"{mem_type}-total-{differential_name}",
                "unit": "Kb",
                "values": [
                    round(
                        sum(mem_info.values())
                        - sum(base_measures["mem"][mem_type].values()),
                        2,
                    )
                ],
            }
            for mem_type, mem_info in mem_measures.items()
        ]
    )

    # Setup cpuTime differentials
    metrics.extend(
        [
            {
                "name": f"cpuTime-{category}-{differential_name}",
                "unit": "ms",
                "values": [cpu_time - base_measures["cpu"][category]],
            }
            for category, cpu_time in cpu_measures.items()
        ]
    )
    metrics.append(
        {
            "name": f"cpuTime-total-{differential_name}",
            "unit": "ms",
            "values": [
                round(
                    sum(cpu_measures.values()) - sum(base_measures["cpu"].values()), 2
                )
            ],
        }
    )

    return metrics


def get_chrome_process_category(process, binary):
    if "privileged_process" in process:
        return "gpu"
    elif "sandboxed_process" in process:
        return "tab"
    elif "zygote" in process:
        return "zygote"
    return "main"


def get_fenix_process_category(process, binary):
    # In the future, we'll also need to catch media/utility procs
    if "tab" in process:
        return "tab"
    elif f"{binary}" in process:
        return "main"
    elif "zygote" in process:
        return "zygote"
    return process


def get_category_for_process(process, binary):
    if "fenix" in binary:
        return get_fenix_process_category(process, binary)
    elif "chrome" in binary:
        return get_chrome_process_category(process, binary)
    raise Exception("Unknown binary for determining process category")


def parse_memory_usage(mem_file, binary):
    mem_info = []
    with mem_file.open() as f:
        mem_info = f.readlines()

    curr_mem = ""
    final_mems = {"rss": {}, "pss": {}}
    for line in mem_info:
        if not line.strip():
            # Anytime a blank line is hit, the current
            # memory type being tracked changes
            curr_mem = ""
            continue
        if not curr_mem:
            if "Total RSS by process:" in line:
                curr_mem = "rss"
            elif "Total PSS by process:" in line:
                curr_mem = "pss"
            continue

        match = MEM_MATCHER.search(line.strip())
        if not match:
            continue

        mem_usage, binary_name = match.groups()
        if binary not in binary_name:
            continue

        name_split = binary_name.split(f"{binary}:")
        if len(name_split) == 1:
            name = name_split[0]
        else:
            name = name_split[-1]

        final_mems[curr_mem][name] = round(float(mem_usage.replace(",", "")), 2)

    measurements = {
        "rss": {"tab": 0, "gpu": 0, "main": 0, "crashhelper": 0},
        "pss": {"tab": 0, "gpu": 0, "main": 0, "crashhelper": 0},
    }
    for mem_type, mem_info in final_mems.items():
        for name, mem_usage in mem_info.items():
            final_name = get_category_for_process(name, binary)
            if (
                final_name == "zygote"
                and measurements[mem_type].get("zygote", None) is None
            ):
                # Only add this process if it exists (it doesn't exist on fenix)
                measurements[mem_type]["zygote"] = 0
            measurements[mem_type][final_name] += mem_usage

    return measurements


def parse_cpu_usage(cpu_file, binary):
    cpu_info = []
    with cpu_file.open() as f:
        cpu_info = f.readlines()

    # Gather all the final cpu times for the processes
    final_times = {}
    for line in cpu_info:
        if not line.strip():
            continue
        vals = line.split()

        name = vals[0]
        if f"{binary}" not in name:
            # Sometimes the PID catches the wrong process
            continue

        name_split = name.split(f"{binary}:")
        if len(name_split) == 1:
            name = name_split[0]
        else:
            name = name_split[-1]

        final_times[name] = vals[-2]

    # Convert the final times to milliseconds
    cpu_times = {"tab": 0, "gpu": 0, "main": 0, "crashhelper": 0}
    for name, time in final_times.items():
        # adb shell ps -o time+= gives us MIN:SEC.HUNDREDTHS.
        # That's why we divide dt.microseconds by 1000 for measuring in milliseconds.
        dt = datetime.strptime(time, "%M:%S.%f")
        milliseconds = (((dt.minute * 60) + dt.second) * 1000) + (dt.microsecond / 1000)

        final_name = get_category_for_process(name, binary)
        if final_name == "zygote" and cpu_times.get("zygote", None) is None:
            # Only add this process if it exists (it doesn't exist on fenix)
            cpu_times["zygote"] = 0

        cpu_times[final_name] += milliseconds

    return cpu_times


def main():
    args = sys.argv[1:]
    binary = args[1]
    testing_dir = pathlib.Path(args[0])
    run_background = True if args[2] == "True" else False

    cpu_info_files = sorted(testing_dir.glob("cpu_info*"))
    mem_info_files = sorted(testing_dir.glob("mem_info*"))

    perf_metrics = []
    base_measures = {}
    for i, measurement_time in enumerate(("start", "10%", "50%", "end")):
        cpu_measures = parse_cpu_usage(cpu_info_files[i], binary)
        mem_measures = parse_memory_usage(mem_info_files[i], binary)

        if not base_measures:
            base_measures["cpu"] = cpu_measures
            base_measures["mem"] = mem_measures

        perf_metrics.extend(
            [
                {
                    "name": f"cpuTime-{category}-{measurement_time}",
                    "unit": "ms",
                    "values": [cpu_time],
                }
                for category, cpu_time in cpu_measures.items()
            ]
        )
        perf_metrics.append(
            {
                "name": f"cpuTime-total-{measurement_time}",
                "unit": "ms",
                "values": [round(sum(cpu_measures.values()), 2)],
            }
        )

        perf_metrics.extend(
            [
                {
                    "name": f"{mem_type}-{category}-{measurement_time}",
                    "unit": "Kb",
                    "values": [round(mem_usage, 2)],
                }
                for mem_type, mem_info in mem_measures.items()
                for category, mem_usage in mem_info.items()
            ]
        )
        perf_metrics.extend(
            [
                {
                    "name": f"{mem_type}-total-{measurement_time}",
                    "unit": "Kb",
                    "values": [round(sum(mem_info.values()), 2)],
                }
                for mem_type, mem_info in mem_measures.items()
            ]
        )

        if base_measures and run_background:
            if measurement_time == "10%":
                perf_metrics.extend(
                    make_differential_metrics(
                        "backgrounding-diff", base_measures, mem_measures, cpu_measures
                    )
                )
            elif measurement_time == "end":
                perf_metrics.extend(
                    make_differential_metrics(
                        "background-diff", base_measures, mem_measures, cpu_measures
                    )
                )

    print(
        "perfMetrics: "
        + str(perf_metrics).replace("{", "{{").replace("}", "}}").replace("'", '"')
    )


if __name__ == "__main__":
    main()

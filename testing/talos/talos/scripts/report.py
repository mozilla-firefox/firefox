# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import csv
import os
import sys
from calendar import day_name
from datetime import datetime

import compare
import numpy

sys.path.insert(1, os.path.join(sys.path[0], ".."))


def get_branch(platform):
    if platform.startswith("OSX"):
        return compare.branch_map["Inbound"]["pgo"]["id"]
    return compare.branch_map["Inbound"]["nonpgo"]["id"]


def get_all_test_tuples():
    ret = []
    for test in compare.test_map:
        for platform in compare.platform_map:
            ret.extend(get_tuple(test, platform))
    return ret


def get_tuple(test, platform):
    return [
        (
            compare.test_map[test]["id"],
            get_branch(platform),
            compare.platform_map[platform],
            test,
            platform,
        )
    ]


def generate_report(tuple_list, filepath, mode="variance"):
    avg = []

    for test in tuple_list:
        testid, branchid, platformid = test[:3]
        data_dict = compare.getGraphData(testid, branchid, platformid)
        week_avgs = []

        if data_dict:
            data = data_dict["test_runs"]
            data.sort(key=lambda x: x[3])
            data = data[int(0.1 * len(data)) : int(0.9 * len(data) + 1)]
            time_dict = {}
            days = {}

            for point in data:
                time = datetime.fromtimestamp(point[2]).strftime("%Y-%m-%d")
                time_dict[time] = time_dict.get(time, []) + [point[3]]

            for time in time_dict:
                runs = len(time_dict[time])
                weekday = datetime.strptime(time, "%Y-%m-%d").strftime("%A")
                variance = numpy.var(time_dict[time])
                if mode == "variance":
                    days[weekday] = days.get(weekday, []) + [variance]
                elif mode == "count":
                    days[weekday] = days.get(weekday, []) + [runs]

            line = ["-".join(test[3:])]
            for day in day_name:
                if mode == "variance":
                    # removing top and bottom 10% to reduce outlier influence
                    # pylint --py3k W1619
                    tenth = len(days[day]) / 10
                    average = numpy.average(sorted(days[day])[tenth : tenth * 9 + 1])
                elif mode == "count":
                    average = numpy.average(days[day])
                line.append(f"{average:.3f}")
                week_avgs.append(average)

            outliers = is_normal(week_avgs)
            for j in range(7):
                if j in outliers:
                    line[j + 1] = "**" + str(line[j + 1]) + "**"

            avg.append(line)

    with open(filepath, "wb") as report:
        avgs_header = csv.writer(report, quoting=csv.QUOTE_ALL)
        avgs_header.writerow(["test-platform"] + list(day_name))
        for line in avg:
            out = csv.writer(report, quoting=csv.QUOTE_ALL)
            out.writerow(line)


def is_normal(y):
    # This is a crude initial attempt at detecting normal distributions
    # TODO: Improve this
    limit = 1.5
    clean_week = []
    outliers = []
    # find a baseline for the week
    if (min(y[0:4]) * limit) <= max(y[0:4]):
        for i in range(1, 5):
            if y[i] > (y[i - 1] * limit) or y[i] > (y[i + 1] * limit):
                outliers.append(i)
                continue
            clean_week.append(y[i])
    else:
        clean_week = y

    # look at weekends now
    # pylint --py3k W1619
    avg = sum(clean_week) / len(clean_week)
    for i in range(5, 7):
        # look for something outside of the 20% window
        if (y[i] * 1.2) < avg or y[i] > (avg * 1.2):
            outliers.append(i)
    return outliers


def main():
    parser = argparse.ArgumentParser(description="Generate weekdays reports")
    parser.add_argument("--test", help="show only the test named TEST")
    parser.add_argument("--platform", help="show only the platform named PLATFORM")
    parser.add_argument("--mode", help="select mode", default="variance")
    args = parser.parse_args()
    tuple_list = get_all_test_tuples()
    f = "report"
    if args.platform:
        tuple_list = [x for x in tuple_list if x[4] == args.platform]
        f += f"-{args.platform}"

    if args.test:
        tuple_list = [x for x in tuple_list if x[3] == args.test]
        f += f"-{args.test}"

    f += f"-{args.mode}"
    generate_report(tuple_list, filepath=f + ".csv", mode=args.mode)


if __name__ == "__main__":
    main()

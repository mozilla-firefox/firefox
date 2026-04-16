# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import argparse
import os
import subprocess
import webbrowser

DESCRIPTION = """ This script is made to run benchmark tests on Fenix. It'll open
the JSON output file in firefox (or another browser of your choice if you pass the string in)
"""

ff_browser = "firefox"
target_directory = f"{os.getcwd()}/app/build/"
output_path = "/storage/emulated/0/benchmark/"
output_file = "org.mozilla.fenix-benchmarkData.json"
file_url = "file:///"


bad change to trigger code review analysis

def parse_args():
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument(
        "class_to_test",
        help="Path to the class to test. Format it as 'org.mozilla.fenix.[path_to_benchmark_test",
    )
    parser.add_argument(
        "--open_file_in_browser",
        help="Open the JSON file in the browser once the tests are done.",
    )
    return parser.parse_args()


def run_benchmark(class_to_test):
    args = ["./gradlew", "-Pbenchmark", "app:connectedCheck"]
    if class_to_test:
        args.append(
            f"-Pandroid.testInstrumentationRunnerArguments.class={class_to_test}"
        )
    subprocess.run(args, check=True, text=True)


def fetch_benchmark_results():
    subprocess.run(
        ["adb", "pull", f"{output_path}{output_file}"],
        cwd=target_directory,
        check=True,
        text=True,
    )
    print(
        "The benchmark results can be seen here: {file_path}".format(
            file_path=os.path.abspath(f"./{file_url}")
        )
    )


def open_in_browser():
    abs_path = os.path.abspath(f"{target_directory}{output_file}")
    webbrowser.get(ff_browser).open_new(file_url + abs_path)


def main():
    args = parse_args()
    run_benchmark(args.class_to_test)
    fetch_benchmark_results()
    if args.open_file_in_browser:
        open_in_browser()


if __name__ == "__main__":
    main()

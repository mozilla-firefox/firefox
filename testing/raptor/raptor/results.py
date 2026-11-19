# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# class to process, format, and report raptor test results
# received from the raptor control server
import builtins
import json
import os
import pathlib
import shutil
import tarfile
from abc import ABCMeta, abstractmethod
from collections.abc import Iterable
from pathlib import Path

from logger.logger import RaptorLogger
from output import BrowsertimeOutput, RaptorOutput
from utils import flatten

LOG = RaptorLogger(component="perftest-results-handler")
KNOWN_TEST_MODIFIERS = [
    "condprof-settled",
    "fission",
    "live",
    "gecko-profile",
    "cold",
    "webrender",
    "bytecode-cached",
    "simpleperf",
    "etw-profile",
]
NON_FIREFOX_OPTS = ("webrender", "bytecode-cached", "fission")
NON_FIREFOX_BROWSERS = ("chrome", "custom-car", "safari", "safari-tp")
NON_FIREFOX_BROWSERS_MOBILE = ("chrome-m", "cstm-car-m")


class PerftestResultsHandler(metaclass=ABCMeta):
    """Abstract base class to handle perftest results"""

    def __init__(
        self,
        gecko_profile=False,
        live_sites=False,
        app=None,
        conditioned_profile=None,
        cold=False,
        chimera=False,
        fission=True,
        perfstats=False,
        test_bytecode_cache=False,
        simpleperf=False,
        etw_profile=False,
        samply_profile=False,
        perf_profile=False,
        extra_summary_methods=[],
        **kwargs,
    ):
        self.gecko_profile = gecko_profile
        self.live_sites = live_sites
        self.app = app
        self.conditioned_profile = conditioned_profile
        self.results = []
        self.page_timeout_list = []
        self.images = []
        self.supporting_data = None
        self.fission_enabled = fission
        self.browser_version = None
        self.browser_name = None
        self.cold = cold
        self.chimera = chimera
        self.perfstats = perfstats
        self.test_bytecode_cache = test_bytecode_cache
        self.simpleperf = simpleperf
        self.etw_profile = etw_profile
        self.samply_profile = samply_profile
        self.perf_profile = perf_profile
        self.existing_results = None
        self.extra_summary_methods = extra_summary_methods

    @abstractmethod
    def add(self, new_result_json):
        raise NotImplementedError()

    def result_dir(self):
        return None

    def build_extra_options(self, modifiers=None):
        extra_options = []

        # If fields is not None, then we default to
        # checking all known fields. Otherwise, we only check
        # the fields that were given to us.
        if modifiers is None:
            if self.conditioned_profile:
                # Don't add an option/tag for the base test case
                if self.conditioned_profile != "settled":
                    extra_options.append(
                        f"condprof-{self.conditioned_profile.replace('artifact:', '')}"
                    )
            if self.fission_enabled:
                extra_options.append("fission")
            if self.live_sites:
                extra_options.append("live")
            if self.gecko_profile:
                extra_options.append("gecko-profile")
            if self.cold:
                extra_options.append("cold")
            if self.test_bytecode_cache:
                extra_options.append("bytecode-cached")
            if self.simpleperf:
                extra_options.append("simpleperf")
            if self.etw_profile:
                extra_options.append("etw-profile")
            if self.samply_profile:
                extra_options.append("samply-profile")
            if self.perf_profile:
                extra_options.append("perf-profile")
            extra_options.append("webrender")
        else:
            for modifier, name in modifiers:
                if not modifier:
                    continue
                if name in KNOWN_TEST_MODIFIERS:
                    extra_options.append(name)
                else:
                    raise Exception(
                        f"Unknown test modifier {name} was provided as an extra option"
                    )

        # Bug 1770225: Make this more dynamic, this will fail us again in the future
        self._clean_up_browser_options(extra_options=extra_options)

        return extra_options

    def _clean_up_browser_options(self, extra_options):
        """Remove certain firefox specific options from different browsers"""
        if self.app.lower() in NON_FIREFOX_BROWSERS + NON_FIREFOX_BROWSERS_MOBILE:
            for opts in NON_FIREFOX_OPTS:
                if opts in extra_options:
                    extra_options.remove(opts)

    def add_browser_meta(self, browser_name, browser_version):
        # sets the browser metadata for the perfherder data
        self.browser_name = browser_name
        self.browser_version = browser_version

    def add_image(self, screenshot, test_name, page_cycle):
        # add to results
        LOG.info("received screenshot")
        self.images.append({
            "screenshot": screenshot,
            "test_name": test_name,
            "page_cycle": page_cycle,
        })

    def add_page_timeout(self, test_name, page_url, page_cycle, pending_metrics):
        timeout_details = {
            "test_name": test_name,
            "url": page_url,
            "page_cycle": page_cycle,
        }
        if pending_metrics:
            pending_metrics = [key for key, value in pending_metrics.items() if value]
            timeout_details["pending_metrics"] = ", ".join(pending_metrics)

        self.page_timeout_list.append(timeout_details)

    def add_supporting_data(self, supporting_data):
        """Supporting data is additional data gathered outside of the regular
        Raptor test run (i.e. power data). Will arrive in a dict in the format of:

        supporting_data = {'type': 'data-type',
                           'test': 'raptor-test-ran-when-data-was-gathered',
                           'unit': 'unit that the values are in',
                           'values': {
                               'name': value,
                               'nameN': valueN}}

        More specifically, power data will look like this:

        supporting_data = {'type': 'power',
                           'test': 'raptor-speedometer-geckoview',
                           'unit': 'mAh',
                           'values': {
                               'cpu': cpu,
                               'wifi': wifi,
                               'screen': screen,
                               'proportional': proportional}}
        """
        LOG.info(
            "RaptorResultsHandler.add_supporting_data received "
            f"{supporting_data['type']} data"
        )
        if self.supporting_data is None:
            self.supporting_data = []
        self.supporting_data.append(supporting_data)

    def use_existing_results(self, directory):
        self.existing_results = directory

    def _get_expected_perfherder(self, output):
        # if results exists, determine if any test is of type 'scenario'
        is_scenario = False
        if output.summarized_results or output.summarized_supporting_data:
            data = output.summarized_supporting_data
            if not data:
                data = [output.summarized_results]
            for next_data_set in data:
                data_type = next_data_set["suites"][0]["type"]
                if data_type == "scenario":
                    is_scenario = True
                    break
        if is_scenario:
            # skip perfherder check when a scenario test-type is run
            return None
        return 1

    def _validate_treeherder_data(self, output, output_perfdata):
        # late import is required, because install is done in create_virtualenv
        import jsonschema

        expected_perfherder = self._get_expected_perfherder(output)
        if expected_perfherder is None:
            LOG.info(
                "Skipping PERFHERDER_DATA check "
                "because no perfherder data output is expected"
            )
            return True
        elif output_perfdata != expected_perfherder:
            LOG.critical(
                f"PERFHERDER_DATA was seen {output_perfdata} times, "
                f"expected {expected_perfherder}."
            )
            return False

        schema_path = os.environ["PERFHERDER_SCHEMA_PATH"]
        LOG.info(f"Validating PERFHERDER_DATA against {schema_path}")
        try:
            with builtins.open(schema_path, encoding="utf-8") as f:
                schema = json.load(f)
            if output.summarized_results:
                data = output.summarized_results
            else:
                data = output.summarized_supporting_data[0]
            jsonschema.validate(data, schema)
        except Exception as e:
            LOG.exception("Error while validating PERFHERDER_DATA")
            LOG.info(str(e))
            return False
        return True

    @abstractmethod
    def summarize_and_output(self, test_config, tests, test_names):
        raise NotImplementedError()


class RaptorResultsHandler(PerftestResultsHandler):
    """Process Raptor results"""

    def add(self, new_result_json):
        LOG.info("received results in RaptorResultsHandler.add")
        new_result_json.setdefault("extra_options", []).extend(
            self.build_extra_options([
                (
                    self.conditioned_profile,
                    f"condprof-{self.conditioned_profile}",
                ),
                (self.fission_enabled, "fission"),
            ])
        )
        if self.live_sites:
            new_result_json.setdefault("tags", []).append("live")
            new_result_json["extra_options"].append("live")
        self.results.append(new_result_json)

    def summarize_and_output(self, test_config, tests, test_names):
        # summarize the result data, write to file and output PERFHERDER_DATA
        LOG.info("summarizing raptor test results")
        output = RaptorOutput(
            self.results,
            self.supporting_data,
            test_config.get("subtest_alert_on", []),
            self.app,
        )
        output.set_browser_meta(self.browser_name, self.browser_version)
        output.summarize(test_names)
        # that has each browser cycle separate; need to check if there were multiple browser
        # cycles, and if so need to combine results from all cycles into one overall result
        output.combine_browser_cycles()
        output.summarize_screenshots(self.images)

        # only dump out supporting data (i.e. power) if actual Raptor test completed
        out_sup_perfdata = 0
        sup_success = True
        if self.supporting_data is not None and len(self.results) != 0:
            output.summarize_supporting_data()
            sup_success, out_sup_perfdata = output.output_supporting_data(test_names)

        success, out_perfdata = output.output(test_names)

        validate_success = True
        if not self.gecko_profile:
            validate_success = self._validate_treeherder_data(
                output, out_sup_perfdata + out_perfdata
            )

        return (
            sup_success and success and validate_success and not self.page_timeout_list
        )


class BrowsertimeResultsHandler(PerftestResultsHandler):
    """Process Browsertime results"""

    def __init__(self, config, root_results_dir=None):
        super().__init__(**config)
        self._root_results_dir = root_results_dir
        self.browsertime_visualmetrics = False
        self.failed_vismets = []
        if not os.path.exists(self._root_results_dir):
            os.mkdir(self._root_results_dir)

        self.browsertime_results_folders = {
            "browsertime_results": "browsertime-results",
            "profiler": "browsertime-profiler",
            "videos_annotated": "browsertime-videos-annotated",
            "videos_original": "browsertime-videos-original",
            "results_extra": "browsertime-results-extra",
        }
        self.browsertime_temp_folder = "temp-browsertime-results"

    def _determine_target_subfolders(
        self, result_file, is_profiling_job, has_video_files
    ):
        """
        Determine the target subfolders for a given result file.
        Args:
            result_file (Path): The path object for the result file.
            is_profiling_job (bool): Indicator if the job is a profiling job.
            has_video_files (bool): Indicator if there are any video recordings present in the results folder,
            so we can determine if we should group json and mp4 files in the same archive

        Returns:
            list: A list of target subfolders for the result file.
        """
        # Move results files if running a profling job
        if is_profiling_job:
            return [self.browsertime_results_folders["profiler"]]

        # Move extra-profiler-run data to separate folder
        if "profiling" in result_file.parts:
            return [self.browsertime_results_folders["profiler"]]

        # Check for video files
        if result_file.suffix == ".mp4":
            return (
                [self.browsertime_results_folders["videos_original"]]
                if "original" in result_file.name
                else [self.browsertime_results_folders["videos_annotated"]]
            )

        # JSON files get mapped to multiple folders
        if result_file.suffix == ".json":
            target_subfolders = [
                self.browsertime_results_folders["browsertime_results"]
            ]
            if has_video_files:
                target_subfolders.extend([
                    self.browsertime_results_folders["videos_annotated"],
                    self.browsertime_results_folders["videos_original"],
                ])
            return target_subfolders

        # Default folder for unexpected files
        return [self.browsertime_results_folders["results_extra"]]

    def _cleanup_archive_folders(self, folders_list):
        """
        Removes the specified results folders after archiving is complete.
        """
        for folder in folders_list:
            LOG.info(f"Removing {folder}")
            shutil.rmtree(folder)

    def _archive_results_folder(self, folder_to_archive):
        """
        Archives the specified folder into a tar.gz file.
        """
        full_archive_path = folder_to_archive.with_suffix(".tgz")

        # Delete previous archives when running locally
        if full_archive_path.is_file():
            full_archive_path.unlink(missing_ok=True)

        LOG.info(f"Creating archive: {full_archive_path}")
        with tarfile.open(str(full_archive_path), "w:gz") as tar:
            tar.add(folder_to_archive, arcname=folder_to_archive.name)

    def _setup_artifact_folders(self, subfolders, root_path):
        """
        Sets up and returns artifact folders based on the provided subfolders and root path.
        Args:
            subfolders (List[str]): A list of subfolder names to be set up under the root path.
            root_path (Path): The root directory under which the artifact subfolders will be created.
        Returns:
            Dict[str, Path]: A dictionary mapping each subfolder name to its corresponding Path object.
        """
        artifact_folders = {}
        for subfolder in subfolders:
            destination_folder = root_path / subfolder
            destination_folder.mkdir(exist_ok=True, parents=True)
            artifact_folders[subfolder] = destination_folder
        return artifact_folders

    def _copy_artifact_to_folders(self, artifact_file, dest_folders_list):
        """
        Copies the specified artifact file to multiple destination folders.
        """
        for dest_folder in dest_folders_list:
            dest_folder.mkdir(exist_ok=True, parents=True)
            shutil.copy(artifact_file, dest_folder)

    def _split_artifact_files(self, artifact_path, artifact_folders, is_profiling_job):
        """
        Distributes artifact files from the given artifact path into the appropriate target subfolders.
        Args:
            artifact_path (Union[str, Path]): The root path where artifact files are located.
            artifact_folders (Dict[str, Union[str, Path]]): A dictionary mapping target subfolder names
            is_profiling_job (bool): A flag indicating whether the current job is a profiling job.
        """
        # Need to check if every glob result is a file,
        # since we can end up with empty folders that contain periods
        # that get mistaken for files
        # (such as 'InteractiveRunner.html' for speedometer tests)
        all_artifact_files = [f for f in artifact_path.glob("**/*.*") if f.is_file()]

        # Check for any mp4 files in the list of unique file extensions
        has_video_files = ".mp4" in set(f.suffix for f in all_artifact_files)

        for artifact in all_artifact_files:
            artifact_relpath = artifact.relative_to(artifact_path).parent
            target_subfolders = self._determine_target_subfolders(
                artifact, is_profiling_job, has_video_files
            )
            destination_folders = [
                artifact_folders[ts] / artifact_relpath for ts in target_subfolders
            ]
            self._copy_artifact_to_folders(artifact, destination_folders)

    def _archive_artifact_folders(self, artifact_folders_list):
        """
        Archives all non-empty artifact folders from the given list.
        Args:
            artifact_folders_list (List[Union[str, Path]]):
                A list of artifact folder paths to be checked and archived.
        """
        for folder in artifact_folders_list:
            if self._folder_contains_files(folder):
                self._archive_results_folder(folder)

    def _folder_contains_files(self, artifact_folder):
        return os.listdir(artifact_folder)

    def archive_raptor_artifacts(self, is_profiling_job):
        """
        Description: Archives browsertime artifacts based on specific criteria.
        Args:
        - is_profiling_job (bool): Indicator if a job is running with the gecko_profile flag set to True
        """
        test_artifact_path = Path(self.result_dir())
        destination_root_path = test_artifact_path.parent
        temp_artifact_path = destination_root_path / self.browsertime_temp_folder

        # Cleanup any temp folders remaining from previous runs, when running locally
        if temp_artifact_path.is_dir():
            self._cleanup_archive_folders([temp_artifact_path])

        # Rename browsertime-results to a temp folder so we don't have name conflicts
        test_artifact_path.rename(temp_artifact_path)

        artifact_folders = self._setup_artifact_folders(
            self.browsertime_results_folders.values(), destination_root_path
        )

        self._split_artifact_files(
            temp_artifact_path, artifact_folders, is_profiling_job
        )
        artifact_folders_list = list(artifact_folders.values())
        self._archive_artifact_folders(artifact_folders_list)
        artifact_folders_list.append(temp_artifact_path)
        self._cleanup_archive_folders(artifact_folders_list)

    def result_dir(self):
        return self._root_results_dir

    def result_dir_for_test(self, test):
        if self.existing_results is None:
            results_root = self._root_results_dir
        else:
            results_root = self.existing_results
        return os.path.join(results_root, test["name"])

    def remove_result_dir_for_test(self, test):
        test_result_dir = self.result_dir_for_test(test)
        if os.path.exists(test_result_dir):
            shutil.rmtree(test_result_dir)
        return test_result_dir

    def result_dir_for_test_profiling(self, test):
        profiling_dir = os.path.join(self.result_dir_for_test(test), "profiling")
        if not os.path.exists(profiling_dir):
            os.mkdir(profiling_dir)
        return profiling_dir

    def add(self, new_result_json):
        # not using control server with bt
        pass

    def build_tags(self, test={}):
        """Use to build the tags option for our perfherder data.

        This should only contain items that will only be shown within
        the tags section and excluded from the extra options.
        """
        tags = []
        LOG.info(test)
        if test.get("interactive", False):
            tags.append("interactive")
        return tags

    def parse_browsertime_json(
        self,
        raw_btresults,
        page_cycles,
        cold,
        browser_cycles,
        measure,
        page_count,
        test_name,
        accept_zero_vismet,
        load_existing,
        test_summary,
        subtest_name_filters,
        handle_custom_data,
        support_class,
        submetric_summary_method,
        **kwargs,
    ):
        """
        Receive a json blob that contains the results direct from the browsertime tool. Parse
        out the values that we wish to use and add those to our result object. That object will
        then be further processed in the BrowsertimeOutput class.

        The values that we care about in the browsertime.json are structured as follows.
        The 'browserScripts' section has one entry for each page-load / browsertime cycle!

        [
          {
            "info": {
              "browsertime": {
                "version": "4.9.2-android"
              },
              "url": "https://www.theguardian.co.uk",
            },
            "browserScripts": [
              {
                "browser": {
                  "userAgent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.13; rv:70.0)
                                Gecko/20100101 Firefox/70.0",
                  "windowSize": "1366x694"
                },
                "timings": {
                  "firstPaint": 830,
                  "loadEventEnd": 4450,
                  "timeToContentfulPaint": 932,
                  "timeToDomContentFlushed": 864,
                  }
                }
              },
              {
                <repeated for every page-load cycle>
              },
            ],
            "statistics": {
              "timings": {
                "firstPaint": {
                  "median": 668,
                  "mean": 680,
                  "mdev": 9.6851,
                  "stddev": 48,
                  "min": 616,
                  "p10": 642,
                  "p90": 719,
                  "p99": 830,
                  "max": 830
                },
                "loadEventEnd": {
                  "median": 3476,
                  "mean": 3642,
                  "mdev": 111.7028,
                  "stddev": 559,
                  "min": 3220,
                  "p10": 3241,
                  "p90": 4450,
                  "p99": 5818,
                  "max": 5818
                },
                "timeToContentfulPaint": {
                  "median": 758,
                  "mean": 769,
                  "mdev": 10.0941,
                  "stddev": 50,
                  "min": 712,
                  "p10": 728,
                  "p90": 810,
                  "p99": 932,
                  "max": 932
                },
                "timeToDomContentFlushed": {
                  "median": 670,
                  "mean": 684,
                  "mdev": 11.6768,
                  "stddev": 58,
                  "min": 614,
                  "p10": 632,
                  "p90": 738,
                  "p99": 864,
                  "max": 864
                },
              }
            },
            "geckoPerfStats": [
              {
                "Compositing": 71,
                "MajorGC": 144
              },
              {
                "Compositing": 13,
                "MajorGC": 126
              }
            ]
          }
        ]

        For benchmark tests the browserScripts tag will look like:
        "browserScripts":[
         {
            "browser":{ },
            "pageinfo":{ },
            "timings":{ },
            "custom":{
               "benchmarks":{
                  "speedometer":[
                     {
                        "Angular2-TypeScript-TodoMVC":[
                           326.41999999999825,
                           238.7799999999952,
                           211.88000000000463,
                           186.77999999999884,
                           191.47999999999593
                        ],
                        etc...
                    }
                  ]
               }
           }
        """
        LOG.info("parsing results from browsertime json")
        # bt to raptor names
        conversion = (
            ("fnbpaint", "firstPaint"),
            ("fcp", ["paintTiming", "first-contentful-paint"]),
            ("dcf", "timeToDomContentFlushed"),
            ("loadtime", "loadEventEnd"),
            ("largestContentfulPaint", ["largestContentfulPaint", "renderTime"]),
        )

        def _get_raptor_val(mdict, mname, retval=False):
            # gets the measurement requested, returns the value
            # if one was found, or retval if it couldn't be found
            #
            # mname: either a path to follow (as a list) to get to
            #        a requested field value, or a string to check
            #        if mdict contains it. i.e.
            #        'first-contentful-paint'/'fcp' is found by searching
            #        in mdict['paintTiming'].
            # mdict: a dictionary to look through to find the mname
            #        value.

            if type(mname) is not list:
                if mname in mdict:
                    return mdict[mname]
                return retval
            target = mname[-1]
            tmpdict = mdict
            for name in mname[:-1]:
                tmpdict = tmpdict.get(name, {})
            if target in tmpdict:
                return tmpdict[target]

            return retval

        results = []

        # Do some preliminary results validation. When running cold page-load, the results will
        # be all in one entry already, as browsertime groups all cold page-load iterations in
        # one results entry with all replicates within. When running warm page-load, there will
        # be one results entry for every warm page-load iteration; with one single replicate
        # inside each.
        if cold or handle_custom_data:
            if len(raw_btresults) == 0:
                raise MissingResultsError(
                    "Missing results for all cold browser cycles."
                )
        elif load_existing:
            pass  # Use whatever is there.
        elif len(raw_btresults) != int(page_cycles):
            raise MissingResultsError("Missing results for at least 1 warm page-cycle.")

        # now parse out the values
        page_counter = 0
        last_result = False
        for i, raw_result in enumerate(raw_btresults):
            if not raw_result["browserScripts"]:
                raise MissingResultsError("Browsertime cycle produced no measurements.")

            if raw_result["browserScripts"][0].get("timings") is None:
                raise MissingResultsError("Browsertime cycle is missing all timings")

            if i == (len(raw_btresults) - 1):
                # Used to tell custom support scripts when the last result
                # is being parsed. This lets them finalize any overall measurements.
                last_result = True
            # Desktop chrome doesn't have `browser` scripts data available for now
            bt_browser = raw_result["browserScripts"][0].get("browser", None)
            bt_ver = raw_result["info"]["browsertime"]["version"]

            # when doing actions, we append a .X for each additional pageload in a scenario
            extra = ""
            if len(page_count) > 0:
                extra = f".{page_count[page_counter % len(page_count)]}"
            url_parts = raw_result["info"]["url"].split("/")
            page_counter += 1

            bt_url = f"{'/'.join(url_parts[:-1])}{extra}/{url_parts[-1]},"
            bt_result = {
                "bt_ver": bt_ver,
                "browser": bt_browser,
                "url": (bt_url,),
                "name": f"{test_name}{extra}",
                "measurements": {},
                "statistics": {},
            }
            if submetric_summary_method is not None:
                bt_result["submetric_summary_method"] = submetric_summary_method

            def _extract_cpu_vals():
                # Bug 1806402 - Handle chrome cpu data properly
                cpu_vals = raw_result.get("cpu", None)
                if (
                    cpu_vals
                    and self.app
                    not in NON_FIREFOX_BROWSERS + NON_FIREFOX_BROWSERS_MOBILE
                ):
                    bt_result["measurements"].setdefault("cpuTime", []).extend(cpu_vals)

            def _extract_android_power_vals():
                power_vals = raw_result.get("android").get("power", {})
                if power_vals:
                    bt_result["measurements"].setdefault("powerUsage", []).extend([
                        round(vals["powerUsage"] * (1 * 10**-6), 2)
                        for vals in power_vals
                    ])

            if support_class:
                bt_result["custom_data"] = True
                support_class.save_data(raw_result, bt_result)
                support_class.handle_result(
                    bt_result,
                    raw_result,
                    conversion=conversion,
                    last_result=last_result,
                )
            elif any(raw_result["extras"]):
                # Each entry here is a separate cold pageload iteration
                for custom_types in raw_result["extras"]:
                    for custom_type in custom_types:
                        data = custom_types[custom_type]
                        if handle_custom_data:
                            if test_summary in ("flatten",):
                                data = flatten(data, ())
                            for k, v in data.items():

                                def _ignore_metric(*args):
                                    if any(
                                        type(arg) not in (int, float) for arg in args
                                    ):
                                        return True
                                    return False

                                # Ignore any non-numerical results
                                if _ignore_metric(v) and _ignore_metric(*v):
                                    continue

                                # Clean up the name if requested
                                filtered_k = k
                                for name_filter in subtest_name_filters.split(","):
                                    filtered_k = filtered_k.replace(name_filter, "")

                                if isinstance(v, Iterable):
                                    bt_result["measurements"].setdefault(
                                        filtered_k, []
                                    ).extend(v)
                                else:
                                    bt_result["measurements"].setdefault(
                                        filtered_k, []
                                    ).append(v)
                            bt_result["custom_data"] = True
                        else:
                            for k, v in data.items():
                                bt_result["measurements"].setdefault(k, []).append(v)
                if self.perfstats:
                    for cycle in raw_result["geckoPerfStats"]:
                        for metric in cycle:
                            bt_result["measurements"].setdefault(
                                "perfstat-" + metric, []
                            ).append(cycle[metric])
                if kwargs.get("gather_cpuTime", None):
                    _extract_cpu_vals()
            else:
                raise Exception(
                    "Test should be using browsertime_pageload.py as the "
                    "support_class instead of the results.py/output.py parsing."
                )

            _extract_android_power_vals()

            results.append(bt_result)

        return results

    def _extract_vmetrics(
        self,
        test_name,
        browsertime_json,
        json_name="browsertime.json",
        tags=[],
        extra_options=[],
        accept_zero_vismet=False,
    ):
        # The visual metrics task expects posix paths.
        def _normalized_join(*args):
            path = os.path.join(*args)
            return path.replace(os.path.sep, "/")

        name = browsertime_json.split(os.path.sep)[-2]
        reldir = _normalized_join("browsertime-results", name)

        return {
            "browsertime_json_path": _normalized_join(reldir, json_name),
            "test_name": test_name,
            "tags": tags,
            "extra_options": extra_options,
            "accept_zero_vismet": accept_zero_vismet,
        }

    def _label_video_folder(self, result_data, base_dir, kind="warm"):
        for filetype in result_data["files"]:
            for idx, data in enumerate(result_data["files"][filetype]):
                parts = list(pathlib.Path(data).parts)
                lable_idx = parts.index("data")
                if "query-" in parts[lable_idx - 1]:
                    lable_idx -= 1

                src_dir = pathlib.Path(base_dir).joinpath(*parts[: lable_idx + 1])
                parts.insert(lable_idx, kind)
                dst_dir = pathlib.Path(base_dir).joinpath(*parts[: lable_idx + 1])

                if src_dir.exists() and not dst_dir.exists():
                    pathlib.Path(dst_dir).mkdir(parents=True, exist_ok=True)
                    shutil.move(str(src_dir), str(dst_dir))

                result_data["files"][filetype][idx] = str(pathlib.Path(*parts[:]))

    def summarize_and_output(self, test_config, tests, test_names):
        """
        Retrieve, process, and output the browsertime test results. Currently supports page-load
        type tests only.

        The Raptor framework either ran a single page-load test (one URL) - or - an entire suite
        of page-load tests (multiple test URLs). Regardless, every test URL measured will
        have its own 'browsertime.json' results file, located in a sub-folder names after the
        Raptor test name, i.e.:

        browsertime-results/
            raptor-tp6-amazon-firefox
                browsertime.json
            raptor-tp6-facebook-firefox
                browsertime.json
            raptor-tp6-google-firefox
                browsertime.json
            raptor-tp6-youtube-firefox
                browsertime.json

        For each test URL that was measured, find the resulting 'browsertime.json' file, and pull
        out the values that we care about.
        """
        # summarize the browsertime result data, write to file and output PERFHERDER_DATA
        LOG.info("retrieving browsertime test results")

        # video_jobs is populated with video files produced by browsertime, we
        # will send to the visual metrics task
        video_jobs = []
        run_local = test_config.get("run_local", False)

        for test in tests:
            test_name = test["name"]
            accept_zero_vismet = test.get("accept_zero_vismet", False)

            bt_res_json = os.path.join(
                self.result_dir_for_test(test), "browsertime.json"
            )
            if os.path.exists(bt_res_json):
                LOG.info(f"found browsertime results at {bt_res_json}")
            else:
                LOG.critical(f"unable to find browsertime results at {bt_res_json}")
                return False

            try:
                with builtins.open(bt_res_json, encoding="utf8") as f:
                    raw_btresults = json.load(f)
            except Exception as e:
                LOG.error(f"Exception reading {bt_res_json}")
                # XXX this should be replaced by a traceback call
                LOG.error(f"Exception: {type(e).__name__} {e}")
                raise

            # Split the chimera videos here for local testing
            def split_browsertime_results(result_json_path, raw_btresults):
                # First result is cold, second is warm
                cold_data = raw_btresults[0]
                warm_data = raw_btresults[1]

                dirpath = os.path.dirname(os.path.abspath(result_json_path))
                _cold_path = os.path.join(dirpath, "cold-browsertime.json")
                _warm_path = os.path.join(dirpath, "warm-browsertime.json")

                self._label_video_folder(cold_data, dirpath, "cold")
                self._label_video_folder(warm_data, dirpath, "warm")

                with builtins.open(_cold_path, "w") as f:
                    json.dump([cold_data], f)
                with builtins.open(_warm_path, "w") as f:
                    json.dump([warm_data], f)

                raw_btresults[0] = cold_data
                raw_btresults[1] = warm_data

                return _cold_path, _warm_path

            cold_path = None
            warm_path = None
            if self.chimera:
                cold_path, warm_path = split_browsertime_results(
                    bt_res_json, raw_btresults
                )

                # Overwrite the contents of the browsertime.json file
                # to update it with the new file paths
                try:
                    with builtins.open(bt_res_json, "w", encoding="utf8") as f:
                        json.dump(raw_btresults, f)
                except Exception as e:
                    LOG.error(f"Exception reading {bt_res_json}")
                    # XXX this should be replaced by a traceback call
                    LOG.error(f"Exception: {type(e).__name__} {e}")
                    raise

                # If extra profiler run is enabled, split its browsertime.json
                # file to cold and warm json files as well.
                bt_profiling_res_json = os.path.join(
                    self.result_dir_for_test_profiling(test), "browsertime.json"
                )
                has_extra_profiler_run = test_config.get(
                    "extra_profiler_run", False
                ) and os.path.exists(bt_profiling_res_json)
                if has_extra_profiler_run:
                    try:
                        with builtins.open(bt_profiling_res_json, encoding="utf8") as f:
                            raw_profiling_btresults = json.load(f)
                            split_browsertime_results(
                                bt_profiling_res_json, raw_profiling_btresults
                            )
                    except Exception as e:
                        LOG.info(
                            f"Exception reading and writing {bt_profiling_res_json}"
                        )
                        LOG.info(f"Exception: {type(e).__name__} {e}")

            if not run_local:
                extra_options = self.build_extra_options()
                tags = self.build_tags(test=test)

                if self.chimera:
                    if cold_path is None or warm_path is None:
                        raise Exception("Cold and warm paths were not created")

                    video_jobs.append(
                        self._extract_vmetrics(
                            test_name,
                            cold_path,
                            json_name="cold-browsertime.json",
                            tags=list(tags),
                            extra_options=list(extra_options),
                            accept_zero_vismet=accept_zero_vismet,
                        )
                    )

                    extra_options.remove("cold")
                    extra_options.append("warm")
                    video_jobs.append(
                        self._extract_vmetrics(
                            test_name,
                            warm_path,
                            json_name="warm-browsertime.json",
                            tags=list(tags),
                            extra_options=list(extra_options),
                            accept_zero_vismet=accept_zero_vismet,
                        )
                    )
                else:
                    video_jobs.append(
                        self._extract_vmetrics(
                            test_name,
                            bt_res_json,
                            tags=list(tags),
                            extra_options=list(extra_options),
                            accept_zero_vismet=accept_zero_vismet,
                        )
                    )

            for new_result in self.parse_browsertime_json(
                raw_btresults,
                test["page_cycles"],
                test["cold"],
                test["browser_cycles"],
                test.get("measure"),
                test_config.get("page_count", []),
                test["name"],
                accept_zero_vismet,
                self.existing_results is not None,
                test.get("test_summary", "pageload"),
                test.get("subtest_name_filters", ""),
                test.get("custom_data", False) == "true",
                test.get("support_class", None),
                test.get("submetric_summary_method", None),
                gather_cpuTime=test.get("gather_cpuTime", None),
            ):

                def _new_standard_result(new_result, subtest_unit="ms"):
                    # add additional info not from the browsertime json
                    for field in (
                        "name",
                        "unit",
                        "lower_is_better",
                        "alert_threshold",
                        "cold",
                    ):
                        if field in new_result:
                            continue
                        new_result[field] = test[field]

                    new_result["min_back_window"] = test.get("min_back_window", None)
                    new_result["max_back_window"] = test.get("max_back_window", None)
                    new_result["fore_window"] = test.get("fore_window", None)
                    new_result["alert_change_type"] = test.get(
                        "alert_change_type", None
                    )

                    # All Browsertime measurements are elapsed times in milliseconds.
                    new_result["subtest_lower_is_better"] = test.get(
                        "subtest_lower_is_better", True
                    )
                    new_result["subtest_unit"] = subtest_unit

                    new_result["extra_options"] = self.build_extra_options()
                    new_result.setdefault("tags", []).extend(self.build_tags(test=test))

                    # Split the chimera
                    if self.chimera and "run=2" in new_result["url"][0]:
                        new_result["extra_options"].remove("cold")
                        new_result["extra_options"].append("warm")

                    # Add the support class to the result
                    new_result["support_class"] = test.get("support_class", None)
                    new_result["simpleperf"] = test.get("simpleperf", False)
                    new_result["etw-profile"] = test.get("etw_profile", False)
                    new_result["samply-profile"] = test.get("samply_profile", False)
                    new_result["perf-profile"] = test.get("perf_profile", False)

                    return new_result

                def _new_powertest_result(new_result):
                    new_result["type"] = "power"
                    new_result["unit"] = "mAh"
                    new_result["lower_is_better"] = True

                    new_result = _new_standard_result(new_result, subtest_unit="mAh")
                    new_result["extra_options"].append("power")

                    LOG.info(f"parsed new power result: {new_result}")
                    return new_result

                def _new_custom_result(new_result):
                    new_result["type"] = "pageload"
                    new_result = _new_standard_result(
                        new_result, subtest_unit=test.get("subtest_unit", "ms")
                    )

                    LOG.info(f"parsed new custom result: {new_result}")
                    return new_result

                def _new_pageload_result(new_result):
                    new_result["type"] = "pageload"
                    new_result = _new_standard_result(new_result)

                    LOG.info(f"parsed new pageload result: {new_result}")
                    return new_result

                def _new_benchmark_result(new_result):
                    new_result["type"] = "benchmark"

                    new_result = _new_standard_result(
                        new_result, subtest_unit=test.get("subtest_unit", "ms")
                    )
                    new_result["gather_cpuTime"] = test.get("gather_cpuTime", None)
                    LOG.info(f"parsed new benchmark result: {new_result}")
                    return new_result

                def _is_supporting_data(res):
                    if res.get("power_data", False):
                        return True
                    return False

                if new_result.get("power_data", False):
                    self.results.append(_new_powertest_result(new_result))
                elif test["type"] == "pageload":
                    if test.get("custom_data", False) == "true":
                        self.results.append(_new_custom_result(new_result))
                    else:
                        self.results.append(_new_pageload_result(new_result))
                elif test["type"] == "benchmark":
                    if test.get("simpleperf"):
                        for i, item in enumerate(self.results):
                            if item["name"] == test["name"] and not _is_supporting_data(
                                item
                            ):
                                for key, value in new_result["measurements"].items():
                                    self.results[i]["measurements"].setdefault(
                                        key, []
                                    ).extend(value)
                                break
                        else:
                            self.results.append(_new_benchmark_result(new_result))
                    else:
                        for i, item in enumerate(self.results):
                            if item["name"] == test["name"] and not _is_supporting_data(
                                item
                            ):
                                # add page cycle custom measurements to the existing results
                                for measurement in new_result["measurements"].items():
                                    self.results[i]["measurements"][
                                        measurement[0]
                                    ].extend(measurement[1])
                                break
                        else:
                            self.results.append(_new_benchmark_result(new_result))

        # now have all results gathered from all browsertime test URLs; format them for output
        output = BrowsertimeOutput(
            self.results,
            self.supporting_data,
            test_config.get("subtest_alert_on", []),
            self.app,
            self.extra_summary_methods,
        )
        output.set_browser_meta(self.browser_name, self.browser_version)
        output.summarize(test_names)
        success, out_perfdata = output.output(test_names)

        if len(self.failed_vismets) > 0:
            LOG.critical(
                "TEST-UNEXPECTED-FAIL | Some visual metrics have an erroneous value of 0."
            )
            LOG.info(f"Visual metric tests failed: {self.failed_vismets}")

        validate_success = True
        if not self.gecko_profile:
            validate_success = self._validate_treeherder_data(output, out_perfdata)

        if len(video_jobs) > 0:
            # The video list and application metadata (browser name and
            # optionally version) that will be used in the visual metrics task.
            jobs_json = {
                "jobs": video_jobs,
                "application": {"name": self.browser_name},
                "extra_options": output.summarized_results["suites"][0]["extraOptions"],
            }

            if self.browser_version is not None:
                jobs_json["application"]["version"] = self.browser_version

            jobs_file = os.path.join(self.result_dir(), "jobs.json")
            LOG.info(
                f"Writing video jobs and application data {jobs_json} into {jobs_file}"
            )
            with builtins.open(jobs_file, "w") as f:
                f.write(json.dumps(jobs_json))

        support_class_success = True
        for test in tests:
            if test.get("support_class"):
                support_class_success = test.get("support_class").report_test_success()

        return (
            (success and validate_success)
            and len(self.failed_vismets) == 0
            and support_class_success
        )


class MissingResultsError(Exception):
    pass

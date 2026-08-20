# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Columnar data structures and the ProfileProcessor aggregator for BHR.

Ported from python_mozetl/mozetl/bhr_collection/bhr_collection.py as part of
the bhr_collection migration. The semantics are unchanged — these classes
take symbolicated, heuristic-trimmed hang samples and build the columnar
output schema (stackTable / funcTable / stringArray / sampleTable /
annotationsTable / dates / libs) the frontend consumes.

The "(root)" sentinel at index 0 of stackTable / pruneStackCache is
intentional: the frontend's stack walker terminates when prefix == 0, so
keeping that slot reserved is load-bearing.
"""

import random
import re


def to_struct_of_arrays(a):
    if len(a) == 0:
        raise Exception("Need at least one item in array for this to work.")

    result = {k: [e[k] for e in a] for k in a[0].keys()}
    result["length"] = len(a)
    return result


class UniqueKeyedTable:
    def __init__(self, get_default_from_key, key_names=()):
        self.get_default_from_key = get_default_from_key
        self.key_to_index_map = {}
        self.key_names = key_names
        self.items = []

    def key_to_index(self, key):
        if key in self.key_to_index_map:
            return self.key_to_index_map[key]

        index = len(self.items)
        self.items.append(self.get_default_from_key(key))
        self.key_to_index_map[key] = index
        return index

    def key_to_item(self, key):
        return self.items[self.key_to_index(key)]

    def index_to_item(self, index):
        return self.items[index]

    def get_items(self):
        return self.items

    def inner_struct_of_arrays(self, items):
        if len(items) == 0:
            raise Exception("Need at least one item in array for this to work.")

        result = {}
        num_keys = len(self.key_names)
        for i in range(0, num_keys):
            result[self.key_names[i]] = [x[i] for x in items]

        result["length"] = len(items)
        return result

    def struct_of_arrays(self):
        return self.inner_struct_of_arrays(self.items)

    def sorted_struct_of_arrays(self, key):
        return self.inner_struct_of_arrays(sorted(self.items, key=key))


class GrowToFitList(list):
    def __setitem__(self, index, value):
        if index >= len(self):
            to_grow = index + 1 - len(self)
            self.extend([None] * to_grow)
        list.__setitem__(self, index, value)

    def __getitem__(self, index):
        if index >= len(self):
            return None
        return list.__getitem__(self, index)


def get_default_lib(name):
    return {
        "name": re.sub(r"\.pdb$", "", name),
        "offset": 0,
        "path": "",
        "debugName": name,
        "debugPath": name,
        "arch": "",
    }


def get_default_thread(name, minimal_sample_table):
    strings_table = UniqueKeyedTable(lambda str: str)
    libs = UniqueKeyedTable(get_default_lib)
    func_table = UniqueKeyedTable(
        lambda key: (
            strings_table.key_to_index(key[0]),
            None if key[1] is None else libs.key_to_index(key[1]),
        ),
        ("name", "lib"),
    )
    stack_table = UniqueKeyedTable(
        lambda key: (key[2], func_table.key_to_index((key[0], key[1]))),
        ("prefix", "func"),
    )
    annotations_table = UniqueKeyedTable(
        lambda key: (
            key[0],
            strings_table.key_to_index(key[1]),
            strings_table.key_to_index(key[2]),
        ),
        ("prefix", "name", "value"),
    )
    if minimal_sample_table:
        sample_table = UniqueKeyedTable(
            lambda key: (
                key[0],
                strings_table.key_to_index(key[1]),
                key[2],
                strings_table.key_to_index(key[3]),
            ),
            ("stack", "platform"),
        )
    else:
        sample_table = UniqueKeyedTable(
            lambda key: (
                key[0],
                strings_table.key_to_index(key[1]),
                key[2],
                strings_table.key_to_index(key[3]),
            ),
            ("stack", "runnable", "annotations", "platform"),
        )

    stack_table.key_to_index(("(root)", None, None))

    prune_stack_cache = UniqueKeyedTable(lambda key: [0.0])
    prune_stack_cache.key_to_index(("(root)", None, None))

    return {
        "name": name,
        "libs": libs,
        "funcTable": func_table,
        "stackTable": stack_table,
        # Per stack-node inline accounting, indexed by stack index. A node is
        # (func, lib, prefix), so this is per calling context: the same
        # function can be inlined into one caller and not another. Weights are
        # hang counts, so the emitted ratio is weighted by how much each build
        # actually hung rather than by how many distinct stacks we saw.
        "inlineDepthByStack": {},
        "inlineWeightByStack": {},
        "totalWeightByStack": {},
        "annotationsTable": annotations_table,
        "pruneStackCache": prune_stack_cache,
        "sampleTable": sample_table,
        "stringArray": strings_table,
        "processType": "tab" if name == "Gecko_Child" else "default",
        "dates": UniqueKeyedTable(
            lambda date: ({
                "date": date,
                "sampleHangMs": GrowToFitList(),
                "sampleHangCount": GrowToFitList(),
            }),
            ("date", "sampleHangMs", "sampleHangCount"),
        ),
    }


def reconstruct_stack(string_array, func_table, stack_table, lib_table, stack_index):
    inline_depth = stack_table.get("inlineDepth")
    result = []
    while stack_index != 0:
        func_index = stack_table["func"][stack_index]
        prefix = stack_table["prefix"][stack_index]
        func_name = string_array[func_table["name"][func_index]]
        lib_name = lib_table[func_table["lib"][func_index]]["debugName"]
        depth = 0 if inline_depth is None else inline_depth[stack_index]
        result.append((func_name, lib_name, depth))
        stack_index = prefix
    return result[::-1]


def merge_number_dicts(a, b):
    keys = set(a.keys()).union(set(b.keys()))
    return {k: a.get(k, 0.0) + b.get(k, 0.0) for k in keys}


class ProfileProcessor:
    def __init__(self, config):
        self.config = config

        def default_thread_closure(name):
            return get_default_thread(name, config["use_minimal_sample_table"])

        self.thread_table = UniqueKeyedTable(default_thread_closure)
        self.usage_hours_by_date = {}

    def debug_dump(self, dump_str):
        if self.config["print_debug_info"]:
            print(dump_str)

    def ingest_processed_profile(self, profile):
        for existing_thread in self.thread_table.get_items():
            prune_stack_cache = UniqueKeyedTable(lambda key: [0.0])
            prune_stack_cache.key_to_index(("(root)", None, None))
            existing_thread["pruneStackCache"] = prune_stack_cache

        sample_size = self.config["post_sample_size"]
        threads = profile["threads"]
        for other in threads:
            other_samples = other["sampleTable"]
            other_dates = other["dates"]

            for date in other_dates:
                build_date = date["date"]
                for i in range(0, len(date["sampleHangCount"])):
                    stack_index = other_samples["stack"][i]
                    stack = reconstruct_stack(
                        other["stringArray"],
                        other["funcTable"],
                        other["stackTable"],
                        other["libs"],
                        stack_index,
                    )
                    self.pre_ingest_row((
                        stack,
                        other["stringArray"][other_samples["runnable"][i]],
                        other["name"],
                        build_date,
                        other_samples["annotations"][i],
                        other["stringArray"][other_samples["platform"][i]],
                        date["sampleHangMs"][i],
                        date["sampleHangCount"][i],
                    ))

            for date in other_dates:
                build_date = date["date"]
                for i in range(0, len(date["sampleHangCount"])):
                    stack_index = other_samples["stack"][i]
                    stack = reconstruct_stack(
                        other["stringArray"],
                        other["funcTable"],
                        other["stackTable"],
                        other["libs"],
                        stack_index,
                    )
                    if sample_size == 1.0 or random.random() <= sample_size:
                        self.ingest_row((
                            stack,
                            other["stringArray"][other_samples["runnable"][i]],
                            other["name"],
                            build_date,
                            other_samples["annotations"][i],
                            other["stringArray"][other_samples["platform"][i]],
                            date["sampleHangMs"][i],
                            date["sampleHangCount"][i],
                        ))

        self.usage_hours_by_date = merge_number_dicts(
            self.usage_hours_by_date, profile.get("usageHoursByDate", {})
        )

    def pre_ingest_row(self, row):
        (
            stack,
            runnable_name,
            thread_name,
            build_date,
            annotations,
            platform,
            hang_ms,
            hang_count,
        ) = row

        thread = self.thread_table.key_to_item(thread_name)
        prune_stack_cache = thread["pruneStackCache"]
        root_stack = prune_stack_cache.key_to_item(("(root)", None, None))
        root_stack[0] += hang_ms

        last_stack = 0
        for func_name, lib_name, _inline_depth in stack:
            last_stack = prune_stack_cache.key_to_index((
                func_name,
                lib_name,
                last_stack,
            ))
            cache_item = prune_stack_cache.index_to_item(last_stack)
            cache_item[0] += hang_ms

    def ingest_row(self, row):
        (
            stack,
            runnable_name,
            thread_name,
            build_date,
            annotations,
            platform,
            hang_ms,
            hang_count,
        ) = row

        thread = self.thread_table.key_to_item(thread_name)
        stack_table = thread["stackTable"]
        annotations_table = thread["annotationsTable"]
        sample_table = thread["sampleTable"]
        dates = thread["dates"]
        prune_stack_cache = thread["pruneStackCache"]

        last_annotation = None
        for name, value in annotations:
            last_annotation = annotations_table.key_to_index((
                last_annotation,
                name,
                value,
            ))

        inline_depth_by_stack = thread["inlineDepthByStack"]
        inline_weight_by_stack = thread["inlineWeightByStack"]
        total_weight_by_stack = thread["totalWeightByStack"]

        last_stack = 0
        last_cache_item_index = 0
        for func_name, lib_name, inline_depth in stack:
            cache_item_index = prune_stack_cache.key_to_index((
                func_name,
                lib_name,
                last_cache_item_index,
            ))
            cache_item = prune_stack_cache.index_to_item(cache_item_index)
            parent_cache_item = prune_stack_cache.index_to_item(last_cache_item_index)
            if (
                cache_item[0] / parent_cache_item[0]
                > self.config["stack_acceptance_threshold"]
            ):
                last_stack = stack_table.key_to_index((func_name, lib_name, last_stack))
                last_cache_item_index = cache_item_index
                total_weight_by_stack[last_stack] = (
                    total_weight_by_stack.get(last_stack, 0.0) + hang_count
                )
                if inline_depth:
                    inline_weight_by_stack[last_stack] = (
                        inline_weight_by_stack.get(last_stack, 0.0) + hang_count
                    )
                    inline_depth_by_stack[last_stack] = max(
                        inline_depth_by_stack.get(last_stack, 0), inline_depth
                    )
            else:
                # Below the acceptance threshold — lump under "(other)" beneath
                # the parent rather than continuing to expand the tree.
                last_stack = stack_table.key_to_index(("(other)", lib_name, last_stack))
                break

        if self.config["use_minimal_sample_table"] and thread_name == "Gecko_Child":
            return

        sample_index = sample_table.key_to_index((
            last_stack,
            runnable_name,
            last_annotation,
            platform,
        ))

        date = dates.key_to_item(build_date)
        if date["sampleHangCount"][sample_index] is None:
            date["sampleHangCount"][sample_index] = 0.0
            date["sampleHangMs"][sample_index] = 0.0

        date["sampleHangCount"][sample_index] += hang_count
        date["sampleHangMs"][sample_index] += hang_ms

    def ingest(self, data, usage_hours_by_date):
        print(f"{len(data)} unfiltered samples in data")
        data = [
            x
            for x in data
            # x[6] is hang_ms
            if x[6] > 0.0
        ]
        print(f"{len(data)} filtered samples in data")

        print("Preprocessing stacks for prune cache...")
        for row in data:
            self.pre_ingest_row(row)

        print("Processing stacks...")
        for row in data:
            self.ingest_row(row)

        self.usage_hours_by_date = merge_number_dicts(
            self.usage_hours_by_date, usage_hours_by_date
        )

    def process_date(self, date):
        if self.config["use_minimal_sample_table"]:
            return {
                "date": date["date"],
                "sampleHangCount": date["sampleHangCount"],
            }
        return date

    def attach_inline_info(self, thread, stack_table):
        """Add per-stack-node inline columns, parallel to prefix/func.

        inlineDepth is the frame's position in its inlined chain: 0 for a real
        frame, 1+ for an inlined callee. inlineRatio is the share of hang count
        at that node where the frame was inlined. The two differ because
        signatures merge across builds, and a build that inlined a call and one
        that did not produce the same reconstructed stack (bug 2052961), so a
        node can be inlined some of the time. Ratio is rounded: unrounded
        floats defeat compression and roughly double the gzipped artifact.
        """
        depths = thread["inlineDepthByStack"]
        inline_weight = thread["inlineWeightByStack"]
        total_weight = thread["totalWeightByStack"]
        length = stack_table["length"]

        stack_table["inlineDepth"] = [depths.get(i, 0) for i in range(length)]
        stack_table["inlineRatio"] = [
            round(inline_weight[i] / total_weight[i], 2)
            if inline_weight.get(i) and total_weight.get(i)
            else 0
            for i in range(length)
        ]

    def process_thread(self, thread):
        string_array = thread["stringArray"]
        func_table = thread["funcTable"].struct_of_arrays()
        stack_table = thread["stackTable"].struct_of_arrays()
        self.attach_inline_info(thread, stack_table)
        annotations_table = thread["annotationsTable"].struct_of_arrays()
        sample_table = thread["sampleTable"].struct_of_arrays()

        return {
            "name": thread["name"],
            "processType": thread["processType"],
            "libs": thread["libs"].get_items(),
            "funcTable": func_table,
            "stackTable": stack_table,
            "annotationsTable": annotations_table,
            "sampleTable": sample_table,
            "stringArray": string_array.get_items(),
            "dates": [self.process_date(d) for d in thread["dates"].get_items()],
        }

    def process_into_split_profile(self):
        return {
            "main_payload": {
                "splitFiles": {
                    t["name"]: [k for k in t.keys() if k != "name"]
                    for t in self.thread_table.get_items()
                },
                "usageHoursByDate": self.usage_hours_by_date,
                "uuid": self.config["uuid"],
                "isSplit": True,
            },
            "file_data": [
                [
                    (t["name"] + "_" + k, v)
                    for k, v in self.process_thread(t).iteritems()
                    if k != "name"
                ]
                for t in self.thread_table.get_items()
            ],
        }

    def process_into_profile(self):
        print("Processing into final format...")
        if self.config["split_threads_in_out_file"]:
            return [
                {
                    "name": t["name"],
                    "threads": [self.process_thread(t)],
                    "usageHoursByDate": self.usage_hours_by_date,
                    "uuid": self.config["uuid"],
                }
                for t in self.thread_table.get_items()
            ]

        return {
            "threads": [self.process_thread(t) for t in self.thread_table.get_items()],
            "usageHoursByDate": self.usage_hours_by_date,
            "uuid": self.config["uuid"],
        }

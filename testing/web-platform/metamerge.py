# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import logging
import os
import sys
from collections import namedtuple

from wptrunner.wptmanifest.backends import base
from wptrunner.wptmanifest.node import KeyValueNode
from wptrunner.wptmanifest.serializer import serialize

here = os.path.dirname(__file__)
logger = logging.getLogger(__name__)


class Compiler(base.Compiler):
    def visit_KeyValueNode(self, node):
        key_name = node.data
        values = []
        for child in node.children:
            values.append(self.visit(child))

        self.output_node.set(key_name, values)

    def visit_ConditionalNode(self, node):
        assert len(node.children) == 2
        # For conditional nodes, just return the subtree
        return serialize(node.children[0]), self.visit(node.children[1])

    def visit_UnaryExpressionNode(self, node):
        raise NotImplementedError

    def visit_BinaryExpressionNode(self, node):
        raise NotImplementedError

    def visit_UnaryOperatorNode(self, node):
        raise NotImplementedError

    def visit_BinaryOperatorNode(self, node):
        raise NotImplementedError


class ExpectedManifest(base.ManifestItem):
    def __init__(self, node):
        """Object representing all the tests in a particular manifest"""
        base.ManifestItem.__init__(self, node)
        self.child_map = {}

    def append(self, child):
        """Add a test to the manifest"""
        base.ManifestItem.append(self, child)
        self.child_map[child.name] = child

    def insert(self, child):
        self.append(child)
        self.node.append(child.node)

    def delete(self, child):
        del self.child_map[child.name]
        child.node.remove()
        child.remove()


class TestManifestItem(ExpectedManifest):
    def set_expected(self, other_manifest):
        for item in self.node.children:
            if isinstance(item, KeyValueNode) and item.data == "expected":
                assert "expected" in self._data
                item.remove()
                del self._data["expected"]
                break

        for item in other_manifest.node.children:
            if isinstance(item, KeyValueNode) and item.data == "expected":
                assert "expected" in other_manifest._data
                item.remove()
                self.node.children.insert(0, item)
                self._data["expected"] = other_manifest._data.pop("expected")
                break


def data_cls_getter(output_node, visited_node):
    # visited_node is intentionally unused
    if output_node is None:
        return ExpectedManifest
    if isinstance(output_node, ExpectedManifest):
        return TestManifestItem
    raise ValueError


def compile(stream, data_cls_getter=None, **kwargs):
    return base.compile(Compiler, stream, data_cls_getter=data_cls_getter, **kwargs)


def get_manifest(manifest_path):
    """Get the ExpectedManifest for a particular manifest path"""
    try:
        with open(manifest_path, "rb") as f:
            try:
                return compile(f, data_cls_getter=data_cls_getter)
            except Exception:
                f.seek(0)
                sys.stderr.write("Error parsing:\n%s" % f.read().decode("utf8"))
                raise
    except OSError:
        return None


def indent(str_data, indent=2):
    rv = []
    for line in str_data.splitlines():
        rv.append("%s%s" % (" " * indent, line))
    return "\n".join(rv)


class Differences:
    def __init__(self):
        self.added = []
        self.deleted = []
        self.modified = []

    def __nonzero__(self):
        return bool(self.added or self.deleted or self.modified)

    def __str__(self):
        modified = []
        for item in self.modified:
            if isinstance(item, TestModified):
                modified.append(
                    "  %s\n    %s\n%s" % (item[0], item[1], indent(str(item[2]), 4))
                )
            else:
                assert isinstance(item, ExpectedModified)
                modified.append("  %s\n    %s %s" % item)
        return "Added:\n%s\nDeleted:\n%s\nModified:\n%s\n" % (
            "\n".join("  %s:\n    %s" % item for item in self.added),
            "\n".join("  %s" % item for item in self.deleted),
            "\n".join(modified),
        )


TestModified = namedtuple("TestModified", ["test", "test_manifest", "differences"])


ExpectedModified = namedtuple(
    "ExpectedModified", ["test", "ancestor_manifest", "new_manifest"]
)


def compare_test(test, ancestor_manifest, new_manifest):
    changes = Differences()

    compare_expected(changes, None, ancestor_manifest, new_manifest)

    for subtest, ancestor_subtest_manifest in ancestor_manifest.child_map.items():
        compare_expected(
            changes,
            subtest,
            ancestor_subtest_manifest,
            new_manifest.child_map.get(subtest),
        )

    for subtest, subtest_manifest in new_manifest.child_map.items():
        if subtest not in ancestor_manifest.child_map:
            changes.added.append((subtest, subtest_manifest))

    return changes


def compare_expected(changes, subtest, ancestor_manifest, new_manifest):
    if not (
        ancestor_manifest and ancestor_manifest.has_key("expected")  # noqa W601
    ) and (
        new_manifest and new_manifest.has_key("expected")  # noqa W601
    ):
        changes.modified.append(
            ExpectedModified(subtest, ancestor_manifest, new_manifest)
        )
    elif (
        ancestor_manifest
        and ancestor_manifest.has_key("expected")  # noqa W601
        and not (new_manifest and new_manifest.has_key("expected"))  # noqa W601
    ):
        changes.deleted.append(subtest)
    elif (
        ancestor_manifest
        and ancestor_manifest.has_key("expected")  # noqa W601
        and new_manifest
        and new_manifest.has_key("expected")  # noqa W601
    ):
        old_expected = ancestor_manifest.get("expected")
        new_expected = new_manifest.get("expected")
        if expected_values_changed(old_expected, new_expected):
            changes.modified.append(
                ExpectedModified(subtest, ancestor_manifest, new_manifest)
            )


def expected_values_changed(old_expected, new_expected):
    if len(old_expected) != len(new_expected):
        return True

    old_dict = {}
    new_dict = {}
    for dest, cond_lines in [(old_dict, old_expected), (new_dict, new_expected)]:
        for cond_line in cond_lines:
            if isinstance(cond_line, tuple):
                condition, value = cond_line
            else:
                condition = None
                value = cond_line
            dest[condition] = value

    return new_dict != old_dict


def record_changes(ancestor_manifest, new_manifest):
    changes = Differences()

    for test, test_manifest in new_manifest.child_map.items():
        if test not in ancestor_manifest.child_map:
            changes.added.append((test, test_manifest))
        else:
            ancestor_test_manifest = ancestor_manifest.child_map[test]
            test_differences = compare_test(test, ancestor_test_manifest, test_manifest)
            if test_differences:
                changes.modified.append(
                    TestModified(test, test_manifest, test_differences)
                )

    for test, test_manifest in ancestor_manifest.child_map.items():
        if test not in new_manifest.child_map:
            changes.deleted.append(test)

    return changes


def apply_changes(current_manifest, changes):
    for test, test_manifest in changes.added:
        if test in current_manifest.child_map:
            current_manifest.delete(current_manifest.child_map[test])
        current_manifest.insert(test_manifest)

    for test in changes.deleted:
        if test in current_manifest.child_map:
            current_manifest.delete(current_manifest.child_map[test])

    for item in changes.modified:
        if isinstance(item, TestModified):
            test, new_manifest, test_changes = item
            if test in current_manifest.child_map:
                apply_changes(current_manifest.child_map[test], test_changes)
            else:
                current_manifest.insert(new_manifest)
        else:
            assert isinstance(item, ExpectedModified)
            subtest, ancestor_manifest, new_manifest = item
            if not subtest:
                current_manifest.set_expected(new_manifest)
            elif subtest in current_manifest.child_map:
                current_manifest.child_map[subtest].set_expected(new_manifest)
            else:
                current_manifest.insert(new_manifest)


def get_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument("ancestor")
    parser.add_argument("current")
    parser.add_argument("new")
    parser.add_argument("dest", nargs="?")
    return parser


def get_parser_mergetool():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-overwrite", dest="overwrite", action="store_false")
    return parser


def make_changes(ancestor_manifest, current_manifest, new_manifest):
    changes = record_changes(ancestor_manifest, new_manifest)
    apply_changes(current_manifest, changes)

    return serialize(current_manifest.node)


def run(ancestor, current, new, dest):
    ancestor_manifest = get_manifest(ancestor)
    current_manifest = get_manifest(current)
    new_manifest = get_manifest(new)

    updated_current_str = make_changes(
        ancestor_manifest, current_manifest, new_manifest
    )

    if dest != "-":
        with open(dest, "wb") as f:
            f.write(updated_current_str.encode("utf8"))
    else:
        print(updated_current_str)

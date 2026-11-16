# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import bisect
import os
import re
from html.parser import HTMLParser

import mozpack.path as mozpath
import yaml
from fluent.syntax import ast, parse, visitor
from mozlint import result
from mozlint.pathutils import expand_exclusions


class TextElementHTMLParser(HTMLParser):
    """HTML Parser for TextElement.

    TextElements may contain embedded html tags, which can include
    quotes in attributes. We only want to check the actual text.
    """

    def __init__(self):
        super().__init__()
        self.extracted_text = []

    def handle_data(self, data):
        self.extracted_text.append(data)


class Linter(visitor.Visitor):
    """Fluent linter implementation.

    This subclasses the Fluent AST visitor. Methods are called corresponding
    to each type of node in the Fluent AST. It is possible to control
    whether a node is recursed into by calling the generic_visit method on
    the superclass.

    See the documentation here:
    https://www.projectfluent.org/python-fluent/fluent.syntax/stable/usage.html
    """

    def __init__(
        self, path, config, exclusions, contents, offsets_and_lines, brand_names=set()
    ):
        super().__init__()
        self.path = path
        self.config = config
        self.exclusions = exclusions
        self.contents = contents
        self.offsets_and_lines = offsets_and_lines

        self.results = []
        self.identifier_re = re.compile(r"[a-z0-9-]+")
        self.apostrophe_re = re.compile(r"\w'")
        self.incorrect_apostrophe_re = re.compile(r"\w\u2018\w")
        self.single_quote_re = re.compile(r"'(.+)'")
        self.double_quote_re = re.compile(r"\".+\"")
        self.ellipsis_re = re.compile(r"\.\.\.")

        self.brand_names = brand_names
        self.minimum_id_length = 9

        self.state = {
            # The resource comment should be at the top of the page after the license.
            "node_can_be_resource_comment": True,
            # Group comments must be followed by a message. Two group comments are not
            # allowed in a row.
            "can_have_group_comment": True,
            # Comment bound to the current message
            "comment": "",
            # The current group comment
            "group_comment": "",
            # Variables in the current message
            "variables": [],
        }

        attributes = [
            "label",
            "value",
            "accesskey",
            "alt",
            "title",
            "tooltiptext",
            "placeholder",
            "aria-label",
            "aria-description",
            "aria-valuetext",
            "style",
            # For XUL key/command setup.
            "key",
            "keycode",
            # For download filenames:
            "download",
            # Used in the Firefox prefs
            "searchkeywords",
            # Used by search-textbox.js
            "searchbuttonlabel",
            # Used in toolbar customization.
            "toolbarname",
            # Used in moz-message-bar.
            "message",
            # Used in dialogs (should be moved to using fluent IDs though)
            "buttonlabelaccept",
            "buttonaccesskeyaccept",
            "buttonlabelcancel",
            "buttonaccesskeycancel",
            "buttonlabelextra2",
            "buttonaccesskeyextra2",
            # Used in app menu notifications (should be moved to use fluent IDs)
            "buttonlabel",
            "buttonaccesskey",
            "secondarybuttonlabel",
            "secondarybuttonaccesskey",
            # Commonly used in Lit-based web components
            "heading",
            "description",
        ]
        self.known_attribute_list = [a.lower() for a in attributes]

        # Set this to true to debug print the root node's json. This is useful for
        # writing new lint rules, or debugging existing ones.
        self.debug_print_json = False

    def generic_visit(self, node):
        node_name = type(node).__name__
        self.state["node_can_be_resource_comment"] = self.state[
            "node_can_be_resource_comment"
        ] and (
            # This is the root node, empty space is allowed, comments are allowed
            node_name in {"Resource", "Span", "Comment"}
        )

        if self.debug_print_json:
            import json

            print(json.dumps(node.to_json(), indent=2))
            # Only debug print the root node.
            self.debug_print_json = False

        super().generic_visit(node)

    def visit_Attribute(self, node):
        # Only visit values for Attribute nodes, the identifier comes from dom.
        self.visit(node.value)

    def visit_FunctionReference(self, node):
        # We don't recurse into function references, the identifiers there are
        # allowed to be free form.
        pass

    def visit_Message(self, node):
        # There must be at least one message or term between group comments.
        self.state["can_have_group_comment"] = True
        self.last_message_id = node.id.name

        super().generic_visit(node)

        # Do this here instead as visit_Attribute doesn't have access to the
        # message's comment.
        for attr in node.attributes:
            if not attr.id.name.lower() in self.known_attribute_list:
                comment = self.state["comment"] + self.state["group_comment"]
                if not f".{attr.id.name}" in comment:
                    self.add_error(
                        attr,
                        "VA01",
                        "Use attributes designed for localized content directly."
                        " If script-based processing is necessary, add a comment"
                        f" explaining why. The linter didn't recognize: .{attr.id.name}",
                        "warning",
                    )

        # Check if variables are referenced in comments
        if self.state["variables"]:
            comments = self.state["comment"] + self.state["group_comment"]
            missing_references = [
                v for v in self.state["variables"] if f"${v}" not in comments
            ]
            if missing_references:
                self.add_error(
                    node,
                    "VC01",
                    "Messages including variables should have a comment "
                    "explaining what will replace the variable. "
                    "Missing references: "
                    + ", ".join([f"${m}" for m in missing_references]),
                )

        # Reset current comment and variable references after reading the
        # message.
        self.state["comment"] = ""
        self.state["variables"] = []

    def visit_Term(self, node):
        # There must be at least one message or term between group comments.
        self.state["can_have_group_comment"] = True
        self.last_message_id = None

        super().generic_visit(node)

        # Reset current comment and variable references after reading the term.
        self.state["comment"] = ""
        self.state["variables"] = []

    def visit_MessageReference(self, node):
        # We don't recurse into message references, the identifiers are either
        # checked elsewhere or are attributes and come from DOM.
        pass

    def visit_Identifier(self, node):
        if (
            self.path not in self.exclusions["ID01"]["files"]
            and node.name not in self.exclusions["ID01"]["messages"]
            and not self.identifier_re.fullmatch(node.name)
        ):
            self.add_error(
                node,
                "ID01",
                f"Identifiers may only contain lowercase characters and - (ID: {node.name})",
            )
        if (
            len(node.name) < self.minimum_id_length
            and self.path not in self.exclusions["ID02"]["files"]
            and node.name not in self.exclusions["ID02"]["messages"]
        ):
            self.add_error(
                node,
                "ID02",
                f"Identifiers must be at least {self.minimum_id_length} characters long (ID: {node.name}",
            )

    def visit_Pattern(self, node):
        # Check punctuation on the whole pattern, so that quotes and apostrophes
        # split across placeables or HTML tags are detected. Placeables are
        # replaced with a word, so that apostrophe checks still apply (e.g.
        # "{ -brand-short-name }'s" is checked as "foo's").
        parser = TextElementHTMLParser()
        parser.feed(
            "".join(
                el.value if isinstance(el, ast.TextElement) else "foo"
                for el in node.elements
            )
        )
        parser.close()
        text = "".join(parser.extracted_text)

        # To check for apostrophes, first remove pairs of straight quotes
        # used as delimiters.
        cleaned_str = re.sub(self.single_quote_re, "", text)
        checks = [
            (
                "TE01",
                self.apostrophe_re.search(cleaned_str),
                "Strings with apostrophes should use foo\u2019s instead of foo's.",
            ),
            (
                "TE02",
                self.incorrect_apostrophe_re.search(text),
                "Strings with apostrophes should use foo\u2019s instead of foo\u2018s.",
            ),
            (
                "TE03",
                self.single_quote_re.search(text),
                "Single-quoted strings should use Unicode \u2018foo\u2019 instead of 'foo'.",
            ),
            (
                "TE04",
                self.double_quote_re.search(text),
                'Double-quoted strings should use Unicode \u201cfoo\u201d instead of "foo".',
            ),
            (
                "TE05",
                self.ellipsis_re.search(text),
                "Strings with an ellipsis should use the Unicode \u2026 character"
                " instead of three periods",
            ),
        ]
        for rule, found, message in checks:
            if found and not self.is_excluded(rule):
                self.add_error(node, rule, message)

        self.generic_visit(node)

    def is_excluded(self, rule):
        # Terms can only be excluded by file, as last_message_id is None for them.
        return self.path in self.exclusions[rule]["files"] or (
            self.last_message_id is not None
            and self.last_message_id in self.exclusions[rule]["messages"]
        )

    def visit_TextElement(self, node):
        parser = TextElementHTMLParser()
        parser.feed(node.value)
        for text in parser.extracted_text:
            # If part of a message, check for brand names
            if (
                self.last_message_id is not None
                and self.path not in self.exclusions["CO01"]["files"]
                and self.last_message_id not in self.exclusions["CO01"]["messages"]
            ):
                found_brands = []
                for brand in sorted(self.brand_names):
                    if re.search(rf"\b{re.escape(brand)}\b", text):
                        found_brands.append(brand)
                if found_brands:
                    self.add_error(
                        node,
                        "CO01",
                        "Strings should use the corresponding terms instead of"
                        f" hard-coded brand names ({', '.join(found_brands)})",
                    )

    def visit_ResourceComment(self, node):
        # This node is a comment with: "###"
        if not self.state["node_can_be_resource_comment"]:
            self.add_error(
                node,
                "RC01",
                "Resource comments (###) should be placed at the top of the file, just "
                "after the license header. There should only be one resource comment "
                "per file.",
            )
            return

        lines_after = get_newlines_count_after(node.span, self.contents)
        lines_before = get_newlines_count_before(node.span, self.contents)

        if node.span.end == len(self.contents) - 1:
            # This file only contains a resource comment.
            return

        if lines_after != 2:
            self.add_error(
                node,
                "RC02",
                "Resource comments (###) should be followed by one empty line.",
            )
            return

        if lines_before != 2:
            self.add_error(
                node,
                "RC03",
                "Resource comments (###) should have one empty line above them.",
            )
            return

    def visit_SelectExpression(self, node):
        # We only want to visit the variant values, the identifiers in selectors
        # and keys are allowed to be free form.
        for variant in node.variants:
            self.visit(variant.value)

        # Store the variable used for the SelectExpression, excluding functions
        # like PLATFORM()
        if (
            type(node.selector) is ast.VariableReference
            and node.selector.id.name not in self.state["variables"]
        ):
            self.state["variables"].append(node.selector.id.name)

    def visit_Comment(self, node):
        # This node is a comment with: "#"

        # Store the comment
        self.state["comment"] = node.content

    def visit_GroupComment(self, node):
        # This node is a comment with: "##"

        # Store the group comment
        self.state["group_comment"] = node.content

        if not self.state["can_have_group_comment"]:
            self.add_error(
                node,
                "GC04",
                "Group comments (##) must be followed by at least one message "
                "or term. Make sure that a single group comment with multiple "
                "paragraphs is not separated by whitespace, as it will be "
                "interpreted as two different comments.",
            )
            return

        self.state["can_have_group_comment"] = False

        lines_after = get_newlines_count_after(node.span, self.contents)
        lines_before = get_newlines_count_before(node.span, self.contents)

        if node.span.end == len(self.contents) - 1:
            # The group comment is the last thing in the file.

            if node.content == "":
                # Empty comments are allowed at the end of the file.
                return

            self.add_error(
                node,
                "GC01",
                "Group comments (##) should not be at the end of the file, they should "
                "always be above a message. Only an empty group comment is allowed at "
                "the end of a file.",
            )
            return

        if lines_after != 2:
            self.add_error(
                node,
                "GC02",
                "Group comments (##) should be followed by one empty line.",
            )
            return

        if lines_before != 2:
            self.add_error(
                node,
                "GC03",
                "Group comments (##) should have an empty line before them.",
            )
            return

    def visit_VariableReference(self, node):
        # Identifiers are allowed to be free form, but need to store them
        # for comment checks.

        if node.id.name not in self.state["variables"]:
            self.state["variables"].append(node.id.name)

    def add_error(self, node, rule, msg, level=None):
        (col, line) = self.span_to_line_and_col(node.span)
        res = {
            "path": self.path,
            "lineno": line,
            "column": col,
            "rule": rule,
            "message": msg,
        }
        if level:
            res["level"] = level

        self.results.append(result.from_config(self.config, **res))

    def span_to_line_and_col(self, span):
        i = bisect.bisect_left(self.offsets_and_lines, (span.start, 0))
        if i > 0:
            col = span.start - self.offsets_and_lines[i - 1][0]
        else:
            col = 1 + span.start
        return (col, self.offsets_and_lines[i][1])


def get_offsets_and_lines(contents):
    """Return a list consisting of tuples of (offset, line).

    The Fluent AST contains spans of start and end offsets in the file.
    This function returns a list of offsets and line numbers so that errors
    can be reported using line and column.
    """
    line = 1
    result = []
    for m in re.finditer(r"\n", contents):
        result.append((m.start(), line))
        line += 1
    return result


def get_newlines_count_after(span, contents):
    # Determine the number of newlines.
    count = 0
    for i in range(span.end, len(contents)):
        assert contents[i] != "\r", "This linter does not handle \\r characters."
        if contents[i] != "\n":
            break
        count += 1

    return count


def get_newlines_count_before(span, contents):
    # Determine the range of newline characters.
    count = 0
    for i in range(span.start - 1, 0, -1):
        assert contents[i] != "\r", "This linter does not handle \\r characters."
        if contents[i] != "\n":
            break
        count += 1

    return count


def get_exclusions(root):
    with open(
        mozpath.join(root, "tools", "lint", "fluent-lint", "exclusions.yml")
    ) as f:
        exclusions = list(yaml.safe_load_all(f))[0]
        for error_type in exclusions:
            exclusions[error_type]["files"] = set([
                mozpath.join(root, x) for x in exclusions[error_type]["files"]
            ])
        return exclusions


def get_term_brand_names(pattern):
    """Return the brand names defined in a term's pattern.

    The pattern must be plain text, or a single select expression with
    plain-text variants. For any other structure, including references to
    other terms or nested select expressions, an empty list is returned.
    """

    def get_plain_text(pattern):
        elements = pattern.elements
        if len(elements) == 1 and isinstance(elements[0], ast.TextElement):
            return elements[0].value
        return None

    text = get_plain_text(pattern)
    if text is not None:
        return [text]

    elements = pattern.elements
    if not (
        len(elements) == 1
        and isinstance(elements[0], ast.Placeable)
        and isinstance(elements[0].expression, ast.SelectExpression)
    ):
        return []

    variants = [get_plain_text(v.value) for v in elements[0].expression.variants]
    if None in variants:
        return []
    return variants


def get_brand_names(root, brand_files):
    brands = set()
    for brand_path in brand_files:
        brand_file = mozpath.join(root, brand_path)
        if os.path.exists(brand_file):
            with open(brand_file, encoding="utf-8") as f:
                resource = parse(f.read())
            for entry in resource.body:
                if isinstance(entry, ast.Term):
                    brands.update(get_term_brand_names(entry.value))

    return brands


def lint(paths, config, fix=None, **lintargs):
    root = lintargs["root"]
    files = list(expand_exclusions(paths, config, root))
    exclusions = get_exclusions(root)
    brand_files = config.get("brand-files")
    brand_names = get_brand_names(root, brand_files)
    results = []
    for path in files:
        contents = open(path, encoding="utf-8").read()
        linter = Linter(
            path,
            config,
            exclusions,
            contents,
            get_offsets_and_lines(contents),
            brand_names,
        )
        linter.visit(parse(contents))
        results.extend(linter.results)
    return results

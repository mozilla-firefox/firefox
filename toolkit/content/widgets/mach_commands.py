# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re

from mach.decorators import Command, CommandArgument
from mach.util import UserError

FIXME_COMMENT = "// FIXME: replace with path to your reusable widget\n"
LICENSE_HEADER = """/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"""

JS_HEADER = """{license}
/// <reference path="./{element_name}.tagmap.d.ts" />

import {{ html }} from "../vendor/lit.all.mjs";
import {{ MozLitElement }} from "../lit-utils.mjs";

/**
 * Component description goes here.
 *
 * @tagname {element_name}
 * @property {{string}} variant - Property description goes here
 */
export class {class_name} extends MozLitElement {{
  static properties = {{
    variant: {{ type: String }},
  }};

  constructor() {{
    super();
    this.variant = "default";
  }}

  render() {{
    return html`
      <link rel="stylesheet" href="chrome://global/content/elements/{element_name}.css" />
      <div>Variant type: ${{this.variant}}</div>
    `;
  }}
}}
customElements.define("{element_name}", {class_name});
"""

TAGMAP_HEADER = """{license}
import type {{ {class_name} }} from "./{element_name}.mjs";

declare global {{
  interface HTMLElementTagNameMap {{
    "{element_name}": {class_name};
  }}
}}
"""

STORY_HEADER = """{license}
{html_lit_import}
{fixme_comment}import "{element_path}";

export default {{
  title: "{story_prefix}/{story_name}",
  component: "{element_name}",
  argTypes: {{
    variant: {{
      options: ["default", "other"],
      control: {{ type: "select" }},
    }},
  }},
}};

const Template = ({{ variant }}) => html`
  <{element_name} .variant=${{variant}}></{element_name}>
`;

export const Default = Template.bind({{}});
Default.args = {{
  variant: "default",
}};
"""


def run_mach(command_context, cmd, **kwargs):
    return command_context._mach_context.commands.dispatch(
        cmd, command_context._mach_context, **kwargs
    )


def run_npm(command_context, args):
    return run_mach(
        command_context, "npm", args=[*args, "--prefix=browser/components/storybook"]
    )


def parse_acorn_elements(file_path):
    with open(file_path, newline="\n") as file:
        content = file.read()

    # Regex to extract elements from the acornElements array
    pattern = r'\[\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,?\s*\]'
    matches = re.findall(pattern, content, flags=re.DOTALL)

    # Filter out tags that don't start with "moz-"
    filtered = [match for match in matches if match[0].startswith("moz-")]

    return filtered  # List of (tag, path tuples)


def add_sorted_entry(elements, new_tag, new_path):
    if not any(tag == new_tag for tag, _ in elements):
        elements.append((new_tag, new_path))
        elements.sort(key=lambda x: x[0])
    return elements


def update_acorn_elements_in_file(file_path, updated_elements):
    with open(file_path, newline="\n") as file:
        lines = file.readlines()
    # Locate the start and end of the acornElements array
    start_id = None
    end_id = None
    for i, line in enumerate(lines):
        if "let acornElements = [" in line:
            start_id = i
        if start_id is not None and line.strip() == "];":
            end_id = i
        if start_id is not None and end_id is not None:
            break
    if start_id is None:
        raise ValueError("Could not find 'let acornElements = [' in customElements.js.")
    if end_id is None:
        raise ValueError(
            "Could not find a closing bracket to 'let acornElements = [' in customElements.js."
        )
    # Build updated array block
    array_lines = ["let acornElements = [\n"]
    for tag, path in updated_elements:
        array_lines.append(f'  ["{tag}", "{path}"],\n')
    array_lines.append("];\n")

    # Replace old array block with new one
    new_lines = lines[:start_id] + array_lines + lines[end_id + 1 :]

    with open(file_path, "w", newline="\n") as file:
        file.writelines(new_lines)


@Command(
    "addwidget",
    category="misc",
    description="Scaffold a front-end component.",
)
@CommandArgument(
    "names",
    nargs="+",
    help="Component names to create in kebab-case, eg. my-card.",
)
def addwidget(command_context, names):
    story_prefix = "UI Widgets"
    html_lit_import = 'import { html } from "../vendor/lit.all.mjs";'
    for name in names:
        component_dir = f"toolkit/content/widgets/{name}"

        try:
            os.mkdir(component_dir)
        except FileExistsError:
            pass

        with open(f"{component_dir}/{name}.mjs", "w", newline="\n") as f:
            class_name = "".join(p.capitalize() for p in name.split("-"))
            f.write(
                JS_HEADER.format(
                    license=LICENSE_HEADER,
                    element_name=name,
                    class_name=class_name,
                )
            )

        with open(f"{component_dir}/{name}.tagmap.d.ts", "w", newline="\n") as f:
            f.write(
                TAGMAP_HEADER.format(
                    license=LICENSE_HEADER,
                    element_name=name,
                    class_name=class_name,
                )
            )

        with open(f"{component_dir}/{name}.css", "w", newline="\n") as f:
            f.write(LICENSE_HEADER)

        test_name = name.replace("-", "_")
        test_path = f"toolkit/content/tests/widgets/test_{test_name}.html"
        jar_path = "toolkit/content/jar.mn"
        jar_lines = None
        with open(jar_path) as f:
            jar_lines = f.readlines()
        elements_startswith = "   content/global/elements/"
        new_css_line = (
            f"{elements_startswith}{name}.css    (widgets/{name}/{name}.css)\n"
        )
        new_js_line = (
            f"{elements_startswith}{name}.mjs    (widgets/{name}/{name}.mjs)\n"
        )
        new_jar_lines = []
        found_elements_section = False
        added_widget = False
        for line in jar_lines:
            if line.startswith(elements_startswith):
                found_elements_section = True
            if found_elements_section and not added_widget and line > new_css_line:
                added_widget = True
                new_jar_lines.append(new_css_line)
                new_jar_lines.append(new_js_line)
            new_jar_lines.append(line)

        with open(jar_path, "w", newline="\n") as f:
            f.write("".join(new_jar_lines))

        custom_elements = parse_acorn_elements("toolkit/content/customElements.js")
        custom_elements = add_sorted_entry(
            custom_elements, f"{name}", f"chrome://global/content/elements/{name}.mjs"
        )
        update_acorn_elements_in_file(
            "toolkit/content/customElements.js", custom_elements
        )

        # Run prettier to fix the formatting generated by adding a new
        # entry to the Acorn elements array
        run_mach(
            command_context, "lint", argv=["--fix", "toolkit/content/customElements.js"]
        )

        story_path = f"{component_dir}/{name}.stories.mjs"
        element_path = f"./{name}.mjs"
        with open(story_path, "w", newline="\n") as f:
            story_name = " ".join(
                name for name in re.findall(r"[A-Z][a-z]+", class_name) if name != "Moz"
            )
            f.write(
                STORY_HEADER.format(
                    license=LICENSE_HEADER,
                    element_name=name,
                    story_name=story_name,
                    story_prefix=story_prefix,
                    fixme_comment="",
                    element_path=element_path,
                    html_lit_import=html_lit_import,
                )
            )

        run_mach(
            command_context, "addtest", argv=[test_path, "--suite", "mochitest-chrome"]
        )


@Command(
    "addstory",
    category="misc",
    description="Scaffold a front-end Storybook story.",
)
@CommandArgument(
    "name",
    help="Story to create in kebab-case, eg. my-card.",
)
@CommandArgument(
    "project_name",
    type=str,
    help='Name of the project or team for the new component to keep stories organized. Eg. "Credential Management"',
)
@CommandArgument(
    "--path",
    help="Path to the widget source, eg. /browser/components/my-module.mjs or chrome://browser/content/my-module.mjs",
)
def addstory(command_context, name, project_name, path):
    html_lit_import = 'import { html } from "lit.all.mjs";'
    story_path = f"browser/components/storybook/stories/{name}.stories.mjs"
    project_name = project_name.split()
    project_name = " ".join(p.capitalize() for p in project_name)
    story_prefix = f"Domain-specific UI Widgets/{project_name}"
    with open(story_path, "w", newline="\n") as f:
        print(f"Creating new story {name} in {story_path}")
        story_name = " ".join(p.capitalize() for p in name.split("-"))
        f.write(
            STORY_HEADER.format(
                license=LICENSE_HEADER,
                element_name=name,
                story_name=story_name,
                element_path=path,
                fixme_comment="" if path else FIXME_COMMENT,
                project_name=project_name,
                story_prefix=story_prefix,
                html_lit_import=html_lit_import,
            )
        )


@Command(
    "buildtokens",
    category="misc",
    description="Build the design tokens CSS files",
)
@CommandArgument(
    "--import-figma",
    action="store_true",
    dest="import_figma",
    help="Import Nova design tokens from the committed figma-variables-all.json "
    "(no network or token needed), then build. Add --remote to fetch a fresh "
    "export from Figma first.",
)
@CommandArgument(
    "--remote",
    action="store_true",
    help="With --import-figma, fetch a fresh export from the Figma API before "
    "importing. Requires a valid FIGMA_ACCESS_TOKEN in your environment.",
)
@CommandArgument(
    "--fetch-figma",
    action="store_true",
    help="Alias for `--import-figma --remote`.",
)
@CommandArgument(
    "--match",
    action="append",
    dest="match",
    default=None,
    metavar="SUBSTRING",
    help="Only import changed tokens whose path contains SUBSTRING. May be "
    "passed multiple times. Requires --import-figma.",
)
@CommandArgument(
    "--all",
    action="store_true",
    dest="import_all",
    help="Import every changed token without prompting for a selection. "
    "Requires --import-figma.",
)
def buildtokens(command_context, import_figma, remote, fetch_figma, match, import_all):
    # `--fetch-figma` is an alias for `--import-figma --remote`.
    do_import = import_figma or fetch_figma
    do_remote = remote or fetch_figma
    if (match or import_all or remote) and not do_import:
        raise UserError("--match, --all, and --remote require --import-figma.")
    if run_mach(
        command_context,
        "npm",
        args=["ls", "--prefix=toolkit/themes/shared/design-system"],
    ):
        run_mach(
            command_context,
            "npm",
            args=["ci", "--prefix=toolkit/themes/shared/design-system"],
        )
    if do_import:
        fetch_args = [
            "run",
            "import-figma-nova",
            "--prefix=toolkit/themes/shared/design-system",
            "--",
        ]
        if do_remote:
            fetch_args.append("--remote")
        for substring in match or []:
            fetch_args.append(f"--match={substring}")
        if import_all:
            fetch_args.append("--all")
        failed = run_mach(command_context, "npm", args=fetch_args)
        if failed:
            if do_remote:
                raise UserError(
                    "Failed to access Figma API, is FIGMA_ACCESS_TOKEN set and valid?"
                )
            raise UserError("Failed to import tokens from figma-variables-all.json.")
    run_mach(
        command_context,
        "npm",
        args=[
            "run",
            "build-figma-nova",
            "--prefix=toolkit/themes/shared/design-system",
        ],
    )
    run_mach(
        command_context,
        "npm",
        args=[
            "run",
            "build-web",
            "--prefix=toolkit/themes/shared/design-system",
            "--",
            "--override=nova",
        ],
    )
    run_mach(
        command_context,
        "npm",
        args=["run", "format-web", "--prefix=toolkit/themes/shared/design-system"],
    )
    run_mach(
        command_context,
        "npm",
        args=["run", "build", "--prefix=toolkit/themes/shared/design-system"],
    )

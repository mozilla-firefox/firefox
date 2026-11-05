# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re

from docutils.nodes import Text, paragraph, reference
from sphinx.transforms import SphinxTransform


class ConvertBugsToLinks(SphinxTransform):
    # Convert text entries in paragraphs that are in the style of "bug xxxxx"
    # to a hyperlink that leads to the appropriate bugzilla bug link.

    default_priority = 400
    bz_url = "https://bugzilla.mozilla.org/show_bug.cgi?id={0}"
    bz_reg = re.compile(r"\bbug [0-9]+\b", re.IGNORECASE)

    def apply(self):
        for node in list(self.document.findall(paragraph)):
            for text in list(node.findall(Text)):
                if not isinstance(text.parent, reference):
                    self.convert(text)

    def convert(self, text):
        parts = []
        pos = 0
        for match in self.bz_reg.finditer(text):
            bug = match.group()
            parts.append(Text(text[pos : match.start()]))
            parts.append(reference(bug, bug, refuri=self.bz_url.format(bug[4:])))
            pos = match.end()
        if parts:
            parts.append(Text(text[pos:]))
            text.parent.replace(text, parts)


def setup(app):
    app.add_transform(ConvertBugsToLinks)

    return {"parallel_read_safe": True, "parallel_write_safe": True}

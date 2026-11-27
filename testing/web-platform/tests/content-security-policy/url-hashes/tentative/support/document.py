import html
import os

from wptserve.pipes import template


# Serves the support document given in the file parameter, i.e.
# <file>.sub.html in this directory, with its {{GET[...]}} substituted like
# for a .sub.html file. If there's a policy parameter, the policy is delivered
# in a meta tag, which saves having a copy of each support document with it.
# A policy in a header is delivered with the header() pipe.
def main(request, response):
    file = request.GET.first(b"file").decode()
    if os.path.basename(file) != file:
        return 400, [], b"file must be in the directory of document.py"
    path = os.path.join(os.path.dirname(__file__), f"{file}.sub.html")
    with open(path, "rb") as f:
        content = template(request, f.read())

    if b"policy" in request.GET:
        if b"<html>" not in content:
            return 500, [], b"No <html> to add the meta tag to"
        # The meta tag goes first, so that the policy applies to all of the
        # document. It ends up in the <head> created by the parser.
        policy = html.escape(request.GET.first(b"policy").decode())
        meta = ('\n  <meta http-equiv="Content-Security-Policy" '
                f'content="{policy}">')
        content = content.replace(b"<html>", b"<html>" + meta.encode(), 1)

    response.headers.set(b"Content-Type", b"text/html; charset=utf-8")
    return content

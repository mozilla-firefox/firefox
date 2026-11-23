#!/usr/bin/env vpython3

# Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
"""Script to generate a WebRTC changelog in HTML format.

This script parses git logs between two branches and categorizes commits
to produce a user-friendly changelog.
"""

import argparse
import collections
import html
import re
import subprocess
import sys

# Define categories and keyword mappings
CATEGORIES = {
    'API': ['api/'],
    'Transport': ['transport', 'dtls', 'ice', 'p2p', 'rtp', 'sctp'],
    'Audio': ['audio', 'aec3', 'voice', 'audio_processing'],
    'Video': ['video', 'h264', 'vp8', 'vp9', 'av1', 'video_coding'],
    'Peerconnection': ['pc/', 'peer_connection', 'signaling', 'jsep'],
    'Stats': ['stats', 'collector', 'rtc_stats'],
    'Security': ['security', 'bounds', 'overflow'],
    'Infrastructure': ['build', 'gn', 'ninja', 'iwyu', 'owners', 'watchlist'],
}

CHANGELOG_CSS = """
    body {
        font-family: sans-serif;
        line-height: 1.4;
        color: #333;
        max-width: 1200px;
        margin: 0 auto;
        padding: 15px;
        font-size: 0.9em;
    }
    h1 {
        color: #1a73e8;
        border-bottom: 2px solid #1a73e8;
        padding-bottom: 5px;
        font-size: 1.5em;
    }
    h2 {
        color: #1a73e8;
        font-size: 1.2em;
        margin: 15px 0 5px 0;
    }
    table {
        width: 100%;
        border-collapse: collapse;
        margin-top: 10px;
        margin-bottom: 20px;
    }
    th, td {
        border: 1px solid #dadce0;
        padding: 6px;
        text-align: left;
        vertical-align: top;
    }
    th {
        background-color: #f8f9fa;
    }
    a {
        color: #1a73e8;
        text-decoration: none;
    }
    .external-links {
        margin-bottom: 15px;
        padding: 10px;
        background: #f1f3f4;
        border-radius: 4px;
    }
    .external-links a {
        font-weight: bold;
        margin-right: 15px;
    }
    .summary-container {
        display: flex;
        gap: 15px;
        align-items: flex-start;
    }
    .overall-summary {
        flex: 2;
    }
    .category-summary {
        flex: 1;
    }
    .category-summary table {
        margin-top: 0;
    }
"""


def get_category(subject, body):
    """Categorizes a commit based on keywords in the subject and body."""
    combined = (subject + ' ' + body).lower()
    # Split by whitespace or the presence of "/" but keep the "/"
    # e.g. "pc/srtp_session.cc" -> ["pc/", "srtp_session.cc"]
    # Then strip punctuation to allow exact matching with keywords.
    words = [
        w.strip('.,:;()[]{}') for w in re.split(r'(?<=/)|\s+', combined) if w
    ]
    for cat, keywords in CATEGORIES.items():
        if any(kw in words for kw in keywords):
            return cat
    return 'General'


def format_single_bug(bug_str):
    patterns = [
        (r'webrtc:(\d+)', r'https://issues.webrtc.org/issues/\1'),
        (r'chromium:(\d+)', r'https://issues.chromium.org/issues/\1'),
        (r'b/(\d+)', r'https://issues.chromium.org/issues/\1'),
        (r'b:(\d+)', r'https://issues.chromium.org/issues/\1'),
    ]
    for pattern, url in patterns:
        if re.search(pattern, bug_str, re.IGNORECASE):
            url_part = re.sub(pattern, url, bug_str, flags=re.IGNORECASE)
            return (f'<a href="{html.escape(url_part)}">'
                    f'{html.escape(bug_str)}</a>')
    return html.escape(bug_str)


def format_bugs(bug_list):
    """Formats a list of bug strings into HTML links, separated by commas."""
    if not bug_list:
        return 'None'
    formatted_bugs = []
    for bug_str in bug_list.split(','):
        bug_str = bug_str.strip()
        if bug_str and bug_str.lower() != 'none':
            formatted_bugs.append(format_single_bug(bug_str))
    return ', '.join(formatted_bugs) if formatted_bugs else 'None'


def parse_git_commits(log_text):
    # Split by the custom delimiter, and filter out empty strings
    commit_chunks = log_text.split('--WEBRTC-COMMIT-DELIMITER--')
    commits = []
    for chunk in commit_chunks:
        chunk = chunk.strip()
        if not chunk:
            continue

        lines = chunk.splitlines()
        if len(lines) < 2:
            # Should have at least (hash, author, subject) and a
            # (potentially empty) body line.
            continue

        [commit_hash, author, subject] = lines[0].split(' ', 2)
        body_lines = lines[1:]

        bugs = []
        review_url = None
        parsed_body = []
        # Extract Bug and Reviewed-on from body lines and keep all body lines.
        for line in body_lines:
            line_stripped = line.strip()
            if line_stripped.startswith('Bug:') or line_stripped.startswith(
                    'Fixed:'):
                # Note: this is a single string even if there are multiple
                # comma-separated bug ids.
                bugs.append(line.split(':', 1)[1].strip())
            elif line_stripped.startswith('Reviewed-on:'):
                review_url = line.split(':', 1)[1].strip()
            parsed_body.append(line)
        category = get_category(subject, '\n'.join(parsed_body))
        bug = ','.join(bugs) if bugs else 'None'

        commits.append({
            'hash': commit_hash,
            'author': author,
            'subject': subject,
            'bug': bug,
            'review_url': review_url,
            'category': category,
            'body': parsed_body
        })

    return commits


def parse_log(log_text):
    """Parses and filter git commits."""
    raw_commits = parse_git_commits(log_text)

    filtered_commits = []
    for commit in raw_commits:
        if commit['subject'] == 'Update WebRTC code version':
            continue
        if commit['subject'].startswith('Roll '):
            continue
        exclude_prefixes = ('Revert', 'Reland', 'Reapply')
        if commit['subject'].startswith(exclude_prefixes):
            continue
        if 'webrtc-version-updater' in commit['author']:
            continue
        filtered_commits.append(commit)

    return filtered_commits


def format_summary(summary_text):
    """Formats a plain-text summary as an HTML list.

    Each non-empty line becomes a list item, without any leading "-" or "*"
    bullet marker. The text is HTML-escaped, so that a summary derived from
    commit messages cannot inject markup into the report.
    """
    items = []
    for line in summary_text.splitlines():
        item = re.sub(r'^\s*[-*]\s+', '', line).strip()
        if item:
            items.append(f'<li>{html.escape(item)}</li>')
    return f'<ul>{"".join(items)}</ul>' if items else ''


def generate_html(commits,
                  from_branch,
                  to_branch,
                  milestone=None,
                  summary_text=None):
    """Generates an HTML changelog from a list of commits."""
    authors = set(commit['author'] for commit in commits)
    full_log_url = (f'https://webrtc.googlesource.com/src/+log/branch-heads/'
                    f'{from_branch}..branch-heads/{to_branch}')
    schedule_url = 'https://chromiumdash.appspot.com/schedule'
    cat_counts = collections.defaultdict(int)
    for commit in commits:
        cat_counts[commit['category']] += 1

    summary_html = format_summary(summary_text) if summary_text else ''
    ai_summary_html = (f'<div id="ai-summary">{summary_html}</div>'
                       if summary_html else
                       '<div id="ai-summary">AI_SUMMARY_PLACEHOLDER</div>')

    formatted_milestone = f'{milestone} ' if milestone else ''
    html_parts = [
        f"""<!DOCTYPE html>
<html>
<head>
<style>
{CHANGELOG_CSS}
</style>
</head>
<body>
<h1>WebRTC Changelog {formatted_milestone} ({from_branch}..{to_branch})</h1>
<div class="external-links">
    <a href="{full_log_url}">Full list of changes</a>
    <a href="{schedule_url}">Chromium Release Schedule</a>
</div>
<div class="summary-container">
    <div class="overall-summary">
        <p>
          This release contains {len(commits)} commits
          by {len(authors)} authors.
        </p>
        <h2>Summary (AI-generated)</h2>
        {ai_summary_html}
    </div>
    <div class="category-summary">
        <h2>Categories</h2>
        <table>
            <thead><tr><th>Category</th><th>Changes</th></tr></thead>
            <tbody>
"""
    ]
    for cat, count in sorted(cat_counts.items()):
        html_parts.append(f'<tr><td>{cat}</td><td>{count}</td></tr>')
    html_parts.append("""</tbody></table></div></div>
<h2>Detailed List of Changes (newest first)</h2>
<table>
    <thead>
        <tr>
            <th>Change Description</th>
            <th>Category</th>
            <th>Links</th>
            <th>Bug</th>
        </tr>
    </thead>
    <tbody>
""")
    for commit in commits:
        commit_link = (f'<a href="https://webrtc.googlesource.com/src/+/'
                       f'{commit["hash"]}">📝</a>')
        review_link = (f'<a href="{html.escape(commit["review_url"])}">🔍</a>'
                       if commit['review_url'] else '')
        html_parts.append(
            f'<tr><td>{html.escape(commit["subject"])}</td>'
            f'<td>{commit["category"]}</td>'
            f'<td style="white-space:nowrap;">{commit_link} {review_link}</td>'
            f'<td>{format_bugs(commit["bug"])}</td></tr>')
    html_parts.append('</tbody></table></body></html>')
    return ''.join(html_parts)


def branch_number(value):
    """Validates a branch number such as 7727, which must be digits only."""
    if not re.fullmatch(r'[0-9]+', value):
        raise argparse.ArgumentTypeError(
            f'invalid branch number {value!r}, expected digits only')
    return value


def milestone_name(value):
    """Validates a milestone such as M148. Also accepts 148."""
    match = re.fullmatch(r'M?([0-9]+)', value)
    if not match:
        raise argparse.ArgumentTypeError(
            f'invalid milestone {value!r}, expected e.g. M148')
    return f'M{match.group(1)}'


def main():
    parser = argparse.ArgumentParser(
        description='Generate WebRTC changelog with optional AI summary.')
    parser.add_argument('branch1',
                        type=branch_number,
                        help='First branch for comparison (e.g., 7727).')
    parser.add_argument('branch2',
                        type=branch_number,
                        help='Second branch for comparison (e.g., 7778).')
    parser.add_argument('output_file', help='Path to the output HTML file.')
    parser.add_argument('--summary_file',
                        help='Optional path to a text file with the '
                        'AI-generated summary, one bullet point per line. '
                        'The text is HTML-escaped.',
                        default=None)
    parser.add_argument('--milestone',
                        type=milestone_name,
                        help='Optional milestone name (e.g. M148).',
                        default=None)

    args = parser.parse_args()

    summary_text = None
    if args.summary_file:
        with open(args.summary_file, encoding='utf-8') as summary_fp:
            summary_text = summary_fp.read()

    try:
        log_content = subprocess.check_output([
            'git', 'log', '--format=%h %ae %s%n%b--WEBRTC-COMMIT-DELIMITER--',
            f'branch-heads/{args.branch1}..branch-heads/{args.branch2}'
        ]).decode('utf-8')
    except subprocess.CalledProcessError as error:
        print(f'Error running git log: {error}', file=sys.stderr)
        sys.exit(1)

    commits = parse_log(log_content)

    if not commits:
        print('No commits found in the specified range.')
        sys.exit(0)

    branch_info = f'({args.branch1} to {args.branch2})'
    if args.milestone:
        branch_info = f'{args.milestone} {branch_info}'

    changelog_html = generate_html(commits,
                                   from_branch=args.branch1,
                                   to_branch=args.branch2,
                                   milestone=args.milestone,
                                   summary_text=summary_text)

    with open(args.output_file, 'w') as file_handle:
        file_handle.write(changelog_html)

    print(f'Changelog written to {args.output_file}')


if __name__ == '__main__':
    main()

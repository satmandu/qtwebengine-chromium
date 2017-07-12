# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

from webkitpy.common.host_mock import MockHost
from webkitpy.common.system.executive_mock import MockExecutive, mock_git_commands
from webkitpy.w3c.chromium_commit import ChromiumCommit
from webkitpy.w3c.test_exporter import TestExporter
from webkitpy.w3c.wpt_github import PullRequest
from webkitpy.w3c.wpt_github_mock import MockWPTGitHub
from webkitpy.w3c.gerrit_mock import MockGerrit


class TestExporterTest(unittest.TestCase):

    def setUp(self):
        self.host = MockHost()

    def test_dry_run_stops_before_creating_pr(self):
        host = MockHost()
        host.executive = mock_git_commands({
            'crrev-parse': 'c2087acb00eee7960339a0be34ea27d6b20e1131',
        })
        test_exporter = TestExporter(host, 'gh-username', 'gh-token', gerrit_user=None,
                                     gerrit_token=None, dry_run=True)
        test_exporter.wpt_github = MockWPTGitHub(pull_requests=[
            PullRequest(title='title1', number=1234, body='', state='open'),
        ])
        test_exporter.gerrit = MockGerrit(host, 'gerrit-username', 'gerrit-token')
        test_exporter.get_exportable_commits = lambda limit: [
            ChromiumCommit(host, position='refs/heads/master@{#458475}'),
            ChromiumCommit(host, position='refs/heads/master@{#458476}'),
            ChromiumCommit(host, position='refs/heads/master@{#458477}'),
        ]
        test_exporter.run()

        self.assertEqual(test_exporter.wpt_github.calls, ['all_pull_requests'])

    def test_creates_pull_request_for_all_exportable_commits(self):
        host = MockHost()

        def mock_command(args):
            canned_git_outputs = {
                'rev-list': '\n'.join([
                    'c881563d734a86f7d9cd57ac509653a61c45c240',
                    'add087a97844f4b9e307d9a216940582d96db306',
                    'cbcd201627f19f81a1697aaaa93cc3a41e644dab',
                ]),
                'footers': 'fake-cr-position',
                'remote': 'github',
                'format-patch': 'fake patch',
                'diff': 'fake patch diff',
                'diff-tree': 'fake\n\files\nchanged',
                'crrev-parse': 'c881563d734a86f7d9cd57ac509653a61c45c240',
            }
            if args[1] == 'show':
                if 'c881563d734a86f7d9cd57ac509653a61c45c240' in args:
                    return 'older fake text'
                elif 'add087a97844f4b9e307d9a216940582d96db306' in args:
                    return 'newer fake text'
                elif 'cbcd201627f19f81a1697aaaa93cc3a41e644dab' in args:
                    return 'newest fake text'
            return canned_git_outputs.get(args[1], '')

        host.executive = MockExecutive(run_command_fn=mock_command)
        test_exporter = TestExporter(host, 'gh-username', 'gh-token', gerrit_user=None, gerrit_token=None)
        test_exporter.wpt_github = MockWPTGitHub(pull_requests=[], create_pr_fail_index=1)
        test_exporter.gerrit = MockGerrit(host, 'gerrit-username', 'gerrit-token')
        test_exporter.run()

        self.assertEqual(test_exporter.wpt_github.calls, [
            'all_pull_requests',
            'create_pr',
            'create_pr',
            'create_pr',
        ])
        self.assertEqual(test_exporter.wpt_github.pull_requests_created, [
            ('chromium-export-c881563d73', 'older fake text', 'older fake text'),
            ('chromium-export-cbcd201627', 'newest fake text', 'newest fake text'),
        ])

    def test_creates_and_merges_pull_requests(self):
        # This tests 4 exportable commits:
        # 1. #458475 has an in-flight PR associated with it but cannot be merged.
        # 2. #458476 has no PR associated with it and should have one created.
        # 3. #458477 has a closed PR associated with it and should be skipped.
        # 4. #458478 has an in-flight PR associated with it and should be merged successfully.
        host = MockHost()
        host.executive = mock_git_commands({
            'show': 'git show text\nCr-Commit-Position: refs/heads/master@{#458476}',
            'crrev-parse': 'c2087acb00eee7960339a0be34ea27d6b20e1131',
        })
        test_exporter = TestExporter(host, 'gh-username', 'gh-token', gerrit_user=None, gerrit_token=None)
        test_exporter.wpt_github = MockWPTGitHub(pull_requests=[
            PullRequest(
                title='Open PR',
                number=1234,
                body='rutabaga\nCr-Commit-Position: refs/heads/master@{#458475}',
                state='open'
            ),
            PullRequest(
                title='Merged PR',
                number=2345,
                body='rutabaga\nCr-Commit-Position: refs/heads/master@{#458477}',
                state='closed'
            ),
            PullRequest(
                title='Open PR',
                number=3456,
                body='rutabaga\nCr-Commit-Position: refs/heads/master@{#458478}',
                state='open'
            ),
        ], unsuccessful_merge_index=0)
        test_exporter.gerrit = MockGerrit(host, 'gerrit-username', 'gerrit-token')
        test_exporter.get_exportable_commits = lambda limit: [
            ChromiumCommit(host, position='refs/heads/master@{#458475}'),
            ChromiumCommit(host, position='refs/heads/master@{#458476}'),
            ChromiumCommit(host, position='refs/heads/master@{#458477}'),
            ChromiumCommit(host, position='refs/heads/master@{#458478}'),
        ]
        test_exporter.run()
        self.assertEqual(test_exporter.wpt_github.calls, [
            'all_pull_requests',
            'get_pr_branch',
            'merge_pull_request',
            'create_pr',
            'get_pr_branch',
            'merge_pull_request',
            'delete_remote_branch',
        ])
        self.assertEqual(test_exporter.wpt_github.pull_requests_created, [
            ('chromium-export-c2087acb00',
             'git show text\nCr-Commit-Position: refs/heads/master@{#458476}',
             'git show text\nCr-Commit-Position: refs/heads/master@{#458476}'),
        ])

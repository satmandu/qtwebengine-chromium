#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Applies edits generated by a clang tool that was run on Chromium code.

Synopsis:

  cat run_tool.out | extract_edits.py | apply_edits.py <build dir> <filters...>

For example - to apply edits only to WTF sources:

  ... | apply_edits.py out/gn third_party/WebKit/Source/wtf

In addition to filters specified on the command line, the tool also skips edits
that apply to files that are not covered by git.
"""

import argparse
import collections
import functools
import multiprocessing
import os
import os.path
import subprocess
import sys

script_dir = os.path.dirname(os.path.realpath(__file__))
tool_dir = os.path.abspath(os.path.join(script_dir, '../pylib'))
sys.path.insert(0, tool_dir)

from clang import compile_db

Edit = collections.namedtuple('Edit',
                              ('edit_type', 'offset', 'length', 'replacement'))


def _GetFilesFromGit(paths=None):
  """Gets the list of files in the git repository.

  Args:
    paths: Prefix filter for the returned paths. May contain multiple entries.
  """
  args = []
  if sys.platform == 'win32':
    args.append('git.bat')
  else:
    args.append('git')
  args.append('ls-files')
  if paths:
    args.extend(paths)
  command = subprocess.Popen(args, stdout=subprocess.PIPE)
  output, _ = command.communicate()
  return [os.path.realpath(p) for p in output.splitlines()]


def _ParseEditsFromStdin(build_directory):
  """Extracts generated list of edits from the tool's stdout.

  The expected format is documented at the top of this file.

  Args:
    build_directory: Directory that contains the compile database. Used to
      normalize the filenames.
    stdout: The stdout from running the clang tool.

  Returns:
    A dictionary mapping filenames to the associated edits.
  """
  path_to_resolved_path = {}
  def _ResolvePath(path):
    if path in path_to_resolved_path:
      return path_to_resolved_path[path]

    if not os.path.isfile(path):
      resolved_path = os.path.realpath(os.path.join(build_directory, path))
    else:
      resolved_path = path

    if not os.path.isfile(resolved_path):
      sys.stderr.write('Edit applies to a non-existent file: %s\n' % path)
      resolved_path = None

    path_to_resolved_path[path] = resolved_path
    return resolved_path

  edits = collections.defaultdict(list)
  for line in sys.stdin:
    line = line.rstrip("\n\r")
    try:
      edit_type, path, offset, length, replacement = line.split(':::', 4)
      replacement = replacement.replace('\0', '\n')
      path = _ResolvePath(path)
      if not path: continue
      edits[path].append(Edit(edit_type, int(offset), int(length), replacement))
    except ValueError:
      sys.stderr.write('Unable to parse edit: %s\n' % line)
  return edits


def _ApplyEditsToSingleFile(filename, edits):
  # Sort the edits and iterate through them in reverse order. Sorting allows
  # duplicate edits to be quickly skipped, while reversing means that
  # subsequent edits don't need to have their offsets updated with each edit
  # applied.
  edit_count = 0
  error_count = 0
  edits.sort()
  last_edit = None
  with open(filename, 'rb+') as f:
    contents = bytearray(f.read())
    for edit in reversed(edits):
      if edit == last_edit:
        continue
      if (last_edit is not None and edit.edit_type == last_edit.edit_type and
          edit.offset == last_edit.offset and edit.length == last_edit.length):
        sys.stderr.write(
            'Conflicting edit: %s at offset %d, length %d: "%s" != "%s"\n' %
            (filename, edit.offset, edit.length, edit.replacement,
             last_edit.replacement))
        error_count += 1
        continue

      last_edit = edit
      contents[edit.offset:edit.offset + edit.length] = edit.replacement
      if not edit.replacement:
        _ExtendDeletionIfElementIsInList(contents, edit.offset)
      edit_count += 1
    f.seek(0)
    f.truncate()
    f.write(contents)
  return (edit_count, error_count)


def _ApplyEdits(edits):
  """Apply the generated edits.

  Args:
    edits: A dict mapping filenames to Edit instances that apply to that file.
  """
  edit_count = 0
  error_count = 0
  done_files = 0
  for k, v in edits.iteritems():
    tmp_edit_count, tmp_error_count = _ApplyEditsToSingleFile(k, v)
    edit_count += tmp_edit_count
    error_count += tmp_error_count
    done_files += 1
    percentage = (float(done_files) / len(edits)) * 100
    sys.stdout.write('Applied %d edits (%d errors) to %d files [%.2f%%]\r' %
                     (edit_count, error_count, done_files, percentage))

  sys.stdout.write('\n')
  return -error_count


_WHITESPACE_BYTES = frozenset((ord('\t'), ord('\n'), ord('\r'), ord(' ')))


def _ExtendDeletionIfElementIsInList(contents, offset):
  """Extends the range of a deletion if the deleted element was part of a list.

  This rewriter helper makes it easy for refactoring tools to remove elements
  from a list. Even if a matcher callback knows that it is removing an element
  from a list, it may not have enough information to accurately remove the list
  element; for example, another matcher callback may end up removing an adjacent
  list element, or all the list elements may end up being removed.

  With this helper, refactoring tools can simply remove the list element and not
  worry about having to include the comma in the replacement.

  Args:
    contents: A bytearray with the deletion already applied.
    offset: The offset in the bytearray where the deleted range used to be.
  """
  char_before = char_after = None
  left_trim_count = 0
  for byte in reversed(contents[:offset]):
    left_trim_count += 1
    if byte in _WHITESPACE_BYTES:
      continue
    if byte in (ord(','), ord(':'), ord('('), ord('{')):
      char_before = chr(byte)
    break

  right_trim_count = 0
  for byte in contents[offset:]:
    right_trim_count += 1
    if byte in _WHITESPACE_BYTES:
      continue
    if byte == ord(','):
      char_after = chr(byte)
    break

  if char_before:
    if char_after:
      del contents[offset:offset + right_trim_count]
    elif char_before in (',', ':'):
      del contents[offset - left_trim_count:offset]


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      'build_directory',
      help='path to the build dir (dir that edit paths are relative to)')
  parser.add_argument(
      'path_filter',
      nargs='*',
      help='optional paths to filter what files the tool is run on')
  args = parser.parse_args()

  filenames = set(_GetFilesFromGit(args.path_filter))
  edits = _ParseEditsFromStdin(args.build_directory)
  return _ApplyEdits(
      {k: v for k, v in edits.iteritems()
            if os.path.realpath(k) in filenames})


if __name__ == '__main__':
  sys.exit(main())

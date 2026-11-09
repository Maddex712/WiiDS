#!/usr/bin/env python3
"""Fail if anything in this repository names a particular game.

Nothing game-specific belongs in gcn-recomp (see CLAUDE.md): game names, disc IDs and a
game's own nouns live in the game projects. The terms checked are in tools/game_terms.txt.

    check_generic.py              every tracked file
    check_generic.py --staged     what is staged for commit (the pre-commit hook)
    check_generic.py FILE...      those files, as they are on disk
    check_generic.py --hook       a Claude Code PostToolUse hook: reads the hook's JSON on
                                  stdin, checks the file the tool wrote if it is in this
                                  repository, and exits 2 (fed back to the model) on a hit

Exits 1 and lists each hit as path:line: text.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The files allowed to name games: the README and the working notes list the game projects.
ALLOWED = {'README.md', 'CLAUDE.md', 'tools/game_terms.txt'}


def terms():
    out = []
    for line in open(os.path.join(ROOT, 'tools', 'game_terms.txt'), encoding='utf-8'):
        line = line.strip()
        if line and not line.startswith('#'):
            out.append(re.compile(line, re.IGNORECASE))
    return out


def git(*args):
    return subprocess.run(['git', '-C', ROOT] + list(args), capture_output=True, check=True).stdout


def check(rel, data, pats, hits):
    if rel.replace('\\', '/') in ALLOWED or b'\0' in data[:8000]:
        return
    text = data.decode('utf-8', errors='replace')
    for n, line in enumerate(text.splitlines(), 1):
        for p in pats:
            if p.search(line):
                hits.append(f'{rel}:{n}: {line.strip()[:120]}')
                break


def main():
    pats = terms()
    hits = []
    args = sys.argv[1:]
    hook = args == ['--hook']
    if hook:
        import json
        try:
            event = json.load(sys.stdin)
        except ValueError:
            return 0
        tool_input = event.get('tool_input') or {}
        tool_response = event.get('tool_response') or {}
        path = tool_input.get('file_path') or (tool_response.get('filePath') if isinstance(tool_response, dict) else None)
        args = [path] if path else []
        if not args:
            return 0
    if args == ['--staged']:
        names = git('diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z').split(b'\0')
        for name in filter(None, names):
            rel = name.decode()
            check(rel, git('show', ':' + rel), pats, hits)
    elif args:
        for path in args:
            full = os.path.abspath(path)
            rel = os.path.relpath(full, ROOT)
            if rel.startswith('..') or not os.path.isfile(full):
                continue   # not in this repository: not ours to judge
            check(rel, open(full, 'rb').read(), pats, hits)
    else:
        for name in filter(None, git('ls-files', '-z').split(b'\0')):
            rel = name.decode()
            full = os.path.join(ROOT, rel)
            if os.path.isfile(full):
                check(rel, open(full, 'rb').read(), pats, hits)
    if hits:
        print('gcn-recomp must not name a particular game (CLAUDE.md, tools/game_terms.txt).',
              file=sys.stderr)
        print('Make it a parameter or a hook the game project supplies, and move the', file=sys.stderr)
        print("game's specifics to that project's docs/dev/:", file=sys.stderr)
        for h in hits:
            print('  ' + h, file=sys.stderr)
        return 2 if hook else 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

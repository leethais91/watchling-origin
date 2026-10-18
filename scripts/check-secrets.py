#!/usr/bin/env python3
"""Targeted credential-pattern scan. Reports locations/rules, never matching values.

Scans tracked and non-ignored untracked text files. --history additionally checks all
reachable Git blobs (including deleted files). Ignored .env files are intentionally not
opened. This is a small project guardrail, not an exhaustive credential/security audit.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys


RULES = {
    "private key": rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----",
    "GitHub token": rb"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b",
    "AWS access key": rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b",
    "Google API key": rb"\bAIza[A-Za-z0-9_-]{35}\b",
    "Slack token": rb"\bxox[baprs]-[A-Za-z0-9-]{20,}\b",
    "OpenAI/Anthropic token": rb"\bsk-(?:proj-|ant-)?[A-Za-z0-9_-]{32,}\b",
    "credential in URL": rb"https?://[^\s/:@]+:[^\s/@]{8,}@",
    "assigned credential": rb'''(?i)(?:api[_-]?key|secret[_-]?key|access[_-]?token|password)\s*[=:]\s*["']([A-Za-z0-9_+/=-]{24,})["']''',
}
PATTERNS = {name: re.compile(pattern) for name, pattern in RULES.items()}


def git(*args):
    return subprocess.check_output(["git", *args])


def findings(data):
    if b"\x00" in data:
        return []
    found = []
    for name, pattern in PATTERNS.items():
        for match in pattern.finditer(data):
            value = match.group(1) if match.lastindex else match.group()
            # Explicit fixture placeholders are not credentials.
            if len(set(value)) < 5 or any(word in value.lower() for word in
                                         (b"example", b"placeholder", b"your_api_key")):
                continue
            found.append((name, data.count(b"\n", 0, match.start()) + 1))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true")
    args = parser.parse_args()
    root = Path(git("rev-parse", "--show-toplevel").decode().strip())
    files = git("ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0")
    count = 0
    hits = 0
    for raw_path in sorted(set(files)):
        if not raw_path:
            continue
        relative = raw_path.decode(errors="replace")
        path = root / relative
        if not path.is_file() or path.is_symlink():
            continue
        count += 1
        for rule, line in findings(path.read_bytes()):
            print(f"working tree: {relative}:{line}: {rule} (value redacted)")
            hits += 1

    blobs = 0
    if args.history:
        objects = git("rev-list", "--objects", "--all").splitlines()
        payload = b"\n".join(item.split(b" ", 1)[0] for item in objects) + b"\n"
        # Read all blobs through one process; do not print their contents.
        process = subprocess.Popen(["git", "cat-file", "--batch"], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE)
        # communicate avoids pipe deadlocks while returning the small repository's history.
        output, _ = process.communicate(payload)
        if process.returncode:
            raise RuntimeError("git cat-file failed")
        offset = 0
        for item in objects:
            header_end = output.index(b"\n", offset)
            header = output[offset:header_end].split()
            size = int(header[2])
            start = header_end + 1
            data = output[start:start + size]
            offset = start + size + 1
            if header[1] != b"blob":
                continue
            blobs += 1
            name = item.split(b" ", 1)
            label = name[1].decode(errors="replace") if len(name) == 2 else "(unnamed blob)"
            for rule, line in findings(data):
                print(f"history {header[0][:12].decode()}: {label}:{line}: {rule} (value redacted)")
                hits += 1

    print(f"Scanned {count} working-tree files and {blobs} reachable history blobs; {hits} findings.")
    print("Pattern scan only; ignored files, unreachable objects and unknown credential formats are not covered.")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())

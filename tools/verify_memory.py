"""Run the verification commands attached to the project's state claims.

    python tools/verify_memory.py            # every claim
    python tools/verify_memory.py --quiet     # failures only
    python tools/verify_memory.py --offline   # skip the ones that
                                              # reach the network

A memory file records what was true when somebody wrote it. That is its
purpose and its weakness: `CLAUDE.md` said "two tests" for a day after
there were five, and nothing about reading it would have revealed that.

So a claim that can be checked carries the check, immediately after it:

    - **v3.3.0 released** on the desktop.
      <!-- verify: grep -q 'NTRIP_VERSION_STRING  "3.3.0"' src/core/version.h -->

    - The keystore is the author's to create.
      <!-- verify: manual — keystore.properties is git-ignored -->

    - The site is live.
      <!-- verify-net: gh api repos/x/y/pages --jq .status | grep -q built -->

`verify-net:` marks a check that leaves the machine. Those run weekly,
because a runner without egress or a service having a bad afternoon
would otherwise fail a claim that is perfectly true. Everything else is
decided from the tree in milliseconds and runs on every push, so a
sentence breaks in the run that broke it.

This runs each one from the repository root and reports:

    PASS    exit 0 -- the sentence above it is still true
    FAIL    exit non-zero -- **the sentence is now false**; fix the
            sentence, not the command
    ERROR   the command could not run at all (a tool went missing, or
            the command itself has gone stale)
    MANUAL  no command can settle it; a person must look

A FAIL is the point of the exercise, so it exits non-zero -- a claim
that has quietly stopped being true is exactly what this is for.
"""
import io
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Commands are POSIX, like every other script here. On Windows cmd.exe
# would take 'a|b' and $(...) literally and report a false failure --
# which is worse than no check, because it cries wolf about a true claim.
#
# `which` can answer with a path this Python cannot exec: under Git Bash
# it returned "/c/Program Files\Git\usr\bin\bash.EXE", half POSIX and
# half Windows, and every check failed on the shell rather than on its
# claim. So take the answer only if it exists as a file.
def _shell():
    found = shutil.which("bash")
    candidates = [found] if found else []
    candidates += [r"C:\Program Files\Git\bin\bash.exe",
                   r"C:\Program Files\Git\usr\bin\bash.exe",
                   "/bin/bash"]
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return None


SHELL = _shell()

# Where claims live. The project file is included because it carries a
# few too, and a claim is a claim wherever it is written.
FILES = [
    "CLAUDE.md",
    os.path.join("memory", "MEMORY.md"),
    os.path.join("memory", "gotcha-log.md"),
]

VERIFY = re.compile(r"<!--\s*verify(-net)?:\s*(.*?)-->", re.S)

# `verify-net:` marks a check that reaches the network -- the live site,
# the wiki, a caster. Those cannot run on every push: a runner without
# egress, or a service having a bad afternoon, fails a claim that is
# perfectly true, and a checker that cries wolf gets ignored.
#
# Every other check is a grep or a test run, settled from the tree in
# milliseconds. Those *should* run on every push, and why is the whole
# point of the split: CLAUDE.md said "Seven tests" for two days after
# there were nine, because the only job that reads it runs on Mondays.
# The claim broke on a Saturday push and the alarm arrived forty hours
# later attached to a scheduled run -- by which time it named neither
# the change that caused it nor the person who could fix it in ten
# seconds.
#
# So --offline runs the deterministic ones, per push. The weekly job
# still runs everything.


def claim_above(lines, index):
    """The nearest line of prose above a comment, for naming it."""
    for i in range(index, -1, -1):
        s = lines[i].strip()
        if not s or s.startswith("<!--") or s.startswith("-->"):
            continue
        s = re.sub(r"<!--.*?-->", "", s).strip()
        s = s.lstrip("-|*# ").strip()
        if s:
            return (s[:68] + "…") if len(s) > 69 else s
    return "(unnamed claim)"


def main():
    quiet = "--quiet" in sys.argv
    offline = "--offline" in sys.argv
    counts = {"PASS": 0, "FAIL": 0, "ERROR": 0, "MANUAL": 0}
    skipped = 0
    failures = []

    for rel in FILES:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with io.open(path, encoding="utf-8") as f:
            text = f.read()

        # Claims live under headings; the preamble above the first one is
        # front matter and the block that explains this very convention,
        # examples included. An example is not a claim, and running one
        # reports a failure about nothing. Blank the preamble rather than
        # cut it, so offsets still name the right claim.
        first = re.search(r"^## ", text, re.M)
        if first:
            text = re.sub(r"[^\n]", " ", text[:first.start()]) + \
                text[first.start():]

        lines = text.split("\n")

        printed_header = False
        for m in VERIFY.finditer(text):
            # Newlines joined, inner spacing preserved: 'FOO  "3.3.0"'
            # is not the same pattern as 'FOO "3.3.0"'.
            needs_net = m.group(1) is not None
            if offline and needs_net:
                skipped += 1
                continue
            body = " ".join(part.strip()
                            for part in m.group(2).strip().splitlines())
            line_no = text[:m.start()].count("\n")
            name = claim_above(lines, line_no - 1)

            if body.lower().startswith("manual"):
                status, detail = "MANUAL", body[len("manual"):].lstrip(" —-")
            else:
                try:
                    # Explicit argv, not shell=True with executable=:
                    # on Windows that hands the shell cmd.exe's "/c",
                    # which bash reads as a path and fails on -- every
                    # check reporting a broken shell as a broken claim.
                    argv = [SHELL, "-c", body] if SHELL else body
                    r = subprocess.run(
                        argv, shell=(SHELL is None), cwd=ROOT,
                        capture_output=True, text=True, timeout=180)
                    status = "PASS" if r.returncode == 0 else "FAIL"
                    detail = (r.stderr or r.stdout).strip().split("\n")[0][:90]
                except Exception as exc:          # command itself is broken
                    status, detail = "ERROR", str(exc)[:90]

            counts[status] += 1
            if status in ("FAIL", "ERROR"):
                failures.append((rel, name, body, detail))
            if quiet and status not in ("FAIL", "ERROR"):
                continue
            if not printed_header:
                print(rel)
                printed_header = True
            print("  %-6s %s" % (status, name))
            if status in ("FAIL", "ERROR") and detail:
                print("         %s" % detail)
        if printed_header:
            print("")

    print("%d pass, %d fail, %d error, %d manual" %
          (counts["PASS"], counts["FAIL"], counts["ERROR"], counts["MANUAL"]))
    # Stated, never silent: a run that skipped half the claims and
    # printed the same closing line as one that ran them all would be a
    # green light for work it did not do.
    if skipped:
        print("%d network claim(s) not run (--offline); the weekly job "
              "runs them." % skipped)

    if failures:
        print("")
        print("These sentences are no longer true. Correct the text:")
        for rel, name, body, detail in failures:
            print("  %s -- %s" % (rel, name))
            print("      %s" % body)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

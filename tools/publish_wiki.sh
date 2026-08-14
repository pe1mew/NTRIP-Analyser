#!/usr/bin/env bash
#
# Publish docs/wiki/ to the GitHub wiki.
#
#   tools/publish_wiki.sh            # copy and show what would change
#   tools/publish_wiki.sh --push     # ...and commit and push it
#
# The pages live in this repository, under docs/wiki/, so they are
# reviewed and versioned with the code they describe. GitHub serves them
# from a *second* repository -- <repo>.wiki.git -- which is why they have
# to be copied at all.
#
# The app links into the wiki: About -> Documentation, and the orbit
# badge on the Analysis screen. Until the pages are published, those
# links redirect to the repository front page, which is why this is a
# release step and not an optional nicety.
#
# **The wiki repository does not exist until the first page is saved in
# the browser.** Enabling the wiki in Settings is not enough; GitHub
# creates the repository lazily. If the clone below fails with
# "Repository not found", visit
#
#     https://github.com/pe1mew/NTRIP-Analyser/wiki
#
# click "Create the first page", save anything at all, and run this
# again -- it will overwrite that placeholder.
set -euo pipefail

REPO="${WIKI_REPO:-https://github.com/pe1mew/NTRIP-Analyser.wiki.git}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/docs/wiki"
WORK="${TMPDIR:-/tmp}/ntrip-wiki"

[ -d "$SRC" ] || { echo "no $SRC"; exit 1; }

if [ -d "$WORK/.git" ]; then
    git -C "$WORK" fetch --quiet origin
    git -C "$WORK" reset --quiet --hard origin/HEAD
else
    rm -rf "$WORK"
    git clone --quiet "$REPO" "$WORK" || {
        echo
        echo "Could not clone $REPO"
        echo "If this says 'Repository not found', the wiki has never had"
        echo "a page. Create one in the browser first -- see the comment"
        echo "at the top of this script."
        exit 1
    }
fi

# Copied, not synced: a page removed here stays on the wiki until it is
# deleted there. Deleting somebody's page from a script is not a thing a
# publish step should do quietly.
cp "$SRC"/*.md "$WORK/"

cd "$WORK"
git add -A
if git diff --cached --quiet; then
    echo "wiki is already up to date"
    exit 0
fi

echo
git diff --cached --stat
echo

if [ "${1:-}" = "--push" ]; then
    git commit -m "Update wiki from docs/wiki/"
    git push origin HEAD
    echo "published"
else
    echo "Nothing was committed. Re-run with --push to publish, or run"
    echo "these two yourself:"
    echo
    echo "  git -C \"$WORK\" commit -m 'Update wiki from docs/wiki/'"
    echo "  git -C \"$WORK\" push origin HEAD"
fi

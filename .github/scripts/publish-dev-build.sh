#!/usr/bin/env bash
#
# Attach a built binary to the rolling "dev-build" prerelease.
#
# The Windows and the MacOS workflow both call this for the same commit, each
# with its own asset, so everything here tolerates the other one getting there
# first: the tag is force-moved, the release is created only when missing, and
# assets are uploaded with --clobber.
#
# Usage: publish-dev-build.sh <asset> [<asset>...]
# Needs: GH_TOKEN with contents:write, GITHUB_REPOSITORY, GITHUB_SHA.

set -euo pipefail

if [ $# -eq 0 ]; then
    echo "usage: $0 <asset> [<asset>...]" >&2
    exit 1
fi

TAG="dev-build"
REPO="$GITHUB_REPOSITORY"

# Point the rolling tag at this commit, creating it on the very first run.
if ! gh api -X PATCH "repos/$REPO/git/refs/tags/$TAG" -f "sha=$GITHUB_SHA" -F force=true >/dev/null 2>&1; then
    gh api -X POST "repos/$REPO/git/refs" -f "ref=refs/tags/$TAG" -f "sha=$GITHUB_SHA" >/dev/null
fi

NOTES="Automatic build of the latest commit on dev (${GITHUB_SHA:0:7}).

Not a stable release. Windows is x64, MacOS is Apple Silicon (arm64) only.
Both are unsigned, so the system will warn about an unidentified developer."

# The release is shared between the two workflows, so only create it once.
if gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
    gh release edit "$TAG" --repo "$REPO" --notes "$NOTES" >/dev/null
else
    gh release create "$TAG" --repo "$REPO" --prerelease --title "Development build" --notes "$NOTES" >/dev/null
fi

for asset in "$@"; do
    echo "Uploading $asset"
    gh release upload "$TAG" "$asset" --repo "$REPO" --clobber
done

echo "Published to https://github.com/$REPO/releases/tag/$TAG"

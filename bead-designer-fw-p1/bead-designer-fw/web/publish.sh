#!/usr/bin/env bash
# Publish the QR export page (web/index.html + generated web/names.js) to the GitHub
# Pages repo that CONFIG_BEAD_EXPORT_URL points at.
#
#   web/publish.sh                 # default repo git@github.com:TeddyGavi/pixel-painter-export.git
#   BEAD_EXPORT_REPO=<git url> web/publish.sh
#
# One-time setup: create the public repo on GitHub, then Settings > Pages > Deploy from
# branch "main", folder "/ (root)". Run palettes/gen_palettes.py first if a TSV changed.
set -euo pipefail
WEB="$(cd "$(dirname "$0")" && pwd)"
REPO="${BEAD_EXPORT_REPO:-git@github.com:TeddyGavi/pixel-painter-export.git}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

git clone -q "$REPO" "$TMP/site" 2>/dev/null || { mkdir -p "$TMP/site"; git -C "$TMP/site" init -q -b main; git -C "$TMP/site" remote add origin "$REPO"; }
cp "$WEB/index.html" "$WEB/names.js" "$TMP/site/"
touch "$TMP/site/.nojekyll"                          # serve files as-is
cd "$TMP/site"
git add -A
if git diff --cached --quiet; then echo "export page already up to date"; exit 0; fi
git commit -q -m "Update export page from pixel-painter $(git -C "$WEB" rev-parse --short HEAD)"
git push -q -u origin main
echo "published to $REPO"

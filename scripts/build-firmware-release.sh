#!/usr/bin/env bash
# Build and package a tagged Watchling firmware release bundle locally on macOS.
#
# Usage:
#   ./scripts/build-firmware-release.sh [vX.Y.Z] [options]
#
# Options:
#   --output-dir <dir>   Output directory (default: release-output)
#   --allow-dirty        Allow uncommitted changes or HEAD mismatching an existing tag
#   --clean              Run `idf.py fullclean` before building firmware
#   --skip-tests         Skip web-installer and packager unit tests
#   --upload-draft       Create or update a GitHub draft release via `gh`
#   -h, --help           Show this help text

set -euo pipefail

usage() {
  sed -n '2,13s/^# \{0,1\}//p' "$0"
}

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT_DIR"

VERSION=""
OUTPUT_DIR="release-output"
ALLOW_DIRTY=0
CLEAN_BUILD=0
SKIP_TESTS=0
UPLOAD_DRAFT=0

while [ $# -gt 0 ]; do
  case "$1" in
    --output-dir)
      if [ $# -lt 2 ]; then
        printf '%s\n' "Missing argument for --output-dir" >&2
        exit 1
      fi
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --allow-dirty)
      ALLOW_DIRTY=1
      shift
      ;;
    --clean)
      CLEAN_BUILD=1
      shift
      ;;
    --skip-tests)
      SKIP_TESTS=1
      shift
      ;;
    --upload-draft)
      UPLOAD_DRAFT=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      printf '%s\n' "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      if [ -n "$VERSION" ]; then
        printf '%s\n' "Unexpected extra argument: $1" >&2
        usage >&2
        exit 1
      fi
      VERSION="$1"
      shift
      ;;
  esac
done

if [ -z "$VERSION" ]; then
  if ! VERSION=$(git describe --tags --exact-match HEAD 2>/dev/null); then
    printf '%s\n' "No version argument given and HEAD does not have an exact Git tag." >&2
    printf '%s\n' "Pass a tag explicitly, e.g.: ./scripts/build-firmware-release.sh v0.1.0" >&2
    exit 1
  fi
fi

if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.-]+)?$ ]]; then
  printf '%s\n' "Version must be a semantic tag such as v0.1.0 (got: $VERSION)" >&2
  exit 1
fi

if [ "$ALLOW_DIRTY" -eq 0 ]; then
  if [ -n "$(git status --porcelain)" ]; then
    printf '%s\n' "Working tree is not clean. Commit or stash changes, or pass --allow-dirty." >&2
    exit 1
  fi
  if git rev-parse -q --verify "refs/tags/$VERSION" >/dev/null 2>&1; then
    TAG_COMMIT=$(git rev-parse "refs/tags/$VERSION^{commit}")
    HEAD_COMMIT=$(git rev-parse HEAD)
    if [ "$TAG_COMMIT" != "$HEAD_COMMIT" ]; then
      printf '%s\n' "Tag $VERSION points to $TAG_COMMIT, but HEAD is $HEAD_COMMIT. Checkout $VERSION or pass --allow-dirty." >&2
      exit 1
    fi
  fi
fi

COMMIT=$(git rev-parse HEAD)

if ! command -v node >/dev/null 2>&1; then
  printf '%s\n' "Node.js 22 or newer is required." >&2
  exit 1
fi
NODE_MAJOR=$(node -p 'process.versions.node.split(".")[0]')
if [ "$NODE_MAJOR" -lt 22 ]; then
  printf '%s\n' "Node.js 22 or newer is required (found $(node -v))." >&2
  exit 1
fi

printf '==> [1/5] Preparing offline web installer (%s)\n' "$VERSION"
if [ ! -d "web-installer/node_modules" ]; then
  npm --prefix web-installer ci
fi
if [ "$SKIP_TESTS" -eq 0 ]; then
  npm --prefix web-installer test
fi
npm --prefix web-installer run build

if [ "$SKIP_TESTS" -eq 0 ]; then
  printf '==> [2/5] Running release packager tests\n'
  python3 -m unittest discover -s scripts/tests -p 'test_*.py'
fi

printf '==> [3/5] Loading ESP-IDF environment\n'
# shellcheck source=scripts/idf-env.sh
. scripts/idf-env.sh

if ! python3 -c 'import importlib.metadata as m, sys; sys.exit(0 if m.version("idf-component-manager") == "2.5.2" else 1)' 2>/dev/null; then
  printf 'Installing idf-component-manager==2.5.2 into active ESP-IDF Python environment...\n'
  python3 -m pip install 'idf-component-manager==2.5.2'
fi

printf '==> [4/5] Building firmware for %s (%s)\n' "$VERSION" "$COMMIT"
if [ "$CLEAN_BUILD" -eq 1 ]; then
  idf.py -C echoear-firmware fullclean
elif [ -f "echoear-firmware/build/project_description.json" ]; then
  BUILT_VER=$(python3 -c 'import json; print(json.load(open("echoear-firmware/build/project_description.json")).get("project_version", ""))' 2>/dev/null || true)
  if [ "$BUILT_VER" != "$VERSION" ]; then
    idf.py -C echoear-firmware -DPROJECT_VER="$VERSION" reconfigure
  fi
fi
idf.py -C echoear-firmware -DPROJECT_VER="$VERSION" build

printf '==> [5/5] Packaging release bundle into %s\n' "$OUTPUT_DIR"
BUNDLE_NAME="watchling-${VERSION}-esp-vocat-v1.2"
rm -rf "${OUTPUT_DIR:?}/${BUNDLE_NAME}" "${OUTPUT_DIR:?}/${BUNDLE_NAME}.zip" "${OUTPUT_DIR:?}/${BUNDLE_NAME}.zip.sha256"

python3 scripts/package-firmware-release.py \
  --build-dir echoear-firmware/build \
  --web-dir web-installer/dist \
  --output-dir "$OUTPUT_DIR" \
  --source-root "$ROOT_DIR" \
  --idf-path "$IDF_PATH" \
  --version "$VERSION" \
  --commit "$COMMIT"

ARCHIVE="${OUTPUT_DIR}/${BUNDLE_NAME}.zip"
CHECKSUM_FILE="${ARCHIVE}.sha256"
(
  cd "$OUTPUT_DIR"
  shasum -a 256 -c "${BUNDLE_NAME}.zip.sha256"
)

if [ "$UPLOAD_DRAFT" -eq 1 ]; then
  if ! command -v gh >/dev/null 2>&1; then
    printf '%s\n' "GitHub CLI (gh) is required for --upload-draft." >&2
    exit 1
  fi
  printf '==> Uploading artifacts to GitHub draft release %s\n' "$VERSION"
  if DEMAND_DRAFT=$(gh release view "$VERSION" --json isDraft --jq .isDraft 2>/dev/null); then
    if [ "$DEMAND_DRAFT" != "true" ]; then
      printf '%s\n' "Release $VERSION is already published; refusing to overwrite." >&2
      exit 1
    fi
    gh release upload "$VERSION" "$ARCHIVE" "$CHECKSUM_FILE" --clobber
  else
    gh release create "$VERSION" --draft \
      --title "Watchling $VERSION · ESP-VoCat V1.2" \
      --notes-file docs/firmware-release-notes.txt \
      "$ARCHIVE" "$CHECKSUM_FILE"
  fi
fi

printf '\nRelease build complete:\n'
printf '  Bundle:   %s/%s/\n' "$OUTPUT_DIR" "$BUNDLE_NAME"
printf '  Archive:  %s\n' "$ARCHIVE"
printf '  SHA-256:  %s\n' "$CHECKSUM_FILE"
if [ "$UPLOAD_DRAFT" -eq 0 ]; then
  printf '  Upload:   ./scripts/build-firmware-release.sh %s --upload-draft\n' "$VERSION"
fi

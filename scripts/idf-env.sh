# Source this file from bash or zsh: `. scripts/idf-env.sh`
# Install ESP-IDF v5.5 first; its export script selects the managed Python env.
# Set IDF_PATH before sourcing to use a non-default installation.
#
# ESP-IDF v5.5 supports Python 3.9-3.13 and builds its Python env with whatever `python3`
# comes first on PATH, so a newer system Python (Homebrew's 3.14+) breaks it. This pins the
# interpreter to a uv-managed Python, WATCHLING_IDF_PYTHON (default 3.12); system Pythons
# are never used. Its own bin directory (uv keeps only that Python's tools there) goes
# first on PATH; a symlink elsewhere would not do, as Python finds its stdlib from there.
if [ -z "${IDF_PATH:-}" ]; then
  export IDF_PATH="$HOME/esp/esp-idf"
fi
if [ ! -f "$IDF_PATH/export.sh" ]; then
  printf '%s\n' "ESP-IDF export.sh not found at $IDF_PATH/export.sh. Install ESP-IDF v5.5 or set IDF_PATH." >&2
  return 1 2>/dev/null || exit 1
fi

watchling_py_version="${WATCHLING_IDF_PYTHON:-3.12}"
if ! command -v uv >/dev/null 2>&1; then
  printf '%s\n' "uv not found. Install it (https://docs.astral.sh/uv/getting-started/installation/), then \`uv python install $watchling_py_version\`." >&2
  unset watchling_py_version
  return 1 2>/dev/null || exit 1
fi
watchling_py=$(uv python find --managed-python "$watchling_py_version" 2>/dev/null)
if [ -z "$watchling_py" ]; then
  printf '%s\n' "No uv-managed Python $watchling_py_version. Run \`uv python install $watchling_py_version\` or set WATCHLING_IDF_PYTHON." >&2
  unset watchling_py watchling_py_version
  return 1 2>/dev/null || exit 1
fi
watchling_py_bin=$("$watchling_py" -c 'import os, sys; print(os.path.dirname(os.path.realpath(sys.executable)))')
export PATH="$watchling_py_bin:$PATH"
unset watchling_py watchling_py_version watchling_py_bin

. "$IDF_PATH/export.sh"

# Source this file from bash or zsh: `. scripts/idf-env.sh`
# Install ESP-IDF v5.5 first; its export script selects the managed Python env.
# Set IDF_PATH before sourcing to use a non-default installation.
#
# ESP-IDF v5.5 supports Python 3.9-3.13 and builds its Python env with whatever `python3`
# comes first on PATH, so a newer system Python (Homebrew's 3.14+) breaks it. This pins the
# interpreter: WATCHLING_IDF_PYTHON (default 3.12), found with uv when available
# (`uv python install 3.12` gets one), else `python3.12` on PATH. It runs through a
# one-file shim directory, so nothing else from that interpreter's bin lands on PATH.
if [ -z "${IDF_PATH:-}" ]; then
  export IDF_PATH="$HOME/esp/esp-idf"
fi
if [ ! -f "$IDF_PATH/export.sh" ]; then
  printf '%s\n' "ESP-IDF export.sh not found at $IDF_PATH/export.sh. Install ESP-IDF v5.5 or set IDF_PATH." >&2
  return 1 2>/dev/null || exit 1
fi

watchling_py_version="${WATCHLING_IDF_PYTHON:-3.12}"
watchling_py=""
if command -v uv >/dev/null 2>&1; then
  watchling_py=$(uv python find "$watchling_py_version" 2>/dev/null)
fi
if [ -z "$watchling_py" ]; then
  watchling_py=$(command -v "python$watchling_py_version" 2>/dev/null)
fi
if [ -z "$watchling_py" ]; then
  printf '%s\n' "Python $watchling_py_version not found. Install it (\`uv python install $watchling_py_version\`) or set WATCHLING_IDF_PYTHON." >&2
  unset watchling_py watchling_py_version
  return 1 2>/dev/null || exit 1
fi
watchling_py_shim="${XDG_CACHE_HOME:-$HOME/.cache}/watchling/idf-python-$watchling_py_version"
mkdir -p "$watchling_py_shim"
ln -sf "$watchling_py" "$watchling_py_shim/python3"
export PATH="$watchling_py_shim:$PATH"
unset watchling_py watchling_py_version watchling_py_shim

. "$IDF_PATH/export.sh"

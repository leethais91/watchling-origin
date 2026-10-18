# Source this file from bash or zsh: `. scripts/idf-env.sh`
# Install ESP-IDF v5.5 first; its export script selects the managed Python env.
# Set IDF_PATH before sourcing to use a non-default installation.
if [ -z "${IDF_PATH:-}" ]; then
  export IDF_PATH="$HOME/esp/esp-idf"
fi
if [ ! -f "$IDF_PATH/export.sh" ]; then
  printf '%s\n' "ESP-IDF export.sh not found at $IDF_PATH/export.sh. Install ESP-IDF v5.5 or set IDF_PATH." >&2
  return 1 2>/dev/null || exit 1
fi
. "$IDF_PATH/export.sh"

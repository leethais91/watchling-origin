# Source this file (do not execute): `. scripts/idf-env.sh`
# Loads ESP-IDF v5.5 with its Python 3.12 venv. The system python3 (3.15) is too new
# for ESP-IDF v5.5, so export.sh must not pick it up.
export PATH="/opt/homebrew/opt/python@3.12/libexec/bin:$PATH"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.12_env"
. "$HOME/esp/esp-idf/export.sh"

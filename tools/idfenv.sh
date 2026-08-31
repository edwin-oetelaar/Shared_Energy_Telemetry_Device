#  Set up an ESP-IDF build environment from the ESP-IDF that PlatformIO
#  installed. Source this file, do not execute it:
#
#      . tools/idfenv.sh
#
#  WARNING: this exports IDF_PATH, IDF_PYTHON_ENV_PATH, PYTHONPATH and a few
#  more into your shell. A real ESP-IDF export.sh sourced in that same shell
#  then picks up the wrong Python and fails with a message about
#  espidf.constraints. Run `idfenv_unset` first, or just open a new shell.
#
#  This is the fallback route. The project targets ESP-IDF v6.1, and PlatformIO
#  ships 5.5.x - so a build made this way does not prove the firmware works on
#  the version that ships. Install ESP-IDF v6.1 the normal way and use its own
#  export.sh; idf.py then works in full, monitor included.
#
#  Why this file exists at all: PlatformIO does not use idf.py, so the Python
#  environment it ships is incomplete for it - esp_idf_monitor, pyyaml and
#  esptool are missing. Driving CMake and ninja directly works around that.
#
#  Override any of these before sourcing if your paths differ.

#  Remember what was there, so idfenv_unset can put it back.
IDFENV_SAVED_IDF_PATH="${IDF_PATH-}"
IDFENV_SAVED_IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH-}"
IDFENV_SAVED_PYTHONPATH="${PYTHONPATH-}"
IDFENV_SAVED_PATH="$PATH"
export IDFENV_SAVED_IDF_PATH IDFENV_SAVED_IDF_PYTHON_ENV_PATH
export IDFENV_SAVED_PYTHONPATH IDFENV_SAVED_PATH

#  Undo everything this script exported. Sourced scripts cannot clean up after
#  themselves, so this has to be a function you call.
idfenv_unset() {
    PATH="$IDFENV_SAVED_PATH"
    export PATH
    unset IDF_MAINTAINER ESP_ROM_ELF_DIR

    for name in IDF_PATH IDF_PYTHON_ENV_PATH PYTHONPATH; do
        eval "saved=\$IDFENV_SAVED_$name"
        if [ -n "$saved" ]; then
            eval "export $name=\$saved"
        else
            unset "$name"
        fi
    done

    unset IDFENV_SAVED_IDF_PATH IDFENV_SAVED_IDF_PYTHON_ENV_PATH
    unset IDFENV_SAVED_PYTHONPATH IDFENV_SAVED_PATH saved
    echo "idfenv: environment restored"
}

: "${PIO_HOME:=$HOME/.platformio}"
: "${IDF_PATH:=$PIO_HOME/packages/framework-espidf}"
: "${IDF_PYTHON_ENV_PATH:=$PIO_HOME/penv/.espidf-5.5.5}"
: "${PIO_PYTHON_SITE:=$PIO_HOME/penv/lib/python3.12/site-packages}"

export IDF_PATH IDF_PYTHON_ENV_PATH

for path in "$IDF_PATH" "$IDF_PYTHON_ENV_PATH" "$PIO_PYTHON_SITE"; do
    if [ ! -d "$path" ]; then
        echo "idfenv: not found: $path" >&2
        echo "idfenv: set PIO_HOME, IDF_PATH, IDF_PYTHON_ENV_PATH or PIO_PYTHON_SITE" >&2
        return 1 2>/dev/null || exit 1
    fi
done

export PATH="$IDF_PYTHON_ENV_PATH/bin:\
$PIO_HOME/packages/toolchain-xtensa-esp-elf/bin:\
$PIO_HOME/packages/tool-cmake/bin:\
$PIO_HOME/packages/tool-ninja:\
$IDF_PATH/tools:$PATH"

#  The IDF venv has no pyyaml and no esptool; PlatformIO's own venv has both.
export PYTHONPATH="$PIO_HOME/packages/tool-esptoolpy:$PIO_PYTHON_SITE:$PYTHONPATH"

#  Espressif ships a newer toolchain than this IDF release lists as supported.
#  Without this the configure step stops with a version error.
export IDF_MAINTAINER=1

export ESP_ROM_ELF_DIR="$PIO_HOME/packages/tool-esp-rom-elfs"

idf_version=$(cat "$IDF_PATH/version.txt" 2>/dev/null)
echo "idfenv: ESP-IDF $idf_version from $IDF_PATH"
case "$idf_version" in
    v6.1*|6.1*) ;;
    *) echo "idfenv: WARNING - this project targets ESP-IDF v6.1, see README.md" >&2 ;;
esac
echo "idfenv: configure with  cmake -S . -B build -G Ninja -DPYTHON=\"\$IDF_PYTHON_ENV_PATH/bin/python\" -DPYTHON_DEPS_CHECKED=1"
echo "idfenv: build with       ninja -C build"
echo "idfenv: flash with       ESPPORT=/dev/ttyACM0 ninja -C build flash"
echo "idfenv: watch with       python tools/monitor.py /dev/ttyACM0"
echo "idfenv: undo with        idfenv_unset   (needed before a real export.sh)"

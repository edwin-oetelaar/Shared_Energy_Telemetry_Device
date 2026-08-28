#  Set up an ESP-IDF build environment from the ESP-IDF that PlatformIO
#  installed. Source this file, do not execute it:
#
#      . tools/idfenv.sh
#
#  Why this exists: PlatformIO does not use idf.py, so the Python environment
#  it ships is incomplete for it - esp_idf_monitor, pyyaml and esptool are
#  missing. Driving CMake and ninja directly works around that. If you install
#  ESP-IDF the normal way (git clone + install.sh), use its own export.sh
#  instead and ignore this file; idf.py then works in full.
#
#  Override any of these before sourcing if your paths differ.

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

echo "idfenv: ESP-IDF $(cat "$IDF_PATH/version.txt" 2>/dev/null) from $IDF_PATH"
echo "idfenv: configure with  cmake -S . -B build -G Ninja -DPYTHON=\"\$IDF_PYTHON_ENV_PATH/bin/python\" -DPYTHON_DEPS_CHECKED=1"
echo "idfenv: build with       ninja -C build"
echo "idfenv: flash with       ESPPORT=/dev/ttyACM0 ninja -C build flash"
echo "idfenv: watch with       python tools/monitor.py /dev/ttyACM0"

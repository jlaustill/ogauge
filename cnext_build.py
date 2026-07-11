Import("env")
import os
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# C-Next transpile hook (runs as a `pre:` extra script).
#
# The transpile MUST run at `pre:`: it GENERATES the .cpp files PlatformIO then
# compiles. At `post:` the generated sources land after source enumeration and
# would only be built on the next run.
#
# cnext resolves the framework/library headers (FreeRTOS, ESP-IDF twai_*, Arduino
# ledc*, lvgl) by reading the project's compile_commands.json — the same build-
# system-agnostic contract clangd reads — for the exact include paths, defines,
# and target compiler this build uses. That is the single source of truth, which
# is why cnext.config.json no longer mirrors framework include paths and this
# script no longer scrapes CPPPATH / CPPDEFINES or sets CNEXT_CROSS_COMPILER.
# ---------------------------------------------------------------------------

PIO_ENV = env["PIOENV"]


def _child_env():
    """Env for the cnext subprocess.

    Prepend the build's PATH so cnext can resolve the target cross-compiler named
    in compile_commands.json (e.g. xtensa-esp32s3-elf-g++), which is not on the
    default PATH. cnext reads the compiler *name* from the database itself.
    """
    child = dict(os.environ)
    pio_path = env["ENV"].get("PATH")
    if pio_path:
        child["PATH"] = pio_path + os.pathsep + child.get("PATH", "")
    return child


def _ensure_compile_commands(child_env):
    """Make sure compile_commands.json exists for cnext to read.

    On a fresh checkout it may be absent; regenerate it from the committed
    generated .cpp via a nested `compiledb` run. CNEXT_SKIP_TRANSPILE breaks the
    recursion — this script no-ops inside that nested run.
    """
    if Path("compile_commands.json").exists():
        return
    if os.environ.get("CNEXT_SKIP_TRANSPILE"):
        return
    print("compile_commands.json missing — generating via `pio run -t compiledb`...")
    gen_env = dict(child_env)
    gen_env["CNEXT_SKIP_TRANSPILE"] = "1"
    subprocess.run(
        ["pio", "run", "-e", PIO_ENV, "-t", "compiledb"],
        env=gen_env,
        check=False,
    )


def transpile_cnext():
    """Transpile from the main.cnx entry point — cnext follows its includes."""
    if os.environ.get("CNEXT_SKIP_TRANSPILE"):
        return  # nested compiledb run — leave the committed .cpp untouched
    if not Path("src/main.cnx").exists():
        return

    child_env = _child_env()
    _ensure_compile_commands(child_env)

    print("Transpiling from main.cnx...")
    try:
        result = subprocess.run(
            ["cnext", "src/main.cnx"],
            check=True,
            capture_output=True,
            text=True,
            env=child_env,
        )
        print(result.stdout.strip())
    except subprocess.CalledProcessError as e:
        print("  ✗ Transpilation failed")
        print(e.stdout)
        print(e.stderr)
        sys.exit(1)


transpile_cnext()

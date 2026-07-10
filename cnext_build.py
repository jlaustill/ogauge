Import("env")
import os
import shutil
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# C-Next transpile hook (runs as a `post:` extra script).
#
# Since c-next v0.2.17 (Issue #985) an undeclared call to a framework function
# (FreeRTOS vTaskDelay, ESP-IDF twai_*, Arduino ledc*) is a hard error (E0422)
# unless cnext can see its declaration. To resolve those, cnext must preprocess
# the framework headers with the SAME include set and target compiler the real
# build uses.
#
# This is registered as `post:` (not `pre:`) precisely so PlatformIO's Arduino
# builder has already populated env["CPPPATH"]/CPPDEFINES and set $CC to the
# xtensa cross-compiler by the time this runs — yet it still executes during
# configuration, before SCons compiles the generated .cpp. We hand cnext the
# authoritative include list from the env (no fragile hand-derived globs) and
# point it at the cross-compiler via CNEXT_CROSS_COMPILER.
# ---------------------------------------------------------------------------

def _cnext_command():
    argv = ["cnext", "src/main.cnx", "-D", "LV_CONF_INCLUDE_SIMPLE"]

    # Preprocessor defines from the real build (matches the compiler's view).
    # subst() resolves SCons variables like $BOARD_F_CPU to their real values.
    for define in env.get("CPPDEFINES", []):
        if isinstance(define, (list, tuple)):
            name = env.subst(str(define[0]))
            has_value = len(define) > 1 and define[1] is not None
            value = env.subst(str(define[1])) if has_value else None
            argv += ["-D", f"{name}={value}" if value is not None else name]
        else:
            argv += ["-D", env.subst(str(define))]

    # Authoritative include directories, resolved to absolute paths.
    includes = []
    for item in env.get("CPPPATH", []):
        directory = env.subst(item if isinstance(item, str) else str(item))
        if directory:
            includes.append(directory)

    # The library-deps root holds the user's lv_conf.h, which lvgl.h resolves via
    # `#include "lv_conf.h"` under LV_CONF_INCLUDE_SIMPLE. It is not always in
    # CPPPATH, and without it lvgl falls back to probing pthread/cmsis OSAL
    # headers that don't exist for this target — so lvgl fails to preprocess and
    # lv_obj_t never resolves. Add it (and the lvgl root) explicitly.
    project_dir = env.subst("$PROJECT_DIR")
    libdeps = os.path.join(project_dir, ".pio", "libdeps", env["PIOENV"])
    for extra in (libdeps, os.path.join(libdeps, "lvgl")):
        if os.path.isdir(extra) and extra not in includes:
            includes.append(extra)

    for directory in includes:
        argv += ["--include", directory]

    return argv


def _cross_compiler_path():
    """Full path to the target C compiler PlatformIO uses ($CC on PATH)."""
    cc = env.subst("$CC")
    return shutil.which(cc, path=env["ENV"].get("PATH", os.environ.get("PATH", ""))) or cc


def transpile_cnext():
    """Transpile from main.cnx entry point — cnext follows includes"""
    entry = Path("src/main.cnx")
    if not entry.exists():
        return

    print("Transpiling from main.cnx...")

    child_env = dict(os.environ)
    child_env["CNEXT_CROSS_COMPILER"] = _cross_compiler_path()

    try:
        result = subprocess.run(
            _cnext_command(),
            check=True,
            capture_output=True,
            text=True,
            env=child_env,
        )
        print(result.stdout.strip())
    except subprocess.CalledProcessError as e:
        print(f"  ✗ Transpilation failed")
        print(e.stdout)
        print(e.stderr)
        sys.exit(1)

# Run transpilation while the env is fully configured but before compilation.
#
# (Bug #982 note: the old fix_array_param_const() hack that force-added `const`
# to array params in generated .hpp files was removed. cnext now emits array-
# param signatures consistently between .cpp and .hpp, so forcing const on only
# the header side created a linker mismatch. If cnext later restores `const` on
# immutable array params in BOTH, no downstream fix-up is needed.)
transpile_cnext()

Import("env")
import re
import subprocess
import sys
from pathlib import Path

def transpile_cnext():
    """Transpile from main.cnx entry point — cnext follows includes"""
    entry = Path("src/main.cnx")
    if not entry.exists():
        return

    print("Transpiling from main.cnx...")

    try:
        result = subprocess.run(
            ["cnext", str(entry), "-D", "LV_CONF_INCLUDE_SIMPLE"],
            check=True,
            capture_output=True,
            text=True
        )
        print(result.stdout.strip())
    except subprocess.CalledProcessError as e:
        print(f"  ✗ Transpilation failed")
        print(e.stderr)
        sys.exit(1)

# Bug #982: transitive includes drop `const` from array params in .hpp
# declarations, causing a C++ linker mismatch vs the .cpp definition.
# C-Next array params are always immutable, so const is correct in prototypes.
# Only applied to project-generated headers (not lib deps).
_PROTO_ARRAY_PARAM_RE = re.compile(
    r'^((?:void|u?int(?:8|16|32|64)_t|float|double|bool)\s+\w+\([^)]*?)'
    r'(?<!const )\b(u?int(?:8|16|32|64)_t)\s+(\w+\[\d+\])',
    re.MULTILINE
)

def _fix_line(m):
    return m.group(1) + 'const ' + m.group(2) + ' ' + m.group(3)

def fix_array_param_const(header_dir: Path):
    for hpp in header_dir.rglob("*.hpp"):
        if '.pio' in hpp.parts:
            continue
        text = hpp.read_text()
        fixed = _PROTO_ARRAY_PARAM_RE.sub(_fix_line, text)
        if fixed != text:
            hpp.write_text(fixed)

# Run transpilation at import time (before compilation starts)
transpile_cnext()
fix_array_param_const(Path("include"))

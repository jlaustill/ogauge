Import("env")
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

# Run transpilation at import time (before compilation starts)
transpile_cnext()

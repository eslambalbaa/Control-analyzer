from __future__ import annotations

from wsgiref.simple_server import make_server
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory

ROOT = Path(__file__).resolve().parent
FRONTEND_DIR = ROOT / "frontend"
EXE_NAME = "control_backend.exe" if os.name == "nt" else "control_backend"
BACKEND_EXE = ROOT / "build" / EXE_NAME

app = Flask(__name__, static_folder=str(FRONTEND_DIR), static_url_path="")


def find_make() -> str:
    for candidate in ("make", "mingw32-make"):
        found = shutil.which(candidate)
        if found:
            return found
    raise RuntimeError("Could not find 'make' or 'mingw32-make' in PATH.")


def ensure_backend() -> Path:
    if BACKEND_EXE.is_file():
        return BACKEND_EXE

    print(f"[server] Building missing backend at {BACKEND_EXE.relative_to(ROOT)}...")
    subprocess.run([find_make()], cwd=ROOT, check=True)

    if not BACKEND_EXE.is_file():
        raise FileNotFoundError(f"Build finished, but {BACKEND_EXE.relative_to(ROOT)} was not created.")
    return BACKEND_EXE


def run_backend(payload: str) -> dict:
    executable = ensure_backend()
    completed = subprocess.run(
        [str(executable)],
        input=payload,
        text=True,
        capture_output=True,
        encoding="utf-8",
        cwd=ROOT,
    )

    stdout = completed.stdout.strip()
    if not stdout:
        detail = completed.stderr.strip() or f"Backend exited with code {completed.returncode}."
        return {"ok": False, "error": detail}
    return json.loads(stdout)


@app.get("/")
def index():
    return send_from_directory(FRONTEND_DIR, "index.html")


@app.post("/api/sfg")
def api_sfg():
    data = request.get_json(silent=True) or {}
    try:
        nodes = int(data["nodes"])
        source = int(data["source"])
        sink = int(data["sink"])
        branches = data["branches"]
    except (KeyError, TypeError, ValueError):
        return jsonify({"ok": False, "error": "Invalid Signal Flow Graph payload."}), 400

    # Each branch must stay on a single line so the C++ line parser reads fields cleanly.
    lines = ["sfg", f"{nodes} {source} {sink} {len(branches)}"]
    for branch in branches:
        try:
            from_node = int(branch["from"])
            to_node = int(branch["to"])
            gain = str(branch.get("gain", "")).replace("\r", " ").replace("\n", " ")
        except (KeyError, TypeError, ValueError):
            return jsonify({"ok": False, "error": "Invalid branch entry."}), 400
        lines.append(f"{from_node} {to_node} {gain}")

    return jsonify(run_backend("\n".join(lines) + "\n"))


@app.post("/api/routh")
def api_routh():
    data = request.get_json(silent=True) or {}
    equation = str(data.get("equation", "")).replace("\r", " ").replace("\n", " ")
    return jsonify(run_backend(f"routh\n{equation}\n"))


if __name__ == "__main__":
    try:
        executable = ensure_backend()
        startup_check = run_backend("routh\ns + 1 = 0\n")
        if not startup_check.get("ok"):
            raise RuntimeError(startup_check.get("error", "Backend startup check failed."))
        print(f"[server] C++ backend ready: {executable.relative_to(ROOT)}")
    except Exception as error:
        print(f"[server] Startup error: {error}", file=sys.stderr)
        sys.exit(1)

    print("[server] Running on http://127.0.0.1:5000 (Press CTRL+C to quit)")
    with make_server("127.0.0.1", 5000, app) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass

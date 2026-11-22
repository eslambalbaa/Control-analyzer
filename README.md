# Control Toolkit

A web application for Signal Flow Graphs and the Routh-Hurwitz stability criterion. The math runs in a C++17 backend, served to an HTML/CSS/JavaScript frontend through a Python Flask server.

## Project Structure

- `src/signal_flow_graph.hpp`: Signal Flow Graph engine implementing forward paths, loops, determinants, cofactors, and Mason's gain formula.
- `src/routh_table.hpp`: Routh-Hurwitz stability engine implementing polynomial parsing, Routh table construction, sign-change counting, and numerical pole verification.
- `src/main.cpp`: C++17 entry point that reads requests from `stdin` and writes JSON to `stdout`.
- `frontend/`: Browser UI built with HTML, CSS, and JavaScript.
- `server.py`: Flask bridge that compiles the backend with `make` if missing, verifies the executable, and serves the app.
- `Makefile`: Cross-platform build rules for the C++ backend.

## Prerequisites

- A C++17 compiler (`g++` or `clang++`) and `make` (or `mingw32-make` on Windows)
- Python 3 with Flask installed (`pip install flask`)

## How to Run

```bash
python server.py

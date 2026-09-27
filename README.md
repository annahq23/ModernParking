# Modern Parking System (C++ backend + web frontend)

A local demonstration application for the DSA Task One. The browser interface talks to a C++ HTTP server; SQLite stores slots, active/completed visits, and payment records.

## Features
- 10 visual parking slots, status refreshed from database
- Vehicle registration entry, duplicate active-vehicle check, first-free-slot allocation
- Server-side automatic entry/exit timestamps
- First 30 minutes free; after that, KSh 100 per started hour (illustrative tariff)
- Fee quote and payment confirmation before simulated exit barrier opens
- SQLite persistence across program restarts

## Requirements
- C++17 compiler
- SQLite3 development library
- `cpp-httplib` single header (`httplib.h`)
- CMake is optional; direct compilation is shown below

### Get cpp-httplib
Download the official single header from https://github.com/yhirose/cpp-httplib and place `httplib.h` in `backend/`.
Install SQLite3 development files for your operating system.

### Compile
From the project root, run a command appropriate to your system:

Linux (common setup):
```bash
g++ -std=c++17 backend/main.cpp -o parking_server -Ibackend -lsqlite3 -pthread
```

Windows with MinGW: install SQLite3 development files and add their include/library paths, then compile:
```bash
g++ -std=c++17 backend/main.cpp -o parking_server.exe -Ibackend -I"path\to\sqlite\include" -L"path\to\sqlite\lib" -lsqlite3 -lws2_32
```
Depending on your cpp-httplib version and TLS options, extra platform libraries may be needed. This demo uses plain local HTTP, not HTTPS.

Run the executable from the project root so it can find `frontend/`:
```bash
./parking_server
```
On Windows, run `parking_server.exe`. Open `http://localhost:8080` in Edge.

## Project files
- `frontend/index.html`: page structure
- `frontend/style.css`: visual styling
- `frontend/app.js`: browser interactions/API calls
- `backend/main.cpp`: HTTP routes, parking logic and SQLite operations
- `docs/`: assignment write-up notes

## Important scope notes
- Scanner input is simulated by typing the plate; connect a real scanner separately if available.
- Barriers are simulated by on-screen status messages; physical relay/controller integration is not included.
- Payment is simulated, not connected to M-Pesa or a payment gateway.
- This is a local classroom demo. It does not include user authentication, production security, or a deployment setup.
- The brief does not specify a tariff or grace period. The 30-minute free period and rate are assumptions for demonstration and should be confirmed with the client/lecturer.

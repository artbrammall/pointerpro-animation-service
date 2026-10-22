# PointerPro Animation Service

A C client/server animation service developed for COMP2017 Systems Programming at the University of Sydney (2026).

Clients connect using POSIX signals and exchange commands and responses through named pipes. The server uses a fixed pthread worker pool to execute requests, manage shared canvases and generate animation output.

## Structure

- `client/`: connection setup, login state, input and output.
- `common/`: protocol parsing and shared helpers.
- `server/`: client state, request queue, thread pool, handles, RPC execution and cleanup.
- `tests/`: 56 shell tests, including shared canvases, disconnects and concurrent requests.
- `architecture.png`: architecture diagram; `architecture.puml` is its editable source.

## Dependencies

Requires Linux (including WSL), GCC, Make, Bash, POSIX threads and FFmpeg for video generation.

The server links against a university-supplied static animation library. It is excluded by `.gitignore` and must be obtained separately through an authorised source. Place its files at:

```text
libanimate/include/animate/animate.h
libanimate/lib/libanimate.a
```

The static library must be compatible with the build machine. A clone without this dependency will not build the server. Assignment 1 is a separate implementation; compatibility as a replacement has not been verified.

## Build and run

From the project root:

```bash
make animate_server animate_client
printf 'Alice 10\n' > users.txt
./animate_server 2
```

In a second terminal in the same directory, replace `SERVER_PID` with the PID printed by the server:

```bash
./animate_client SERVER_PID
```

Enter:

```text
Login Alice
Disconnect
```

This is a local coursework service, not a production authentication system.

## Tests

```bash
make test
```

Run tests in a disposable working copy with no other instance of this service running. Tests overwrite/remove `users.txt` and generated files, and some use process-name cleanup. Do not run the suite concurrently with itself.

Both executables compiled successfully from the uploaded version in a Linux review on 22 October 2026. The test suite completed with 51 passed and 5 failed in that environment. Failures affected killed-client cleanup and four stress tests. See the accompanying review notes and logs. These failures have not been fixed.

## Background

The protocol, specification and supplied animation library originate from the course. The supplied library is not this project's implementation. Confirm redistribution permission before publishing course materials and describe development assistance accurately.

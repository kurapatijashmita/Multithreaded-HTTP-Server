# C++ Multithreaded HTTP Server

A beginner-friendly HTTP/1.1 web server written in C++17 for Windows using the native Winsock2 networking API.

---

## Project Overview

This project is an educational multithreaded HTTP server. It listens for incoming HTTP requests on `localhost:8080`, handles multiple clients concurrently using `std::thread`, routes requests to basic endpoints (`/`, `/health`, and `/status`), logs activity to the console, and returns standard HTTP status codes.

---

## Features

- **Standard HTTP/1.1 Responses**:
  - `200 OK` for valid GET requests to `/`, `/health`, and `/status`.
  - `400 Bad Request` for malformed HTTP request lines.
  - `404 Not Found` for routes that do not exist.
  - `405 Method Not Allowed` for unsupported HTTP methods (includes `Allow: GET` header).
  - `503 Service Unavailable` when the active client limit is reached.
- **Multithreading**: Dispatches each incoming connection to a separate `std::thread`.
- **Concurrency Limit**: Enforces a simple limit of 10 concurrent worker threads using atomic counters to prevent resource exhaustion.
- **Console Logging**: Prints the request method, path, HTTP status code, and response time in milliseconds to the terminal with thread-safe output.
- **Static File Serving**: Serves `index.html` at the root path (`/`).

---

## Technologies

- **Language**: C++17
- **Networking**: Windows Sockets 2 (`Winsock2` / `ws2_32.lib`)
- **Standard Library Components**:
  - `<thread>` for client concurrency
  - `<atomic>` for thread-safe counters
  - `<mutex>` for synchronized console logging
  - `<chrono>` for request timing
  - `<fstream>` and `<sstream>` for static file reading and response construction
- **Testing**: Python 3 (standard library only)

---

## Windows Compilation Instructions

Open PowerShell or Command Prompt inside the `HTTP-Server-CPP` directory:

### Option 1: GCC / MinGW (`g++`)
```powershell
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ main.cpp -o server.exe -lws2_32
```
*(The `-static` flags embed runtime libraries so `server.exe` runs independently without needing MinGW DLLs on your PATH).*

### Option 2: Microsoft Visual C++ (`cl.exe`)
From a "x64 Native Tools Command Prompt for VS":
```powershell
cl /EHsc /std:c++17 main.cpp /Fe:server.exe /link ws2_32.lib
```

---

## Running Instructions

1. Start the compiled executable:
   ```powershell
   .\server.exe
   ```

2. Open your web browser or use curl to access the server:
   - Homepage: [http://127.0.0.1:8080/](http://127.0.0.1:8080/)
   - Health check: [http://127.0.0.1:8080/health](http://127.0.0.1:8080/health)
   - Server metrics: [http://127.0.0.1:8080/status](http://127.0.0.1:8080/status)

3. To stop the server, press `Ctrl+C` in the terminal.

---

## Testing Instructions

With the server running, execute the automated test script in a separate terminal window:

```powershell
python tests/test_server.py
```

The test script uses Python's built-in standard library to verify:
1. `GET /` returns status 200 and serves HTML.
2. `GET /health` returns status 200 with JSON health status.
3. `GET /status` returns status 200 with uptime and request count.
4. `GET /nonexistent` returns status 404.
5. `POST /health` returns status 405 with an `Allow: GET` header.
6. Malformed requests return status 400.
7. Concurrency test: Sends 15 simultaneous client requests to confirm multithreaded operation.

---

## Known Limitations

This project is intended strictly for educational learning and is not intended for production use:
- **Thread-per-client model**: Creating an OS thread per connection is simple to understand, but does not scale to thousands of concurrent users.
- **No Keep-Alive / Persistent Connections**: Sockets are closed after each response (`Connection: close`).
- **Plaintext Only**: Does not support TLS/HTTPS encryption.
- **Basic Request Parsing**: Parses only the first line of the HTTP request; does not parse complex headers, request bodies, or chunked transfer encoding.

#!/usr/bin/env python3
"""
Test Suite for Multithreaded C++ HTTP Server
Verifies HTTP endpoints, status codes (200, 400, 404, 405), and concurrent client handling.
Uses only Python standard library (no pip packages required).
"""

import sys
import time
import socket
import json
import urllib.request
import urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed

BASE_URL = "http://127.0.0.1:8080"
HOST = "127.0.0.1"
PORT = 8080

def print_result(test_name: str, passed: bool, detail: str = ""):
    status = "\033[92m[PASS]\033[0m" if passed else "\033[91m[FAIL]\033[0m"
    print(f" {status} {test_name}")
    if detail:
        print(f"        -> {detail}")

def test_homepage() -> bool:
    try:
        req = urllib.request.Request(f"{BASE_URL}/", headers={"User-Agent": "TestClient/1.0"})
        with urllib.request.urlopen(req, timeout=3) as resp:
            status = resp.status
            content_type = resp.headers.get("Content-Type", "")
            body = resp.read().decode("utf-8")
            passed = (status == 200) and ("text/html" in content_type) and ("HTTP/1.1 Server" in body)
            print_result("GET / (Homepage - 200 OK)", passed, f"Status: {status}, Length: {len(body)} bytes")
            return passed
    except Exception as e:
        print_result("GET / (Homepage - 200 OK)", False, f"Exception: {e}")
        return False

def test_health() -> bool:
    try:
        req = urllib.request.Request(f"{BASE_URL}/health")
        with urllib.request.urlopen(req, timeout=3) as resp:
            status = resp.status
            body = json.loads(resp.read().decode("utf-8"))
            passed = (status == 200) and (body.get("status") == "healthy")
            print_result("GET /health (Liveness Probe - 200 OK)", passed, f"Response: {body}")
            return passed
    except Exception as e:
        print_result("GET /health (Liveness Probe - 200 OK)", False, f"Exception: {e}")
        return False

def test_status() -> bool:
    try:
        req = urllib.request.Request(f"{BASE_URL}/status")
        with urllib.request.urlopen(req, timeout=3) as resp:
            status = resp.status
            body = json.loads(resp.read().decode("utf-8"))
            has_metrics = "uptime_seconds" in body and "total_requests_served" in body
            passed = (status == 200) and has_metrics
            print_result("GET /status (Server Metrics - 200 OK)", passed, 
                         f"Uptime: {body.get('uptime_seconds')}s, Total Requests: {body.get('total_requests_served')}")
            return passed
    except Exception as e:
        print_result("GET /status (Server Metrics - 200 OK)", False, f"Exception: {e}")
        return False

def test_not_found() -> bool:
    try:
        req = urllib.request.Request(f"{BASE_URL}/nonexistent-route-path")
        urllib.request.urlopen(req, timeout=3)
        print_result("GET /nonexistent-route-path (404 Not Found)", False, "Expected 404 but got 200 OK")
        return False
    except urllib.error.HTTPError as e:
        passed = (e.code == 404)
        print_result("GET /nonexistent-route-path (404 Not Found)", passed, f"Status: {e.code}")
        return passed
    except Exception as e:
        print_result("GET /nonexistent-route-path (404 Not Found)", False, f"Exception: {e}")
        return False

def test_method_not_allowed() -> bool:
    try:
        # Send a POST request to /health
        data = json.dumps({"test": "data"}).encode("utf-8")
        req = urllib.request.Request(f"{BASE_URL}/health", data=data, method="POST")
        req.add_header("Content-Type", "application/json")
        urllib.request.urlopen(req, timeout=3)
        print_result("POST /health (405 Method Not Allowed)", False, "Expected 405 but request succeeded")
        return False
    except urllib.error.HTTPError as e:
        allow_header = e.headers.get("Allow", "")
        passed = (e.code == 405) and ("GET" in allow_header)
        print_result("POST /health (405 Method Not Allowed)", passed, f"Status: {e.code}, Allow Header: '{allow_header}'")
        return passed
    except Exception as e:
        print_result("POST /health (405 Method Not Allowed)", False, f"Exception: {e}")
        return False

def test_bad_request() -> bool:
    try:
        # Send a raw malformed request line over TCP
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(3.0)
            s.connect((HOST, PORT))
            s.sendall(b"MALFORMED_GARBAGE_REQUEST_WITHOUT_HTTP_VERSION\r\n\r\n")
            raw_response = s.recv(1024).decode("utf-8", errors="ignore")
            passed = "400 Bad Request" in raw_response
            first_line = raw_response.splitlines()[0] if raw_response else "empty"
            print_result("Malformed Request (400 Bad Request)", passed, f"Server responded: {first_line}")
            return passed
    except Exception as e:
        print_result("Malformed Request (400 Bad Request)", False, f"Exception: {e}")
        return False

def send_concurrent_request(req_id: int):
    start = time.perf_counter()
    endpoint = "/health" if req_id % 2 == 0 else "/status"
    try:
        req = urllib.request.Request(f"{BASE_URL}{endpoint}")
        with urllib.request.urlopen(req, timeout=5) as resp:
            duration_ms = (time.perf_counter() - start) * 1000.0
            return resp.status, duration_ms
    except urllib.error.HTTPError as e:
        duration_ms = (time.perf_counter() - start) * 1000.0
        return e.code, duration_ms
    except Exception as e:
        duration_ms = (time.perf_counter() - start) * 1000.0
        return 0, duration_ms

def test_concurrency(num_workers: int = 15) -> bool:
    print(f"\n [INFO] Running Concurrency Test with {num_workers} simultaneous client requests...")
    results = []
    start_total = time.perf_counter()

    with ThreadPoolExecutor(max_workers=num_workers) as executor:
        futures = [executor.submit(send_concurrent_request, i) for i in range(num_workers)]
        for f in as_completed(futures):
            results.append(f.result())

    total_time = (time.perf_counter() - start_total) * 1000.0
    status_codes = [code for code, _ in results]
    durations = [dur for _, dur in results]

    success_count = sum(1 for code in status_codes if code == 200)
    rejected_count = sum(1 for code in status_codes if code == 503) # SRE load shedding check

    min_lat = min(durations) if durations else 0
    avg_lat = sum(durations) / len(durations) if durations else 0
    max_lat = max(durations) if durations else 0

    passed = (success_count + rejected_count == num_workers) and (success_count > 0)
    print_result(f"Concurrent Load Test ({num_workers} requests)", passed,
                 f"200 OK: {success_count}, 503 Shed: {rejected_count}, "
                 f"Latency: min={min_lat:.1f}ms, avg={avg_lat:.1f}ms, max={max_lat:.1f}ms (Total: {total_time:.1f}ms)")
    return passed

def main():
    print("=" * 65)
    print("      Multithreaded C++ HTTP Server Verification Suite       ")
    print("=" * 65)
    print(f"Target: {BASE_URL}\n")

    # Quick check if server is reachable
    try:
        with socket.create_connection((HOST, PORT), timeout=2.0):
            pass
    except Exception as e:
        print(f"\033[91m[ERROR] Could not connect to {HOST}:{PORT}.\033[0m")
        print("Please ensure the C++ HTTP server is compiled and running before executing tests.")
        print("Build command:  g++ -std=c++17 main.cpp -o server.exe -lws2_32")
        print("Run command:    .\\server.exe")
        sys.exit(1)

    tests = [
        ("GET /", test_homepage),
        ("GET /health", test_health),
        ("GET /status", test_status),
        ("GET /404", test_not_found),
        ("POST /405", test_method_not_allowed),
        ("Malformed /400", test_bad_request),
        ("Concurrency", lambda: test_concurrency(15)),
    ]

    all_passed = True
    for name, test_func in tests:
        if not test_func():
            all_passed = False

    print("=" * 65)
    if all_passed:
        print("\033[92m[SUCCESS] All endpoint and concurrency tests passed successfully!\033[0m")
        sys.exit(0)
    else:
        print("\033[91m[FAILURE] One or more tests failed.\033[0m")
        sys.exit(1)

if __name__ == "__main__":
    main()

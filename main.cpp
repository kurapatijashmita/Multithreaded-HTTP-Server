#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <iomanip>

// Link Winsock library when using MSVC
#pragma comment(lib, "ws2_32.lib")

// ============================================================================
// Configuration & Shared State
// ============================================================================

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 4096;
constexpr int MAX_CONCURRENT_CLIENTS = 10; // Maximum worker threads allowed at once

// Shared metrics across threads
std::atomic<int> g_active_clients{0};
std::atomic<int> g_total_requests{0};
std::mutex g_log_mutex; // Prevents overlapping output when multiple threads log at the same time
auto g_server_start_time = std::chrono::steady_clock::now();

// ============================================================================
// Helper Functions
// ============================================================================

// Prints request details to the console safely across threads
void log_request(const std::string& method, const std::string& path, int status_code, double duration_ms) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << "[LOG] "
              << method << " " << path << " -> "
              << status_code << " ("
              << std::fixed << std::setprecision(2) << duration_ms << " ms)"
              << std::endl;
}

// Reads the index.html file into a string
std::string read_html_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "<html><body><h1>HTTP Server</h1><p>Server running. index.html not found.</p></body></html>";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Formats a standard HTTP/1.1 response string with status, headers, and body
std::string make_response(int status_code, const std::string& status_text,
                          const std::string& content_type, const std::string& body,
                          const std::string& extra_header = "") {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    if (!extra_header.empty()) {
        response << extra_header << "\r\n";
    }
    response << "\r\n";
    response << body;
    return response.str();
}

// ============================================================================
// Client Worker Function (Runs on its own thread)
// ============================================================================

void handle_client(SOCKET client_socket) {
    auto start_time = std::chrono::steady_clock::now();

    // 1. Receive raw HTTP text from the socket
    char buffer[BUFFER_SIZE] = {0};
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

    if (bytes_received <= 0) {
        closesocket(client_socket);
        --g_active_clients;
        return;
    }

    ++g_total_requests;

    // 2. Parse the first line: "METHOD PATH HTTP_VERSION"
    std::string raw_request(buffer, bytes_received);
    std::istringstream stream(raw_request);
    std::string method, path, http_version;
    stream >> method >> path >> http_version;

    std::string response;
    int status_code = 200;

    // 3. Request validation & Routing
    if (method.empty() || path.empty() || http_version.rfind("HTTP/", 0) != 0) {
        // Malformed request line -> 400 Bad Request
        status_code = 400;
        method = method.empty() ? "MALFORMED" : method;
        path = path.empty() ? "/" : path;
        response = make_response(400, "Bad Request", "application/json",
                                 "{\"error\": \"Bad Request\", \"message\": \"Malformed request line\"}\n");
    }
    else if (method != "GET") {
        // We only support GET in this beginner server -> 405 Method Not Allowed
        status_code = 405;
        response = make_response(405, "Method Not Allowed", "application/json",
                                 "{\"error\": \"Method Not Allowed\", \"message\": \"Only GET is supported\"}\n",
                                 "Allow: GET");
    }
    else if (path == "/" || path == "/index.html") {
        // Serve static HTML homepage -> 200 OK
        status_code = 200;
        response = make_response(200, "OK", "text/html; charset=utf-8", read_html_file("index.html"));
    }
    else if (path == "/health") {
        // Basic health probe -> 200 OK
        status_code = 200;
        response = make_response(200, "OK", "application/json",
                                 "{\"status\": \"healthy\", \"service\": \"HTTP-Server-CPP\"}\n");
    }
    else if (path == "/status") {
        // Basic operational metrics -> 200 OK
        status_code = 200;
        auto now = std::chrono::steady_clock::now();
        auto uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(now - g_server_start_time).count();

        std::ostringstream json;
        json << "{\n"
             << "  \"service\": \"HTTP-Server-CPP\",\n"
             << "  \"status\": \"running\",\n"
             << "  \"uptime_seconds\": " << uptime_sec << ",\n"
             << "  \"active_connections\": " << g_active_clients.load() << ",\n"
             << "  \"total_requests_served\": " << g_total_requests.load() << ",\n"
             << "  \"concurrency_limit\": " << MAX_CONCURRENT_CLIENTS << "\n"
             << "}\n";
        response = make_response(200, "OK", "application/json", json.str());
    }
    else {
        // Unrecognized path -> 404 Not Found
        status_code = 404;
        response = make_response(404, "Not Found", "application/json",
                                 "{\"error\": \"Not Found\", \"message\": \"Route not found\"}\n");
    }

    // 4. Send the HTTP response back to the client
    send(client_socket, response.c_str(), static_cast<int>(response.length()), 0);

    // 5. Measure duration and log to terminal
    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    log_request(method, path, status_code, duration_ms);

    // 6. Close the connection and free the active thread count
    closesocket(client_socket);
    --g_active_clients;
}

// ============================================================================
// Main Function: Socket Setup & Accept Loop
// ============================================================================

int main() {
    std::cout << "===============================================" << std::endl;
    std::cout << "   Beginner C++17 Multithreaded HTTP Server    " << std::endl;
    std::cout << "===============================================" << std::endl;

    // Step 1: Initialize the Windows Sockets (Winsock) library
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    // Step 2: Create a TCP stream socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: Bind the socket to IP 127.0.0.1 and Port 8080
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Step 4: Start listening for incoming connections
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Listening on http://127.0.0.1:" << PORT << std::endl;
    std::cout << "Max concurrent clients: " << MAX_CONCURRENT_CLIENTS << std::endl;
    std::cout << "Available routes: /, /health, /status" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;
    std::cout << "-----------------------------------------------" << std::endl;

    // Step 5: Accept loop
    while (true) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);

        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept error: " << WSAGetLastError() << std::endl;
            continue;
        }

        // Concurrency Guard: If at max capacity, return 503 instead of overloading
        if (g_active_clients.load() >= MAX_CONCURRENT_CLIENTS) {
            std::string body = "{\"error\": \"Service Unavailable\", \"message\": \"Server busy\"}\n";
            std::string resp = make_response(503, "Service Unavailable", "application/json", body, "Retry-After: 1");
            send(client_socket, resp.c_str(), static_cast<int>(resp.length()), 0);
            log_request("REJECTED", "LIMIT_EXCEEDED", 503, 0.0);
            closesocket(client_socket);
            continue;
        }

        // Spawn a new detached thread for this client
        ++g_active_clients;
        std::thread(handle_client, client_socket).detach();
    }

    // Cleanup (reached if loop is exited)
    closesocket(server_socket);
    WSACleanup();
    return 0;
}

#include "http_server.h"

#ifndef _WIN32
#error "This simple API server is implemented for Windows/Winsock."
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include "config.h"
#include "scheduler.h"

namespace timetable {
namespace {

std::mutex g_schedule_mutex;

std::string JsonEscape(const std::string& s) {
    std::ostringstream out;
    for (unsigned char ch : s) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "?";
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "";
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool FileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

std::string MakeHttpResponse(int code, const std::string& status, const std::string& content_type, const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << code << " " << status << "\r\n";
    out << "Content-Type: " << content_type << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    out << "Access-Control-Allow-Headers: Content-Type\r\n";
    out << "\r\n";
    out << body;
    return out.str();
}

std::string JsonResponse(int code, const std::string& status, const std::string& body) {
    return MakeHttpResponse(code, status, "application/json; charset=utf-8", body);
}

std::string TextResponse(int code, const std::string& status, const std::string& body) {
    return MakeHttpResponse(code, status, "text/plain; charset=utf-8", body);
}

std::string NotFoundJson(const std::string& message) {
    return JsonResponse(404, "Not Found", "{\"success\":false,\"message\":\"" + JsonEscape(message) + "\"}");
}

int GroupIndexFromPathValue(const std::string& value) {
    if (value == "0" || value == "isp-3304" || value == "ISP-3304") {
        return 0;
    }

    if (value == "1" || value == "isp-3305p" || value == "ISP-3305P" || value == "ISP-3305p") {
        return 1;
    }

    for (int i = 0; i < GROUPS; i++) {
        if (value == GROUP_NAME[i]) {
            return i;
        }
    }

    return -1;
}

void SendAll(SOCKET client, const std::string& data) {
    const char* ptr = data.data();
    int left = static_cast<int>(data.size());

    while (left > 0) {
        int sent = send(client, ptr, left, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            break;
        }
        ptr += sent;
        left -= sent;
    }
}

std::string HandleRequest(const std::string& request, const std::string& output_dir) {
    std::istringstream request_stream(request);
    std::string method;
    std::string path;
    std::string version;
    request_stream >> method >> path >> version;

    const std::filesystem::path out_dir(output_dir);

    if (method == "OPTIONS") {
        return JsonResponse(200, "OK", "{}");
    }

    if (method == "GET" && (path == "/" || path == "/api")) {
        return JsonResponse(200, "OK",
            "{"
            "\"name\":\"timetable api\"," 
            "\"endpoints\":["
            "\"POST /api/schedule/regenerate\"," 
            "\"GET /api/schedule\"," 
            "\"GET /api/schedule/group/0\"," 
            "\"GET /api/schedule/group/1\""
            "]}"
        );
    }

    if (method == "POST" && path == "/api/schedule/regenerate") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        GenerationResult result = GenerateSchedule(output_dir);
        std::ostringstream body;
        body << "{\"success\":" << (result.success ? "true" : "false")
             << ",\"status\":\"" << JsonEscape(result.status) << "\""
             << ",\"message\":\"" << JsonEscape(result.message) << "\""
             << ",\"output_dir\":\"" << JsonEscape(result.output_dir) << "\"}"
             ;
        return JsonResponse(result.success ? 200 : 500, result.success ? "OK" : "Internal Server Error", body.str());
    }

    if (method == "GET" && path == "/api/schedule") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path file = out_dir / "schedule_all.json";
        if (!FileExists(file)) {
            return NotFoundJson("Расписание ещё не сгенерировано. Вызови POST /api/schedule/regenerate.");
        }
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    const std::string group_prefix = "/api/schedule/group/";
    if (method == "GET" && path.rfind(group_prefix, 0) == 0) {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::string value = path.substr(group_prefix.size());
        int group = GroupIndexFromPathValue(value);
        if (group < 0 || group >= GROUPS) {
            return NotFoundJson("Группа не найдена. Используй 0, 1, ISP-3304 или ISP-3305p.");
        }

        std::filesystem::path file = out_dir / "groups" / ("group_" + std::to_string(group) + ".json");
        if (!FileExists(file)) {
            return NotFoundJson("Расписание ещё не сгенерировано. Вызови POST /api/schedule/regenerate.");
        }
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    return NotFoundJson("Неизвестный endpoint");
}

}  // namespace

int RunApiServer(const std::string& host, int port, const std::string& output_dir) {
    WSADATA wsa_data;
    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        std::cerr << "WSAStartup failed: " << startup_result << "\n";
        return 1;
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, host.c_str(), &service.sin_addr);

    if (bind(listen_socket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << "\n";
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << "\n";
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    std::filesystem::create_directories(output_dir);

    std::cout << "API запущено: http://" << host << ":" << port << "\n";
    std::cout << "Генерация НЕ запускается автоматически. Запусти POST /api/schedule/regenerate\n";
    std::cout << "Файлы расписания будут в папке: " << output_dir << "\n";

    while (true) {
        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "accept failed: " << WSAGetLastError() << "\n";
            continue;
        }

        char buffer[8192];
        int received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            std::string request(buffer, received);
            std::string response = HandleRequest(request, output_dir);
            SendAll(client_socket, response);
        }

        shutdown(client_socket, SD_SEND);
        closesocket(client_socket);
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}

}  // namespace timetable

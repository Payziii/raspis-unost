#include "http_server.h"

#ifndef _WIN32
#error "This simple API server is implemented for Windows/Winsock."
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include "config.h"
#include "data_store.h"
#include "json_utils.h"
#include "scheduler.h"

namespace timetable {
namespace {

std::mutex g_schedule_mutex;

std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
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
    out << "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n";
    out << "Access-Control-Allow-Headers: Content-Type\r\n";
    out << "\r\n";
    out << body;
    return out.str();
}

std::string JsonResponse(int code, const std::string& status, const std::string& body) {
    return MakeHttpResponse(code, status, "application/json; charset=utf-8", body);
}

std::string ErrorJson(int code, const std::string& status, const std::string& message) {
    return JsonResponse(code, status, "{\"success\":false,\"message\":\"" + JsonEscape(message) + "\"}");
}

std::string OkJson(const JsonValue& value) {
    return JsonResponse(200, "OK", ToJson(value, 2));
}

std::string CreatedJson(const JsonValue& value) {
    return JsonResponse(201, "Created", ToJson(value, 2));
}

std::string BodyOfRequest(const std::string& request) {
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return request.substr(pos + 4);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string UrlDecode(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size()) {
            std::string hex = value.substr(i + 1, 2);
            char* end = nullptr;
            long c = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(c));
                i += 2;
                continue;
            }
        }
        if (value[i] == '+') out.push_back(' ');
        else out.push_back(value[i]);
    }
    return out;
}

int ParseIdFromPath(const std::string& path, const std::string& prefix) {
    if (path.rfind(prefix, 0) != 0) return -1;
    std::string id_text = path.substr(prefix.size());
    if (id_text.empty()) return -1;
    try {
        return std::stoi(id_text);
    } catch (...) {
        return -1;
    }
}

JsonParseResult LoadRoot() {
    EnsureDataFileExists();
    return ParseJson(ReadDataJsonText());
}

bool SaveRoot(const JsonValue& root, std::string& error) {
    return SaveDataJson(root, error);
}

JsonValue ResponseEnvelope(bool success, const std::string& message, const JsonValue* data = nullptr) {
    JsonValue root = JsonValue::MakeObject();
    root.At("success") = JsonValue::MakeBool(success);
    root.At("message") = JsonValue::MakeString(message);
    root.At("needs_regenerate") = JsonValue::MakeBool(true);
    if (data) root.At("data") = *data;
    return root;
}

std::string GetArrayEndpoint(const std::string& array_name) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    return OkJson(parsed.value.At(array_name));
}

std::string PostArrayEndpoint(const std::string& array_name, const std::string& body) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) array_value = JsonValue::MakeArray();

    JsonValue item = body_json.value;
    if (!item.Has("id")) {
        item.At("id") = JsonValue::MakeNumber(NextId(array_value));
    }
    array_value.array_value.push_back(item);

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Сохранено. Для применения вызови POST /api/schedule/regenerate.", &item);
    return CreatedJson(envelope);
}

std::string PutArrayEndpoint(const std::string& array_name, int id, const std::string& body) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) return ErrorJson(404, "Not Found", "Массив " + array_name + " не найден");

    JsonValue* existing = FindObjectById(array_value, id);
    if (!existing) return ErrorJson(404, "Not Found", "Запись не найдена");

    JsonValue item = body_json.value;
    item.At("id") = JsonValue::MakeNumber(id);
    *existing = item;

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Обновлено. Для применения вызови POST /api/schedule/regenerate.", &item);
    return OkJson(envelope);
}

std::string PatchArrayEndpoint(const std::string& array_name, int id, const std::string& body) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) return ErrorJson(404, "Not Found", "Массив " + array_name + " не найден");

    JsonValue* existing = FindObjectById(array_value, id);
    if (!existing) return ErrorJson(404, "Not Found", "Запись не найдена");

    for (const auto& kv : body_json.value.object_value) {
        if (kv.first == "id") continue;
        existing->At(kv.first) = kv.second;
    }
    existing->At("id") = JsonValue::MakeNumber(id);

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Обновлено. Для применения вызови POST /api/schedule/regenerate.", existing);
    return OkJson(envelope);
}

std::string DeleteArrayEndpoint(const std::string& array_name, int id) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);

    JsonValue& array_value = parsed.value.At(array_name);
    if (!array_value.IsArray()) return ErrorJson(404, "Not Found", "Массив " + array_name + " не найден");
    if (!RemoveObjectById(array_value, id)) return ErrorJson(404, "Not Found", "Запись не найдена");

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Удалено. Для применения вызови POST /api/schedule/regenerate.");
    return OkJson(envelope);
}

std::string GetOneEndpoint(const std::string& array_name, int id) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonValue& array_value = parsed.value.At(array_name);
    JsonValue* item = FindObjectById(array_value, id);
    if (!item) return ErrorJson(404, "Not Found", "Запись не найдена");
    return OkJson(*item);
}

int GroupIndexFromPathValue(const std::string& raw_value) {
    std::string value = UrlDecode(raw_value);
    try {
        return std::stoi(value);
    } catch (...) {
        // дальше ищем по имени
    }

    ScheduleInputData data;
    std::string error;
    if (!LoadScheduleInputData(data, error)) return -1;
    std::string lowered = Lower(value);
    for (const GroupData& group : data.groups) {
        if (Lower(group.name) == lowered) return group.id;
    }
    return -1;
}

std::string GetSettings() {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    return OkJson(parsed.value.At("settings"));
}

std::string UpdateSettings(const std::string& body, bool patch) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);
    JsonParseResult body_json = ParseJson(body);
    if (!body_json.ok || !body_json.value.IsObject()) {
        return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
    }

    JsonValue& settings = parsed.value.At("settings");
    if (!settings.IsObject() || !patch) settings = JsonValue::MakeObject();
    for (const auto& kv : body_json.value.object_value) {
        settings.At(kv.first) = kv.second;
    }

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
    JsonValue envelope = ResponseEnvelope(true, "Настройки сохранены. Для применения вызови POST /api/schedule/regenerate.", &settings);
    return OkJson(envelope);
}

std::string HandleCrud(const std::string& method, const std::string& path, const std::string& body,
                       const std::string& api_prefix, const std::string& array_name) {
    if (path == api_prefix) {
        if (method == "GET") return GetArrayEndpoint(array_name);
        if (method == "POST") return PostArrayEndpoint(array_name, body);
    }

    const std::string one_prefix = api_prefix + "/";
    if (path.rfind(one_prefix, 0) == 0) {
        int id = ParseIdFromPath(path, one_prefix);
        if (id < 0) return ErrorJson(400, "Bad Request", "Некорректный id");
        if (method == "GET") return GetOneEndpoint(array_name, id);
        if (method == "PUT") return PutArrayEndpoint(array_name, id, body);
        if (method == "PATCH") return PatchArrayEndpoint(array_name, id, body);
        if (method == "DELETE") return DeleteArrayEndpoint(array_name, id);
    }

    return "";
}

std::string HandleRequest(const std::string& request, const std::string& output_dir) {
    std::istringstream request_stream(request);
    std::string method;
    std::string path;
    std::string version;
    request_stream >> method >> path >> version;
    std::string body = BodyOfRequest(request);

    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) path = path.substr(0, query_pos);

    const std::filesystem::path out_dir(output_dir);

    if (method == "OPTIONS") return JsonResponse(200, "OK", "{}");

    if (method == "GET" && (path == "/" || path == "/api")) {
        return JsonResponse(200, "OK",
            "{"
            "\"name\":\"timetable api\","
            "\"note\":\"После изменения данных вызови POST /api/schedule/regenerate\","
            "\"endpoints\":["
            "\"GET /api/data\","
            "\"PUT /api/data\","
            "\"GET/PUT/PATCH /api/settings\","
            "\"GET/POST /api/groups\","
            "\"GET/PUT/PATCH/DELETE /api/groups/{id}\","
            "\"GET/POST /api/teachers\","
            "\"GET/PUT/PATCH/DELETE /api/teachers/{id}\","
            "\"GET/POST /api/lessons\","
            "\"GET/PUT/PATCH/DELETE /api/lessons/{id}\","
            "\"GET/POST /api/unavailable\","
            "\"GET/PUT/PATCH/DELETE /api/unavailable/{id}\","
            "\"POST /api/schedule/regenerate\","
            "\"GET /api/schedule\","
            "\"GET /api/schedule/group/{id-or-name}\""
            "]}"
        );
    }

    if (method == "GET" && path == "/api/data") {
        return JsonResponse(200, "OK", ReadDataJsonText());
    }

    if (method == "PUT" && path == "/api/data") {
        JsonParseResult parsed = ParseJson(body);
        if (!parsed.ok || !parsed.value.IsObject()) return ErrorJson(400, "Bad Request", parsed.error.empty() ? "Нужен JSON-объект" : parsed.error);
        std::string error;
        if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);
        JsonValue envelope = ResponseEnvelope(true, "Файл данных заменён. Для применения вызови POST /api/schedule/regenerate.");
        return OkJson(envelope);
    }

    if (method == "GET" && path == "/api/settings") return GetSettings();
    if ((method == "PUT" || method == "PATCH") && path == "/api/settings") return UpdateSettings(body, method == "PATCH");

    std::string crud;
    crud = HandleCrud(method, path, body, "/api/groups", "groups");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/teachers", "teachers");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/lessons", "lessons");
    if (!crud.empty()) return crud;
    crud = HandleCrud(method, path, body, "/api/unavailable", "unavailable");
    if (!crud.empty()) return crud;

    if (method == "POST" && path == "/api/schedule/regenerate") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        GenerationResult result = GenerateSchedule(output_dir);
        std::ostringstream response_body;
        response_body << "{\"success\":" << (result.success ? "true" : "false")
                      << ",\"status\":\"" << JsonEscape(result.status) << "\""
                      << ",\"message\":\"" << JsonEscape(result.message) << "\""
                      << ",\"output_dir\":\"" << JsonEscape(result.output_dir) << "\"}";
        return JsonResponse(result.success ? 200 : 500, result.success ? "OK" : "Internal Server Error", response_body.str());
    }

    if (method == "GET" && path == "/api/schedule") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path file = out_dir / "schedule_all.json";
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Расписание ещё не сгенерировано. Вызови POST /api/schedule/regenerate.");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    const std::string group_prefix = "/api/schedule/group/";
    if (method == "GET" && path.rfind(group_prefix, 0) == 0) {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::string value = path.substr(group_prefix.size());
        int group = GroupIndexFromPathValue(value);
        if (group < 0) return ErrorJson(404, "Not Found", "Группа не найдена");

        std::filesystem::path file = out_dir / "groups" / ("group_" + std::to_string(group) + ".json");
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Расписание ещё не сгенерировано. Вызови POST /api/schedule/regenerate.");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    return ErrorJson(404, "Not Found", "Неизвестный endpoint");
}

int HeaderContentLength(const std::string& request) {
    std::istringstream ss(request);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string lower = Lower(line);
        const std::string prefix = "content-length:";
        if (lower.rfind(prefix, 0) == 0) {
            std::string number = line.substr(prefix.size());
            try { return std::stoi(number); } catch (...) { return 0; }
        }
        if (line.empty()) break;
    }
    return 0;
}

void SendAll(SOCKET client, const std::string& data) {
    const char* ptr = data.data();
    int left = static_cast<int>(data.size());
    while (left > 0) {
        int sent = send(client, ptr, left, 0);
        if (sent == SOCKET_ERROR || sent == 0) break;
        ptr += sent;
        left -= sent;
    }
}

std::string ReceiveFullRequest(SOCKET client_socket) {
    std::string request;
    char buffer[8192];
    int received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (received <= 0) return request;
    request.append(buffer, received);

    size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) return request;

    int content_length = HeaderContentLength(request.substr(0, header_end + 4));
    size_t body_start = header_end + 4;
    while (content_length > 0 && request.size() < body_start + static_cast<size_t>(content_length)) {
        received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        request.append(buffer, received);
    }
    return request;
}

}  // namespace

int RunApiServer(const std::string& host, int port, const std::string& output_dir) {
    EnsureDataFileExists();

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
    std::cout << "Файл данных: " << DataFilePath() << "\n";
    std::cout << "Файлы расписания будут в папке: " << output_dir << "\n";

    while (true) {
        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "accept failed: " << WSAGetLastError() << "\n";
            continue;
        }

        std::string request = ReceiveFullRequest(client_socket);
        if (!request.empty()) {
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

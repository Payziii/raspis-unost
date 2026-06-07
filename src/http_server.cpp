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
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "data_store.h"
#include "json_utils.h"
#include "runtime_config.h"
#include "scheduler.h"

namespace timetable {
namespace {

std::mutex g_schedule_mutex;

// ── Async generation state ────────────────────────────────────────────────────

struct WeekEntry {
    int num = 0;
    std::string date_from;
    std::string date_to;
    std::string status;   // "pending" | "running" | "done" | "failed" | "skipped"
    double elapsed = 0.0;
};

struct GenState {
    std::mutex mu;
    std::atomic<bool> running{false};
    std::atomic<bool> cancel_requested{false};

    // Защищено mu:
    std::string state;          // "idle" | "running" | "done" | "failed" | "cancelled"
    int total_weeks = 0;
    int current_week = 0;       // 1-based, 0 = ещё не начинали
    int solved_weeks = 0;
    std::vector<WeekEntry> weeks;
    std::string result_message;
    double total_elapsed = 0.0;
    bool result_success = false;

    void reset(int total) {
        std::lock_guard<std::mutex> lock(mu);
        state = "running";
        total_weeks = total;
        current_week = 0;
        solved_weeks = 0;
        weeks.clear();
        for (int i = 0; i < total; i++) {
            weeks.push_back({i + 1, "", "", "pending", 0.0});
        }
        result_message = "";
        total_elapsed = 0.0;
        result_success = false;
    }
};

static GenState g_gen;
static auto g_gen_start = std::chrono::steady_clock::now();

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

std::string GetSolverConfig() {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);

    JsonValue& settings = parsed.value.At("settings");
    JsonValue& solver_config = settings.At("solver_config");

    if (!solver_config.IsObject()) {
        solver_config = SolverConfigToJson(DefaultSolverConfig());
    } else {
        JsonValue defaults = SolverConfigToJson(DefaultSolverConfig());
        for (const auto& kv : defaults.object_value) {
            if (!solver_config.Has(kv.first)) {
                solver_config.At(kv.first) = kv.second;
            }
        }
    }

    JsonValue schema_arr = JsonValue::MakeArray();
    for (const SolverConfigField& field : SolverConfigSchema()) {
        JsonValue entry = JsonValue::MakeObject();
        entry.At("key") = JsonValue::MakeString(field.key);
        entry.At("label") = JsonValue::MakeString(field.label);
        entry.At("description") = JsonValue::MakeString(field.description);
        entry.At("category") = JsonValue::MakeString(field.category);
        entry.At("type") = JsonValue::MakeString(field.type);
        schema_arr.array_value.push_back(entry);
    }

    JsonValue result = JsonValue::MakeObject();
    result.At("values") = solver_config;
    result.At("schema") = schema_arr;
    result.At("defaults") = SolverConfigToJson(DefaultSolverConfig());
    return OkJson(result);
}

std::string UpdateSolverConfig(const std::string& body, bool reset_to_defaults) {
    JsonParseResult parsed = LoadRoot();
    if (!parsed.ok) return ErrorJson(500, "Internal Server Error", parsed.error);

    JsonValue& settings = parsed.value.At("settings");
    if (!settings.IsObject()) settings = JsonValue::MakeObject();

    if (reset_to_defaults) {
        settings.At("solver_config") = SolverConfigToJson(DefaultSolverConfig());
        LoadSolverConfigFromJson(settings.At("solver_config"));
    } else {
        JsonParseResult body_json = ParseJson(body);
        if (!body_json.ok || !body_json.value.IsObject()) {
            return ErrorJson(400, "Bad Request", "Нужен JSON-объект в теле запроса");
        }

        JsonValue& solver_config = settings.At("solver_config");
        if (!solver_config.IsObject()) solver_config = SolverConfigToJson(DefaultSolverConfig());

        for (const auto& kv : body_json.value.object_value) {
            solver_config.At(kv.first) = kv.second;
        }

        LoadSolverConfigFromJson(solver_config);
    }

    std::string error;
    if (!SaveRoot(parsed.value, error)) return ErrorJson(500, "Internal Server Error", error);

    JsonValue envelope = ResponseEnvelope(
        true,
        reset_to_defaults
            ? "Параметры солвера сброшены к дефолтам. Запусти регенерацию чтобы применить."
            : "Параметры солвера сохранены. Запусти регенерацию чтобы применить.",
        &settings.At("solver_config")
    );
    return OkJson(envelope);
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
            "\"GET/PUT/PATCH /api/settings/solver-config\","
            "\"POST /api/settings/solver-config/reset\","
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

    if (method == "GET" && path == "/api/settings/solver-config") return GetSolverConfig();
    if ((method == "PUT" || method == "PATCH") && path == "/api/settings/solver-config") {
        return UpdateSolverConfig(body, false);
    }
    if (method == "POST" && path == "/api/settings/solver-config/reset") {
        return UpdateSolverConfig("", true);
    }

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
        // Уже идёт — отказываем
        if (g_gen.running.load()) {
            return ErrorJson(409, "Conflict", "Генерация уже запущена. Дождитесь завершения или отмените.");
        }

        GenerationOptions opts;
        opts.lock_source = "none";
        std::string gen_mode = "weekly";
        if (!body.empty()) {
            JsonParseResult parsed = ParseJson(body);
            if (parsed.ok && parsed.value.IsObject()) {
                gen_mode = JsonString(parsed.value, "mode", "weekly");
                std::string lock_existing = JsonString(parsed.value, "lock_existing", "none");
                std::string lock_path;
                if (lock_existing == "manual") {
                    lock_path = (std::filesystem::path("output") / "manual" / "schedule_all.json").string();
                } else if (lock_existing == "auto") {
                    lock_path = (std::filesystem::path(output_dir) / "schedule_all.json").string();
                }
                if (!lock_path.empty() && FileExists(lock_path)) {
                    JsonParseResult sched = ParseJson(ReadFileUtf8(lock_path));
                    if (sched.ok && sched.value.IsObject()) {
                        const JsonValue& groups_arr = sched.value.At("groups");
                        if (groups_arr.IsArray()) {
                            for (const JsonValue& g : groups_arr.array_value) {
                                if (!g.IsObject()) continue;
                                const JsonValue& days = g.At("days");
                                if (!days.IsArray()) continue;
                                for (const JsonValue& day : days.array_value) {
                                    if (!day.IsObject()) continue;
                                    Date date{};
                                    if (!ParseDateIso(JsonString(day, "date_iso", ""), date)) {
                                        std::string disp = JsonString(day, "date", "");
                                        if (disp.size() == 10 && disp[2] == '.' && disp[5] == '.') {
                                            try {
                                                date.day = std::stoi(disp.substr(0, 2));
                                                date.month = std::stoi(disp.substr(3, 2));
                                                date.year = std::stoi(disp.substr(6, 4));
                                            } catch (...) { continue; }
                                        } else { continue; }
                                    }
                                    const JsonValue& slots = day.At("slots");
                                    if (!slots.IsArray()) continue;
                                    for (const JsonValue& slot : slots.array_value) {
                                        if (!slot.IsObject()) continue;
                                        int slot_num = JsonInt(slot, "slot", 0);
                                        if (slot_num < 1) continue;
                                        const JsonValue& lessons_arr = slot.At("lessons");
                                        if (!lessons_arr.IsArray()) continue;
                                        for (const JsonValue& l : lessons_arr.array_value) {
                                            if (!l.IsObject()) continue;
                                            int lid = JsonInt(l, "id", -1);
                                            if (lid < 0) continue;
                                            LockedAssignment a;
                                            a.lesson_id = lid;
                                            a.date = date;
                                            a.slot = slot_num - 1;
                                            opts.locked.push_back(a);
                                        }
                                    }
                                }
                            }
                        }
                        opts.lock_source = lock_existing;
                    }
                } else if (lock_existing == "manual" || lock_existing == "auto") {
                    return ErrorJson(404, "Not Found",
                        lock_existing == "manual"
                            ? "Ручное расписание пусто. Открой Конструктор и сохрани хотя бы одно занятие."
                            : "Автогенерации ещё нет — сначала сгенерируй обычное расписание.");
                }
            }
        }

        if (gen_mode == "monolithic") {
            // Монолитный режим — синхронно (без прогресса)
            std::lock_guard<std::mutex> lock(g_schedule_mutex);
            GenerationResult result = GenerateSchedule(output_dir, opts);
            std::ostringstream rb;
            rb << "{\"success\":" << (result.success ? "true" : "false")
               << ",\"status\":\"" << JsonEscape(result.status) << "\""
               << ",\"message\":\"" << JsonEscape(result.message) << "\""
               << ",\"mode\":\"monolithic\""
               << ",\"async\":false}";
            return JsonResponse(result.success ? 200 : 500,
                result.success ? "OK" : "Internal Server Error", rb.str());
        }

        // Недельный режим — async
        g_gen.cancel_requested.store(false);
        g_gen.running.store(true);
        g_gen_start = std::chrono::steady_clock::now();

        // Инициализируем прогресс (предварительно 0 недель, уточнится при старте)
        {
            std::lock_guard<std::mutex> lk(g_gen.mu);
            g_gen.state = "running";
            g_gen.total_weeks = 0;
            g_gen.current_week = 0;
            g_gen.solved_weeks = 0;
            g_gen.weeks.clear();
            g_gen.result_message = "";
            g_gen.total_elapsed = 0.0;
        }

        // Захватываем всё нужное для потока
        std::string cap_output_dir = output_dir;
        GenerationOptions cap_opts = opts;

        std::thread([cap_output_dir, cap_opts]() mutable {
            WeeklyGenCallbacks cbs;
            cbs.cancel_flag = &g_gen.cancel_requested;

            cbs.on_week_start = [](int w, int total, std::string df, std::string dt) {
                std::lock_guard<std::mutex> lk(g_gen.mu);
                // Если недель ещё не знаем — инициализируем
                if (g_gen.total_weeks != total) {
                    g_gen.total_weeks = total;
                    g_gen.weeks.resize(total);
                    for (int i = 0; i < total; i++) {
                        g_gen.weeks[i].num = i + 1;
                        g_gen.weeks[i].status = "pending";
                    }
                }
                g_gen.current_week = w + 1;
                if (w < (int)g_gen.weeks.size()) {
                    g_gen.weeks[w].date_from = df;
                    g_gen.weeks[w].date_to   = dt;
                    g_gen.weeks[w].status = "running";
                }
            };

            cbs.on_week_done = [](int w, int total, std::string df, std::string dt,
                                  std::string status, double elapsed) {
                std::lock_guard<std::mutex> lk(g_gen.mu);
                if (g_gen.total_weeks != total) {
                    g_gen.total_weeks = total;
                    g_gen.weeks.resize(total);
                    for (int i = 0; i < total; i++) {
                        g_gen.weeks[i].num = i + 1;
                        g_gen.weeks[i].status = "pending";
                    }
                }
                if (w < (int)g_gen.weeks.size()) {
                    g_gen.weeks[w].date_from = df;
                    g_gen.weeks[w].date_to   = dt;
                    g_gen.weeks[w].status = status;
                    g_gen.weeks[w].elapsed = elapsed;
                }
                if (status == "done" || status == "skipped") g_gen.solved_weeks++;
                g_gen.total_elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - g_gen_start).count();
            };

            GenerationResult result = GenerateScheduleWeekly(cap_output_dir, cap_opts, cbs);

            {
                std::lock_guard<std::mutex> lk(g_gen.mu);
                g_gen.total_elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - g_gen_start).count();
                g_gen.result_message = result.message;
                g_gen.result_success = result.success;
                if (result.status == "CANCELLED") {
                    g_gen.state = "cancelled";
                } else {
                    g_gen.state = result.success ? "done" : "failed";
                }
            }
            g_gen.running.store(false);
        }).detach();

        std::ostringstream rb;
        rb << "{\"started\":true,\"async\":true,\"mode\":\"weekly\""
           << ",\"lock_source\":\"" << JsonEscape(opts.lock_source) << "\""
           << ",\"locked_count\":" << opts.locked.size() << "}";
        return JsonResponse(202, "Accepted", rb.str());
    }

    if (method == "GET" && path == "/api/schedule/progress") {
        std::lock_guard<std::mutex> lk(g_gen.mu);
        std::ostringstream out;
        out << "{\"state\":\"" << JsonEscape(g_gen.state) << "\""
            << ",\"total_weeks\":" << g_gen.total_weeks
            << ",\"current_week\":" << g_gen.current_week
            << ",\"solved_weeks\":" << g_gen.solved_weeks
            << ",\"total_elapsed\":" << std::fixed << std::setprecision(1) << g_gen.total_elapsed
            << ",\"message\":\"" << JsonEscape(g_gen.result_message) << "\""
            << ",\"success\":" << (g_gen.result_success ? "true" : "false")
            << ",\"weeks\":[";
        for (int i = 0; i < (int)g_gen.weeks.size(); i++) {
            const auto& w = g_gen.weeks[i];
            if (i > 0) out << ",";
            out << "{\"num\":" << w.num
                << ",\"date_from\":\"" << JsonEscape(w.date_from) << "\""
                << ",\"date_to\":\"" << JsonEscape(w.date_to) << "\""
                << ",\"status\":\"" << JsonEscape(w.status) << "\""
                << ",\"elapsed\":" << std::fixed << std::setprecision(1) << w.elapsed << "}";
        }
        out << "]}";
        return JsonResponse(200, "OK", out.str());
    }

    if (method == "POST" && path == "/api/schedule/cancel") {
        if (!g_gen.running.load()) {
            return JsonResponse(200, "OK", "{\"cancelled\":false,\"message\":\"Генерация не запущена\"}");
        }
        g_gen.cancel_requested.store(true);
        return JsonResponse(200, "OK", "{\"cancelled\":true,\"message\":\"Запрос на отмену отправлен\"}");
    }

    if (method == "GET" && path == "/api/schedule") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path file = out_dir / "schedule_all.json";
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Расписание ещё не сгенерировано. Вызови POST /api/schedule/regenerate.");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    // ── Manual (Конструктор) endpoints ──
    if (method == "GET" && path == "/api/schedule/manual") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path file = std::filesystem::path("output") / "manual" / "schedule_all.json";
        if (!FileExists(file)) return ErrorJson(404, "Not Found", "Ручное расписание пусто. Скопируй из автогенерации или начни с нуля.");
        return JsonResponse(200, "OK", ReadFileUtf8(file));
    }

    if (method == "POST" && path == "/api/schedule/manual") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        JsonParseResult parsed = ParseJson(body);
        if (!parsed.ok || !parsed.value.IsObject()) {
            return ErrorJson(400, "Bad Request", parsed.error.empty() ? "Нужен JSON-объект" : parsed.error);
        }
        std::filesystem::path manual_dir = std::filesystem::path("output") / "manual";
        std::filesystem::path groups_dir = manual_dir / "groups";
        std::error_code ec;
        std::filesystem::create_directories(groups_dir, ec);

        std::ofstream out_all(manual_dir / "schedule_all.json", std::ios::binary);
        if (!out_all) return ErrorJson(500, "Internal Server Error", "Не удалось открыть output/manual/schedule_all.json");
        out_all << ToJson(parsed.value, 2);
        out_all.close();

        const JsonValue& groups_arr = parsed.value.At("groups");
        if (groups_arr.IsArray()) {
            for (const JsonValue& group : groups_arr.array_value) {
                if (!group.IsObject()) continue;
                int gi = JsonInt(group, "group_index", -1);
                if (gi < 0) continue;
                std::ofstream go(groups_dir / ("group_" + std::to_string(gi) + ".json"), std::ios::binary);
                if (go) go << ToJson(group, 2);
            }
        }

        return OkJson(ResponseEnvelope(true, "Ручное расписание сохранено."));
    }

    if (method == "DELETE" && path == "/api/schedule/manual") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path manual_dir = std::filesystem::path("output") / "manual";
        std::error_code ec;
        std::filesystem::remove_all(manual_dir, ec);
        return OkJson(ResponseEnvelope(true, "Ручное расписание очищено."));
    }

    if (method == "POST" && path == "/api/schedule/manual/copy-from-auto") {
        std::lock_guard<std::mutex> lock(g_schedule_mutex);
        std::filesystem::path manual_dir = std::filesystem::path("output") / "manual";
        std::filesystem::path groups_dir = manual_dir / "groups";
        std::error_code ec;
        std::filesystem::create_directories(groups_dir, ec);
        std::filesystem::path src_all = out_dir / "schedule_all.json";
        if (!FileExists(src_all)) {
            return ErrorJson(404, "Not Found", "Автогенерации ещё нет. Сначала сгенерируй расписание.");
        }
        std::filesystem::copy_file(src_all, manual_dir / "schedule_all.json", std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return ErrorJson(500, "Internal Server Error", "Не удалось скопировать schedule_all.json: " + ec.message());

        std::filesystem::path src_groups = out_dir / "groups";
        if (std::filesystem::exists(src_groups, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(src_groups, ec)) {
                if (!entry.is_regular_file()) continue;
                std::filesystem::copy_file(entry.path(), groups_dir / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
        return OkJson(ResponseEnvelope(true, "Расписание скопировано из автогенерации в Конструктор."));
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

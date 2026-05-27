#include "data_store.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "config.h"
#include "format_utils.h"
#include "date_utils.h"
#include "runtime_config.h"

namespace timetable {
namespace {

JsonValue GroupJson(int id, const std::string& name, int parts) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("name") = JsonValue::MakeString(name);
    v.At("parts") = JsonValue::MakeNumber(parts);
    return v;
}

JsonValue TeacherJson(int id, const std::string& name) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("name") = JsonValue::MakeString(name);
    return v;
}

JsonValue LessonJson(
    int id,
    int group,
    int subgroup,
    int teacher,
    int total_slots,
    const std::string& name,
    int subject_id,
    bool is_lab,
    bool is_block,
    const std::vector<int>& allowed_campuses
) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("group") = JsonValue::MakeNumber(group);
    v.At("subgroup") = JsonValue::MakeNumber(subgroup);
    v.At("teacher") = JsonValue::MakeNumber(teacher);
    v.At("total_slots") = JsonValue::MakeNumber(total_slots);
    v.At("name") = JsonValue::MakeString(name);
    v.At("subject_id") = JsonValue::MakeNumber(subject_id);
    v.At("is_lab") = JsonValue::MakeBool(is_lab);
    v.At("is_block") = JsonValue::MakeBool(is_block);
    JsonValue campuses = JsonValue::MakeArray();
    for (int campus : allowed_campuses) {
        campuses.array_value.push_back(JsonValue::MakeNumber(campus));
    }
    v.At("allowed_campuses") = campuses;
    return v;
}

JsonValue UnavailableJson(int id, int group, const std::string& from, const std::string& to) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("group") = JsonValue::MakeNumber(group);
    v.At("from") = JsonValue::MakeString(from);
    v.At("to") = JsonValue::MakeString(to);
    return v;
}


JsonValue UnavailableJsonWithText(int id, int group, const std::vector<std::string>& dates, const std::string& text, bool all_groups) {
    JsonValue v = JsonValue::MakeObject();
    v.At("id") = JsonValue::MakeNumber(id);
    v.At("all_groups") = JsonValue::MakeBool(all_groups);
    if (!all_groups) {
        v.At("group") = JsonValue::MakeNumber(group);
    }
    JsonValue dates_json = JsonValue::MakeArray();
    for (const std::string& date : dates) {
        dates_json.array_value.push_back(JsonValue::MakeString(date));
    }
    v.At("dates") = dates_json;
    v.At("text") = JsonValue::MakeString(text);
    return v;
}

std::vector<int> TargetGroupsForUnavailable(const JsonValue& item, const std::vector<GroupData>& groups) {
    std::vector<int> result;
    if (JsonBool(item, "all_groups", false)) {
        for (const GroupData& group : groups) {
            result.push_back(group.id);
        }
        return result;
    }

    int group = JsonInt(item, "group", -1);
    if (group >= 0) {
        result.push_back(group);
    }
    return result;
}

std::vector<Date> DatesFromUnavailableItem(const JsonValue& item) {
    std::vector<Date> dates;

    Date single;
    if (ParseDateIso(JsonString(item, "date", ""), single)) {
        dates.push_back(single);
    }

    const JsonValue& dates_json = item.At("dates");
    if (dates_json.IsArray()) {
        for (const JsonValue& value : dates_json.array_value) {
            if (!value.IsString()) continue;
            Date date;
            if (ParseDateIso(value.string_value, date)) {
                dates.push_back(date);
            }
        }
    }

    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
    return dates;
}

void AddTextForRange(std::map<Date, std::string>& texts, const Date& from, const Date& to, const std::string& text) {
    if (text.empty()) return;
    Date cur = from;
    while (cur <= to) {
        if (DayOfWeek(cur) != 7) {
            texts[cur] = text;
        }
        cur = NextDay(cur);
    }
}

std::vector<std::string> NamesFromGroups(const std::vector<GroupData>& groups) {
    int max_id = -1;
    for (const auto& group : groups) max_id = std::max(max_id, group.id);
    std::vector<std::string> names(max_id + 1);
    for (const auto& group : groups) {
        if (group.id >= 0) names[group.id] = group.name;
    }
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        if (names[i].empty()) names[i] = "Группа " + std::to_string(i);
    }
    return names;
}

std::vector<std::string> NamesFromTeachers(const std::vector<TeacherData>& teachers) {
    int max_id = -1;
    for (const auto& teacher : teachers) max_id = std::max(max_id, teacher.id);
    std::vector<std::string> names(max_id + 1);
    for (const auto& teacher : teachers) {
        if (teacher.id >= 0) names[teacher.id] = teacher.name;
    }
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        if (names[i].empty()) names[i] = "Преподаватель " + std::to_string(i);
    }
    return names;
}

}  // namespace

std::string DataFilePath() {
    return "data/timetable_data.json";
}

JsonValue DefaultDataJson() {
    JsonValue root = JsonValue::MakeObject();

    JsonValue settings = JsonValue::MakeObject();
    settings.At("start_date") = JsonValue::MakeString("2026-01-12");
    settings.At("end_date") = JsonValue::MakeString("2026-06-19");
    settings.At("solver_config") = SolverConfigToJson(DefaultSolverConfig());
    root.At("settings") = settings;

    JsonValue groups = JsonValue::MakeArray();
    groups.array_value.push_back(GroupJson(0, "ИСП-3304", 2));
    groups.array_value.push_back(GroupJson(1, "ИСП-3305п", 2));
    root.At("groups") = groups;

    JsonValue teachers = JsonValue::MakeArray();
    teachers.array_value.push_back(TeacherJson(0, "Новосёлова"));
    teachers.array_value.push_back(TeacherJson(1, "Давыдова"));
    teachers.array_value.push_back(TeacherJson(2, "Нуров"));
    teachers.array_value.push_back(TeacherJson(3, "Потапова"));
    teachers.array_value.push_back(TeacherJson(4, "Серянина"));
    teachers.array_value.push_back(TeacherJson(5, "Гобов"));
    teachers.array_value.push_back(TeacherJson(6, "Самцова"));
    teachers.array_value.push_back(TeacherJson(7, "Гарбузов"));
    root.At("teachers") = teachers;

    JsonValue unavailable = JsonValue::MakeArray();
    unavailable.array_value.push_back(UnavailableJson(0, 0, "2026-04-30", "2026-06-19"));
    unavailable.array_value.back().At("text") = JsonValue::MakeString("Производственная практика");
    unavailable.array_value.push_back(UnavailableJson(1, 1, "2026-04-30", "2026-06-19"));
    unavailable.array_value.back().At("text") = JsonValue::MakeString("Производственная практика");
    root.At("unavailable") = unavailable;

    JsonValue lessons = JsonValue::MakeArray();
    int id = 0;
    int subj = 0;
    int engId = subj++;

    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_NOVOSELOVA, 13, "Ин. язык", engId, false, false, {LESNAYA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_DAVYDOVA, 13, "Ин. язык", engId, false, false, {LESNAYA}));
    }

    for (int g = 0; g < 2; g++) lessons.array_value.push_back(LessonJson(id++, g, -1, T_NUROV, 14, "Физическая культура", -1, false, false, {LESNAYA, KRIVOUSOVA}));
    for (int g = 0; g < 2; g++) lessons.array_value.push_back(LessonJson(id++, g, -1, T_POTAPOVA, 17, "БЖД", -1, false, false, {LESNAYA, KRIVOUSOVA}));

    lessons.array_value.push_back(LessonJson(id++, 0, -1, T_SERYANINA, 12, "Экономика", -1, false, false, {LESNAYA, KRIVOUSOVA}));
    lessons.array_value.push_back(LessonJson(id++, 1, -1, T_GARBUZOV, 12, "Экономика", -1, false, false, {LESNAYA, KRIVOUSOVA}));

    int pmId = subj++;
    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_GARBUZOV, 8, "МДК.01.01 теория", pmId, false, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_GARBUZOV, 15, "МДК.01.01 КП", -1, false, false, {LESNAYA, KRIVOUSOVA}));
    }

    int dbId = subj++;
    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_SAMTSOVA, 36, "МДК.04.01 теория", dbId, false, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_SAMTSOVA, 36, "УП.04", -1, false, true, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_SAMTSOVA, 36, "УП.04", -1, false, true, {LESNAYA, KRIVOUSOVA}));
    }

    int autoId = subj++;
    for (int g = 0; g < 2; g++) {
        int bs = g * PARTS_PER_GROUP;
        lessons.array_value.push_back(LessonJson(id++, g, -1, T_GARBUZOV, 16, "ВМДК.05.01 теория", autoId, false, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs, T_GOBOV, 18, "УП.05", -1, false, true, {LESNAYA, KRIVOUSOVA}));
        lessons.array_value.push_back(LessonJson(id++, g, bs + 1, T_GOBOV, 18, "УП.05", -1, false, true, {LESNAYA, KRIVOUSOVA}));
    }
    root.At("lessons") = lessons;

    return root;
}

void EnsureDataFileExists() {
    std::filesystem::path path(DataFilePath());
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << ToJson(DefaultDataJson(), 2);
}

std::string ReadDataJsonText() {
    EnsureDataFileExists();
    std::ifstream in(DataFilePath(), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool SaveDataJson(const JsonValue& root, std::string& error) {
    std::filesystem::path path(DataFilePath());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "Не удалось открыть файл данных для записи";
        return false;
    }
    out << ToJson(root, 2);
    return true;
}

bool ParseDateIso(const std::string& text, Date& date) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    try {
        date.year = std::stoi(text.substr(0, 4));
        date.month = std::stoi(text.substr(5, 2));
        date.day = std::stoi(text.substr(8, 2));
        return date.month >= 1 && date.month <= 12 && date.day >= 1 && date.day <= 31;
    } catch (...) {
        return false;
    }
}

std::string DateToIso(const Date& date) {
    std::ostringstream ss;
    ss << date.year << "-";
    if (date.month < 10) ss << "0";
    ss << date.month << "-";
    if (date.day < 10) ss << "0";
    ss << date.day;
    return ss.str();
}

bool LoadScheduleInputData(ScheduleInputData& data, std::string& error) {
    EnsureDataFileExists();
    JsonParseResult parsed = ParseJson(ReadDataJsonText());
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }

    const JsonValue& root = parsed.value;
    if (!root.IsObject()) {
        error = "Файл данных должен быть JSON-объектом";
        return false;
    }

    const JsonValue& settings = root.At("settings");
    ParseDateIso(JsonString(settings, "start_date", "2026-01-12"), data.start_date);
    ParseDateIso(JsonString(settings, "end_date", "2026-06-19"), data.end_date);
    LoadSolverConfigFromJson(settings.At("solver_config"));

    data.groups.clear();
    const JsonValue& groups = root.At("groups");
    if (!groups.IsArray() || groups.array_value.empty()) {
        error = "Нужен непустой массив groups";
        return false;
    }
    for (const JsonValue& item : groups.array_value) {
        if (!item.IsObject()) continue;
        GroupData group;
        group.id = JsonInt(item, "id", static_cast<int>(data.groups.size()));
        group.name = JsonString(item, "name", "Группа " + std::to_string(group.id));
        group.parts = JsonInt(item, "parts", PARTS_PER_GROUP);
        data.groups.push_back(group);
    }

    data.teachers.clear();
    const JsonValue& teachers = root.At("teachers");
    if (!teachers.IsArray() || teachers.array_value.empty()) {
        error = "Нужен непустой массив teachers";
        return false;
    }
    for (const JsonValue& item : teachers.array_value) {
        if (!item.IsObject()) continue;
        TeacherData teacher;
        teacher.id = JsonInt(item, "id", static_cast<int>(data.teachers.size()));
        teacher.name = JsonString(item, "name", "Преподаватель " + std::to_string(teacher.id));
        data.teachers.push_back(teacher);
    }

    data.unavailable.clear();
    data.unavailable_day_texts.clear();
    data.special_days.clear();
    const JsonValue& unavailable = root.At("unavailable");
    if (unavailable.IsArray()) {
        for (const JsonValue& item : unavailable.array_value) {
            if (!item.IsObject()) continue;

            std::vector<int> target_groups = TargetGroupsForUnavailable(item, data.groups);
            std::string text = JsonString(item, "text", "");
            std::vector<Date> exact_dates = DatesFromUnavailableItem(item);

            SpecialDayData special_day;
            special_day.id = JsonInt(item, "id", static_cast<int>(data.special_days.size()));
            special_day.all_groups = JsonBool(item, "all_groups", false);
            special_day.group = JsonInt(item, "group", -1);
            special_day.text = text;
            special_day.dates = exact_dates;

            Date from;
            Date to;
            bool has_range = ParseDateIso(JsonString(item, "from", ""), from) &&
                             ParseDateIso(JsonString(item, "to", ""), to);

            for (int group : target_groups) {
                for (const Date& date : exact_dates) {
                    data.unavailable[group].push_back({date, date});
                    if (!text.empty()) {
                        data.unavailable_day_texts[group][date] = text;
                    }
                }

                if (has_range) {
                    data.unavailable[group].push_back({from, to});
                    AddTextForRange(data.unavailable_day_texts[group], from, to, text);
                }
            }

            if (!exact_dates.empty() || has_range) {
                if (has_range) {
                    Date cur = from;
                    while (cur <= to) {
                        if (DayOfWeek(cur) != 7) {
                            special_day.dates.push_back(cur);
                        }
                        cur = NextDay(cur);
                    }
                    std::sort(special_day.dates.begin(), special_day.dates.end());
                    special_day.dates.erase(std::unique(special_day.dates.begin(), special_day.dates.end()), special_day.dates.end());
                }
                data.special_days.push_back(special_day);
            }
        }
    }

    data.lessons.clear();
    const JsonValue& lessons = root.At("lessons");
    if (!lessons.IsArray()) {
        error = "Нужен массив lessons";
        return false;
    }
    for (const JsonValue& item : lessons.array_value) {
        if (!item.IsObject()) continue;
        Lesson lesson;
        lesson.id = JsonInt(item, "id", static_cast<int>(data.lessons.size()));
        lesson.group = JsonInt(item, "group", 0);
        lesson.subgroup = JsonInt(item, "subgroup", -1);
        lesson.teacher = JsonInt(item, "teacher", 0);
        lesson.total_slots = JsonInt(item, "total_slots", 1);
        lesson.name = JsonString(item, "name", "Занятие");
        lesson.subject_id = JsonInt(item, "subject_id", -1);
        lesson.is_lab = JsonBool(item, "is_lab", false);
        lesson.is_block = JsonBool(item, "is_block", false);
        lesson.allowed_campuses.clear();
        const JsonValue& campuses = item.At("allowed_campuses");
        if (campuses.IsArray()) {
            for (const JsonValue& campus : campuses.array_value) {
                if (!campus.IsNumber()) continue;
                int c = static_cast<int>(campus.number_value);
                if (c == LESNAYA || c == KRIVOUSOVA) lesson.allowed_campuses.insert(static_cast<Campus>(c));
            }
        }
        if (lesson.allowed_campuses.empty()) {
            lesson.allowed_campuses.insert(LESNAYA);
            lesson.allowed_campuses.insert(KRIVOUSOVA);
        }
        data.lessons.push_back(lesson);
    }

    SetRuntimeNames(NamesFromGroups(data.groups), NamesFromTeachers(data.teachers));
    return true;
}

int NextId(const JsonValue& array_value) {
    int next = 0;
    if (!array_value.IsArray()) return next;
    for (const JsonValue& item : array_value.array_value) {
        if (!item.IsObject()) continue;
        next = std::max(next, JsonInt(item, "id", -1) + 1);
    }
    return next;
}

JsonValue* FindObjectById(JsonValue& array_value, int id) {
    if (!array_value.IsArray()) return nullptr;
    for (JsonValue& item : array_value.array_value) {
        if (item.IsObject() && JsonInt(item, "id", -1) == id) return &item;
    }
    return nullptr;
}

bool RemoveObjectById(JsonValue& array_value, int id) {
    if (!array_value.IsArray()) return false;
    auto old_size = array_value.array_value.size();
    array_value.array_value.erase(
        std::remove_if(array_value.array_value.begin(), array_value.array_value.end(), [id](const JsonValue& item) {
            return item.IsObject() && JsonInt(item, "id", -1) == id;
        }),
        array_value.array_value.end()
    );
    return array_value.array_value.size() != old_size;
}

}  // namespace timetable

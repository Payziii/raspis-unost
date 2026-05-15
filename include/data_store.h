#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "json_utils.h"
#include "types.h"

namespace timetable {

struct GroupData {
    int id = 0;
    std::string name;
    int parts = 2;
};

struct TeacherData {
    int id = 0;
    std::string name;
};

struct SpecialDayData {
    int id = 0;
    bool all_groups = false;
    int group = -1;
    std::vector<Date> dates;
    std::string text;
};

struct ScheduleInputData {
    Date start_date{2026, 1, 12};
    Date end_date{2026, 6, 19};
    std::vector<GroupData> groups;
    std::vector<TeacherData> teachers;
    std::vector<Lesson> lessons;
    std::map<int, std::vector<std::pair<Date, Date>>> unavailable;
    std::map<int, std::map<Date, std::string>> unavailable_day_texts;
    std::vector<SpecialDayData> special_days;
};

std::string DataFilePath();
void EnsureDataFileExists();
JsonValue DefaultDataJson();

bool LoadScheduleInputData(ScheduleInputData& data, std::string& error);
bool SaveDataJson(const JsonValue& root, std::string& error);
std::string ReadDataJsonText();

bool ParseDateIso(const std::string& text, Date& date);
std::string DateToIso(const Date& date);

int NextId(const JsonValue& array_value);
JsonValue* FindObjectById(JsonValue& array_value, int id);
bool RemoveObjectById(JsonValue& array_value, int id);

}  // namespace timetable

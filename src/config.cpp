#include "config.h"

namespace timetable {

std::vector<std::string> GROUP_NAME = {
    "ИСП-3304",
    "ИСП-3305п"
};

std::vector<std::string> TEACHER_NAME = {
    "Новосёлова",
    "Давыдова",
    "Нуров",
    "Потапова",
    "Серянина",
    "Гобов",
    "Самцова",
    "Гарбузов"
};

const std::vector<std::string> WEEKDAY_NAME = {
    "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ", "ВС"
};

namespace {
const std::string kUnknownGroup = "Неизвестная группа";
const std::string kUnknownTeacher = "Неизвестный преподаватель";
}

int GroupCount() {
    return static_cast<int>(GROUP_NAME.size());
}

int TeacherCount() {
    return static_cast<int>(TEACHER_NAME.size());
}

const std::string& GroupName(int index) {
    if (index < 0 || index >= static_cast<int>(GROUP_NAME.size())) {
        return kUnknownGroup;
    }
    return GROUP_NAME[index];
}

const std::string& TeacherName(int index) {
    if (index < 0 || index >= static_cast<int>(TEACHER_NAME.size())) {
        return kUnknownTeacher;
    }
    return TEACHER_NAME[index];
}

void SetRuntimeNames(const std::vector<std::string>& groups, const std::vector<std::string>& teachers) {
    GROUP_NAME = groups;
    TEACHER_NAME = teachers;
}

}  // namespace timetable

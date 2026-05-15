#pragma once

#include <set>
#include <string>
#include <vector>

#include "ortools/sat/cp_model.h"

namespace timetable {

enum Campus {
    LESNAYA = 0,
    KRIVOUSOVA = 1
};

struct Date {
    int year;
    int month;
    int day;

    bool operator<(const Date& o) const {
        if (year != o.year) return year < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }

    bool operator==(const Date& o) const {
        return year == o.year && month == o.month && day == o.day;
    }

    bool operator<=(const Date& o) const {
        return *this < o || *this == o;
    }

    bool operator>(const Date& o) const {
        return o < *this;
    }

    bool operator>=(const Date& o) const {
        return !(*this < o);
    }
};

struct Lesson {
    int id;
    int group;
    int subgroup;       // -1 = вся группа, иначе 0/1 или 2/3
    int teacher;
    int total_slots;
    std::string name;
    int subject_id;     // для связки теория-лабы
    bool is_lab;
    bool is_block;      // true = УП, одно появление = 2 пары нагрузки
    std::set<Campus> allowed_campuses;
};

struct BlockInfo {
    int lesson_id;
    std::vector<int> possible_starts;
    std::vector<operations_research::sat::BoolVar> start_vars;
};

struct TimeInterval {
    int from_minute;
    int to_minute;
};

std::string SubjectFamilyKey(const Lesson& lesson);
bool LessonAffectsPart(const Lesson& lesson, int group, int part);

}  // namespace timetable

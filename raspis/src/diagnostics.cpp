#include "diagnostics.h"

#include <iostream>
#include <set>

#include "config.h"
#include "date_utils.h"

namespace timetable {

bool ValidateInputLessons(const std::vector<Lesson>& lessons) {
    bool ok = true;
    std::set<int> seen_ids;

    for (const auto& lesson : lessons) {
        if (!seen_ids.insert(lesson.id).second) {
            std::cerr << "Повторяется id занятия: " << lesson.id << "\n";
            ok = false;
        }

        if (lesson.group < 0 || lesson.group >= GROUPS) {
            std::cerr << "Некорректная группа у занятия: "
                      << lesson.name << ", group=" << lesson.group << "\n";
            ok = false;
        }

        if (lesson.teacher < 0 || lesson.teacher >= TEACHERS) {
            std::cerr << "Некорректный преподаватель у занятия: "
                      << lesson.name << ", teacher=" << lesson.teacher << "\n";
            ok = false;
        }

        if (lesson.total_slots <= 0) {
            std::cerr << "Некорректное число пар у занятия: "
                      << lesson.name << ", total_slots=" << lesson.total_slots << "\n";
            ok = false;
        }

        if (lesson.group >= 0 && lesson.group < GROUPS && lesson.subgroup != -1) {
            int base_subgroup = lesson.group * PARTS_PER_GROUP;
            int last_subgroup = base_subgroup + PARTS_PER_GROUP - 1;

            if (lesson.subgroup < base_subgroup || lesson.subgroup > last_subgroup) {
                std::cerr << "Некорректная подгруппа у занятия: "
                          << lesson.name
                          << ", group=" << GROUP_NAME[lesson.group]
                          << ", subgroup=" << lesson.subgroup
                          << ", ожидается -1 или диапазон "
                          << base_subgroup << ".." << last_subgroup << "\n";
                ok = false;
            }
        }

        if (lesson.allowed_campuses.empty()) {
            std::cerr << "У занятия нет разрешённых кампусов: "
                      << lesson.name << "\n";
            ok = false;
        }

        for (Campus campus : lesson.allowed_campuses) {
            if (campus != LESNAYA && campus != KRIVOUSOVA) {
                std::cerr << "Некорректный кампус у занятия: "
                          << lesson.name << "\n";
                ok = false;
            }
        }

        if (lesson.is_block) {
            if (lesson.total_slots % 2 != 0) {
                std::cerr << "Блоковое занятие должно иметь чётное число пар: "
                          << lesson.name
                          << ", группа " << GROUP_NAME[lesson.group]
                          << ", total_slots=" << lesson.total_slots << "\n";
                ok = false;
            }

            if (lesson.total_slots < 2) {
                std::cerr << "Блоковое занятие должно иметь минимум 2 пары: "
                          << lesson.name
                          << ", группа " << GROUP_NAME[lesson.group]
                          << "\n";
                ok = false;
            }
        }
    }

    return ok;
}

void PrintInputDiagnostics(
    const std::vector<Lesson>& lessons,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const Date& start_date
) {
    std::vector<int> group_load(GROUPS, 0);
    std::vector<int> teacher_load(TEACHERS, 0);

    int block_lessons = 0;
    int required_double_blocks = 0;

    for (const auto& lesson : lessons) {
        group_load[lesson.group] += lesson.total_slots;
        teacher_load[lesson.teacher] += lesson.total_slots;

        if (lesson.is_block) {
            block_lessons++;
            required_double_blocks += lesson.total_slots / 2;
        }
    }

    std::cout << "\n========== Диагностика входных данных ==========\n";
    std::cout << "Учебных дней: " << all_days.size() << "\n";
    std::cout << "Всего строк предметов после агрегации: " << lessons.size() << "\n";
    std::cout << "Агрегированных строк УП: " << block_lessons << "\n";
    std::cout << "Требуется двойных блоков УП: " << required_double_blocks << "\n\n";

    for (int g = 0; g < GROUPS; g++) {
        int available_days = 0;
        std::set<int> available_weeks;

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                available_days++;
                available_weeks.insert(WeekIndexFromStart(start_date, all_days[d]));
            }
        }

        std::cout << "Группа " << GROUP_NAME[g]
                  << ": нагрузка " << group_load[g]
                  << " назначений, доступно "
                  << available_days * SLOTS_PER_DAY
                  << " временных слотов, доступных недель примерно "
                  << available_weeks.size()
                  << "\n";
    }

    std::cout << "\nНагрузка преподавателей:\n";

    for (int t = 0; t < TEACHERS; t++) {
        std::cout << "  " << TEACHER_NAME[t]
                  << ": " << teacher_load[t]
                  << " пар\n";
    }

    std::cout << "================================================\n\n";
}

}  // namespace timetable

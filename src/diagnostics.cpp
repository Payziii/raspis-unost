#include "diagnostics.h"

#include <iostream>
#include <set>
#include <sstream>

#include "config.h"
#include "date_utils.h"

namespace timetable {

bool ValidateInputLessonsDetailed(const std::vector<Lesson>& lessons, std::vector<std::string>& errors) {
    errors.clear();
    std::set<int> seen_ids;

    auto push = [&errors](const std::string& msg) {
        errors.push_back(msg);
        std::cerr << msg << "\n";
    };

    for (const auto& lesson : lessons) {
        std::ostringstream prefix;
        prefix << "Занятие id=" << lesson.id << " «" << lesson.name << "»";

        if (!seen_ids.insert(lesson.id).second) {
            push("Повторяется id занятия: " + std::to_string(lesson.id));
        }

        if (lesson.group < 0 || lesson.group >= GROUPS) {
            std::ostringstream m;
            m << prefix.str() << ": некорректная группа group=" << lesson.group
              << " (всего групп: " << GROUPS << ")";
            push(m.str());
        }

        if (lesson.teacher < 0 || lesson.teacher >= TEACHERS) {
            std::ostringstream m;
            m << prefix.str() << ": некорректный преподаватель teacher=" << lesson.teacher
              << " (всего преподов: " << TEACHERS << ")";
            push(m.str());
        }

        if (lesson.total_slots <= 0) {
            std::ostringstream m;
            m << prefix.str() << ": некорректное число пар total_slots=" << lesson.total_slots;
            push(m.str());
        }

        if (lesson.group >= 0 && lesson.group < GROUPS && lesson.subgroup != -1) {
            int base_subgroup = lesson.group * PARTS_PER_GROUP;
            int last_subgroup = base_subgroup + PARTS_PER_GROUP - 1;

            if (lesson.subgroup < base_subgroup || lesson.subgroup > last_subgroup) {
                std::ostringstream m;
                m << prefix.str() << ": некорректная подгруппа subgroup=" << lesson.subgroup
                  << " (ожидается -1 или " << base_subgroup << ".." << last_subgroup
                  << " для группы «" << GROUP_NAME[lesson.group] << "»)";
                push(m.str());
            }
        }

        if (lesson.allowed_campuses.empty()) {
            push(prefix.str() + ": нет разрешённых кампусов");
        }

        for (Campus campus : lesson.allowed_campuses) {
            if (campus != LESNAYA && campus != KRIVOUSOVA) {
                push(prefix.str() + ": некорректный кампус");
            }
        }

        if (lesson.is_block) {
            if (lesson.total_slots % 2 != 0) {
                std::ostringstream m;
                m << prefix.str() << ": блоковое занятие должно иметь чётное число пар, total_slots="
                  << lesson.total_slots;
                push(m.str());
            }

            if (lesson.total_slots < 2) {
                push(prefix.str() + ": блоковое занятие должно иметь минимум 2 пары");
            }
        }
    }

    return errors.empty();
}

bool ValidateInputLessons(const std::vector<Lesson>& lessons) {
    std::vector<std::string> errors;
    return ValidateInputLessonsDetailed(lessons, errors);
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

#include "diagnostics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
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

        if (lesson.teacher != -1 && (lesson.teacher < 0 || lesson.teacher >= TEACHERS)) {
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
            if (lesson.total_slots < 1) {
                push(prefix.str() + ": блоковое занятие должно иметь минимум 1 двойной блок");
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
        if (lesson.teacher >= 0) teacher_load[lesson.teacher] += lesson.total_slots;

        if (lesson.is_block) {
            block_lessons++;
            required_double_blocks += lesson.total_slots;
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

    // ── Анализ плотности ──
    std::cout << "\n========== Анализ плотности (нагрузка / доступная ёмкость) ==========\n";

    struct DensityEntry {
        std::string name;
        int load = 0;
        int capacity = 0;
        double pct = 0.0;
    };

    auto pct_of = [](int load, int capacity) {
        return capacity > 0 ? (100.0 * load / capacity) : 999.0;
    };
    auto tag_of = [](double pct) -> const char* {
        if (pct > 100.0) return "  ⛔ НЕВОЗМОЖНО (нагрузка > ёмкости)";
        if (pct > 85.0) return "  ⚠ КРИТИЧНО";
        if (pct > 70.0) return "  ⚠ тесно";
        return "";
    };
    auto print_top = [&tag_of](std::vector<DensityEntry>& entries, const char* header, int limit) {
        std::sort(entries.begin(), entries.end(),
            [](const DensityEntry& a, const DensityEntry& b) { return a.pct > b.pct; });
        std::cout << "\n" << header << "\n";
        int shown = 0;
        for (const auto& e : entries) {
            if (shown >= limit && e.pct <= 70.0) break;
            std::cout << "  " << e.name << ": " << e.load << "/" << e.capacity
                      << " (" << std::fixed << std::setprecision(1) << e.pct << "%)"
                      << tag_of(e.pct) << "\n";
            shown++;
        }
    };

    // Доступные слоты по группе
    std::vector<int> group_capacity(GROUPS, 0);
    for (int g = 0; g < GROUPS; g++) {
        int available_days = 0;
        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (IsAvailable(all_days[d], g, unavailable)) available_days++;
        }
        group_capacity[g] = available_days * SLOTS_PER_DAY;
    }

    // Плотность по группам (нагрузка считается по «видимым» слотам:
    // занятия всей группы + занятия одной подгруппы делят один календарь)
    std::vector<int> group_visible_load(GROUPS, 0);
    std::vector<std::vector<int>> part_specific_load(GROUPS, std::vector<int>(PARTS_PER_GROUP, 0));
    std::vector<int> whole_group_load(GROUPS, 0);
    for (const auto& lesson : lessons) {
        if (lesson.group < 0 || lesson.group >= GROUPS) continue;
        if (lesson.subgroup == -1) {
            whole_group_load[lesson.group] += lesson.total_slots;
        } else {
            int part = lesson.subgroup - lesson.group * PARTS_PER_GROUP;
            if (part >= 0 && part < PARTS_PER_GROUP) {
                part_specific_load[lesson.group][part] += lesson.total_slots;
            }
        }
    }
    std::vector<DensityEntry> group_density;
    for (int g = 0; g < GROUPS; g++) {
        // Максимально «занятый» календарь одной подгруппы:
        // общие занятия + занятия её собственной подгруппы.
        int max_part = 0;
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            max_part = std::max(max_part, part_specific_load[g][p]);
        }
        int visible = whole_group_load[g] + max_part;
        group_visible_load[g] = visible;
        group_density.push_back({GROUP_NAME[g], visible, group_capacity[g], pct_of(visible, group_capacity[g])});
    }
    print_top(group_density, "Группы (видимая нагрузка подгруппы / доступные слоты):", 10);

    // Плотность по преподавателям (общий календарь, одна ёмкость = все учебные дни)
    int total_capacity = static_cast<int>(all_days.size()) * SLOTS_PER_DAY;
    std::vector<DensityEntry> teacher_density;
    for (int t = 0; t < TEACHERS; t++) {
        teacher_density.push_back({TEACHER_NAME[t], teacher_load[t], total_capacity, pct_of(teacher_load[t], total_capacity)});
    }
    print_top(teacher_density, "Преподаватели (нагрузка / все учебные слоты):", 10);

    // Сводка: сколько узких мест
    int crit_groups = 0, impossible_groups = 0;
    for (const auto& e : group_density) {
        if (e.pct > 100.0) impossible_groups++;
        else if (e.pct > 85.0) crit_groups++;
    }
    int crit_teachers = 0, impossible_teachers = 0;
    for (const auto& e : teacher_density) {
        if (e.pct > 100.0) impossible_teachers++;
        else if (e.pct > 85.0) crit_teachers++;
    }

    std::cout << "\n--- Сводка ---\n";
    std::cout << "Групп с переполнением (>100%): " << impossible_groups
              << ", критичных (>85%): " << crit_groups << "\n";
    std::cout << "Преподавателей с переполнением (>100%): " << impossible_teachers
              << ", критичных (>85%): " << crit_teachers << "\n";
    if (impossible_groups > 0 || impossible_teachers > 0) {
        std::cout << "⛔ Модель ГАРАНТИРОВАННО невозможна: где-то нагрузка превышает физическую ёмкость.\n";
        std::cout << "   Уменьши total_slots, расширь даты семестра или сними недоступные дни.\n";
    } else if (crit_groups > 0 || crit_teachers > 0) {
        std::cout << "⚠ Есть критично плотные сущности — окна и компактность почти невозможны, solver будет тяжёлым.\n";
    }

    // Оценка размера пространства поиска: сумма log10(C(avail, slots)) по урокам
    double log10_space = 0.0;
    for (const auto& lesson : lessons) {
        if (lesson.group < 0 || lesson.group >= GROUPS) continue;
        int a = group_capacity[lesson.group];
        int s = lesson.total_slots;
        if (s <= 0 || a <= 0 || s > a) continue;
        // log10 C(a, s) = sum_{i=1..s} log10((a - s + i) / i)
        for (int i = 1; i <= s; i++) {
            log10_space += std::log10(static_cast<double>(a - s + i) / static_cast<double>(i));
        }
    }
    std::cout << "\nВерхняя оценка пространства поиска (∏ C(доступно, нужно)): ~10^"
              << static_cast<long long>(log10_space) << "\n";
    std::cout << "(Это грубый потолок без учёта ограничений; реальное feasible-пространство несоизмеримо меньше.)\n";

    std::cout << "================================================\n\n";
}

}  // namespace timetable

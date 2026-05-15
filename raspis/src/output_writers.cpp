#include "output_writers.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "config.h"
#include "date_utils.h"
#include "format_utils.h"

namespace timetable {

using operations_research::sat::BoolVar;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::IntVar;

std::string BuildGroupSlotText(
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    int group,
    int day,
    int slot
) {
    int t = day * SLOTS_PER_DAY + slot;
    std::vector<std::string> items;

    int campus = IntValue(response, group_day_campus[group][day]);

    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].group != group) continue;

        if (BoolValue(response, x[l][t])) {
            std::ostringstream ss;
            std::string lesson_name = lessons[l].name;

            if (lessons[l].is_block) {
                std::string up_label = UpShiftLabelForDisplaySlot(all_days[day], slot);
                if (!up_label.empty()) {
                    lesson_name += " (" + up_label + ")";
                }
            }

            ss << lesson_name
               << " — " << SubgroupName(lessons[l].subgroup)
               << ", " << TEACHER_NAME[lessons[l].teacher]
               << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    if (items.empty()) {
        return "-";
    }

    return Join(items, " | ");
}

std::string BuildTeacherSlotText(
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<IntVar>>& teacher_day_campus,
    int teacher,
    int day,
    int slot
) {
    int t = day * SLOTS_PER_DAY + slot;
    std::vector<std::string> items;

    int campus = IntValue(response, teacher_day_campus[teacher][day]);
    TimeInterval pair_interval = PairSlotInterval(DayOfWeek(all_days[day]), slot);

    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].teacher != teacher) continue;
        if (lessons[l].is_block) continue;

        if (BoolValue(response, x[l][t])) {
            std::ostringstream ss;

            ss << GROUP_NAME[lessons[l].group]
               << ", " << lessons[l].name
               << " — " << SubgroupName(lessons[l].subgroup)
               << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    for (const auto& blk : blocks) {
        const Lesson& lesson = lessons[blk.lesson_id];

        if (lesson.teacher != teacher) {
            continue;
        }

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            if (!BoolValue(response, blk.start_vars[i])) {
                continue;
            }

            int start_t = blk.possible_starts[i];
            int start_day = start_t / SLOTS_PER_DAY;
            int start_slot = start_t % SLOTS_PER_DAY;

            if (start_day != day) {
                continue;
            }

            TimeInterval up_interval = UpIntervalForStartSlot(all_days[day], start_slot);

            if (!IntervalsOverlap(up_interval, pair_interval)) {
                continue;
            }

            std::ostringstream ss;
            ss << GROUP_NAME[lesson.group]
               << ", " << lesson.name
               << " (" << UpIntervalLabelForStartSlot(all_days[day], start_slot) << ")"
               << " — " << SubgroupName(lesson.subgroup)
               << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    if (items.empty()) {
        return "-";
    }

    return Join(items, " | ");
}

bool HasGroupDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& group_busy,
    int group,
    int day
) {
    for (int s = 0; s < SLOTS_PER_DAY; s++) {
        int t = day * SLOTS_PER_DAY + s;

        if (BoolValue(response, group_busy[group][t])) {
            return true;
        }
    }

    return false;
}

bool HasTeacherDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& teacher_busy,
    int teacher,
    int day
) {
    for (int s = 0; s < SLOTS_PER_DAY; s++) {
        int t = day * SLOTS_PER_DAY + s;

        if (BoolValue(response, teacher_busy[teacher][t])) {
            return true;
        }
    }

    return false;
}

void WriteGroupScheduleTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    int group
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Расписание группы " << GROUP_NAME[group] << "\n";
    out << "========================================\n\n";

    for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
        if (!HasGroupDay(response, group_busy, group, d)) {
            continue;
        }

        const Date& dt = all_days[d];

        out << DateToString(dt)
            << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            out << "  " << PairSlotLabel(dt, s) << ": "
                << BuildGroupSlotText(
                    response,
                    all_days,
                    lessons,
                    x,
                    group_day_campus,
                    group,
                    d,
                    s
                )
                << "\n";
        }

        out << "\n";
    }
}

void WriteAllGroupsTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Общее расписание групп\n";
    out << "======================\n\n";

    for (int g = 0; g < GROUPS; g++) {
        out << "\n\n========== " << GROUP_NAME[g] << " ==========\n\n";

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasGroupDay(response, group_busy, g, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            out << DateToString(dt)
                << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                out << "  " << PairSlotLabel(dt, s) << ": "
                    << BuildGroupSlotText(
                        response,
                        all_days,
                        lessons,
                        x,
                        group_day_campus,
                        g,
                        d,
                        s
                    )
                    << "\n";
            }

            out << "\n";
        }
    }
}

void WriteGroupsCsv(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << CsvEscape("Группа") << ";"
        << CsvEscape("Дата") << ";"
        << CsvEscape("День") << ";"
        << CsvEscape("Пара") << ";"
        << CsvEscape("Занятия") << "\n";

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasGroupDay(response, group_busy, g, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                std::string text = BuildGroupSlotText(
                    response,
                    all_days,
                    lessons,
                    x,
                    group_day_campus,
                    g,
                    d,
                    s
                );

                out << CsvEscape(GROUP_NAME[g]) << ";"
                    << CsvEscape(DateToString(dt)) << ";"
                    << CsvEscape(WEEKDAY_NAME[DayOfWeek(dt) - 1]) << ";"
                    << CsvEscape(PairSlotLabel(dt, s)) << ";"
                    << CsvEscape(text) << "\n";
            }
        }
    }
}

void WriteTeachersTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<BoolVar>>& teacher_busy,
    const std::vector<std::vector<IntVar>>& teacher_day_campus
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Расписание преподавателей\n";
    out << "=========================\n\n";

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        out << "\n\n========== " << TEACHER_NAME[teacher] << " ==========\n\n";

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasTeacherDay(response, teacher_busy, teacher, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            out << DateToString(dt)
                << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                out << "  " << PairSlotLabel(dt, s) << ": "
                    << BuildTeacherSlotText(
                        response,
                        all_days,
                        lessons,
                        x,
                        blocks,
                        teacher_day_campus,
                        teacher,
                        d,
                        s
                    )
                    << "\n";
            }

            out << "\n";
        }
    }
}

}  // namespace timetable

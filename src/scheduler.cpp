#include "scheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"

#include "config.h"
#include "data_store.h"
#include "date_utils.h"
#include "diagnostics.h"
#include "lessons_data.h"
#include "model_utils.h"
#include "output_writers.h"
#include "runtime_config.h"
#include "types.h"

namespace timetable {

using operations_research::Domain;
using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpModelProto;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverResponseStats;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::CpSolverStatus_Name;
using operations_research::sat::IntVar;
using operations_research::sat::LinearExpr;
using operations_research::sat::NewSatParameters;
using operations_research::sat::SatParameters;
using operations_research::sat::SolveCpModel;

struct UpStartRef {
    int block_index;
    int start_index;
    int teacher;
    int start_t;
    TimeInterval interval;
};

GenerationResult GenerateSchedule(const std::string& output_dir) {
    GenerationOptions empty_opts;
    return GenerateSchedule(output_dir, empty_opts);
}

GenerationResult GenerateSchedule(const std::string& output_dir, const GenerationOptions& options) {
    ScheduleInputData input_data;
    std::string input_error;
    if (!LoadScheduleInputData(input_data, input_error)) {
        return {false, "INPUT_ERROR", "Не удалось загрузить data/timetable_data.json: " + input_error, output_dir};
    }

    Date start_date = input_data.start_date;
    Date end_date = input_data.end_date;
    std::map<int, std::vector<std::pair<Date, Date>>> unavailable = input_data.unavailable;
    const auto& unavailable_day_texts = input_data.unavailable_day_texts;

    auto all_days = GenerateSchoolDays(start_date, end_date);

    int num_days = static_cast<int>(all_days.size());
    int total_slots = num_days * SLOTS_PER_DAY;

    // ── Weekly structure ─────────────────────────────────────────────────────
    // week_index[d] = raw sequential week number for day d
    std::vector<int> week_index(num_days);
    for (int d = 0; d < num_days; d++) {
        week_index[d] = WeekIndexFromStart(start_date, all_days[d]);
    }
    // Sorted unique week numbers → dense position index
    std::set<int> wk_set_tmp;
    for (int wi : week_index) wk_set_tmp.insert(wi);
    const std::vector<int> weeks_list(wk_set_tmp.begin(), wk_set_tmp.end());
    const int num_weeks = static_cast<int>(weeks_list.size());
    std::map<int, int> week_pos;   // raw week → position in weeks_list
    for (int i = 0; i < num_weeks; i++) week_pos[weeks_list[i]] = i;
    // All global slot indices grouped by week position
    std::vector<std::vector<int>> week_slots(num_weeks);
    for (int d = 0; d < num_days; d++) {
        int w = week_pos[week_index[d]];
        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            week_slots[w].push_back(d * SLOTS_PER_DAY + s);
        }
    }

    std::vector<Lesson> lessons = input_data.lessons;
    int num_lessons = static_cast<int>(lessons.size());

    std::vector<std::string> validation_errors;
    if (!ValidateInputLessonsDetailed(lessons, validation_errors)) {
        std::cerr << "\nВходные данные содержат ошибки (" << validation_errors.size() << "). Модель не построена.\n";
        std::string message = "Входные данные содержат ошибки (" + std::to_string(validation_errors.size()) + "): ";
        for (size_t i = 0; i < validation_errors.size() && i < 5; i++) {
            if (i > 0) message += " | ";
            message += validation_errors[i];
        }
        if (validation_errors.size() > 5) message += " | …";
        return {false, "INPUT_ERROR", message, output_dir};
    }

    std::filesystem::create_directories(output_dir);
    std::filesystem::create_directories(std::filesystem::path(output_dir) / "groups");

    PrintInputDiagnostics(lessons, all_days, unavailable, start_date);

    CpModelBuilder model;
    LinearExpr objective;

    std::vector<std::vector<BoolVar>> x(
        num_lessons,
        std::vector<BoolVar>(total_slots)
    );

    for (int l = 0; l < num_lessons; l++) {
        for (int t = 0; t < total_slots; t++) {
            x[l][t] = model.NewBoolVar();
        }
    }

    std::vector<BlockInfo> blocks;

    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_block) continue;

        BlockInfo bi;
        bi.lesson_id = l;

        for (int d = 0; d < num_days; d++) {
            if (!IsAvailable(all_days[d], lessons[l].group, unavailable)) {
                continue;
            }

            for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                if (!IsAllowedUpStartSlot(all_days[d], s)) {
                    continue;
                }

                int t = d * SLOTS_PER_DAY + s;
                bi.possible_starts.push_back(t);
            }
        }

        for (int i = 0; i < static_cast<int>(bi.possible_starts.size()); i++) {
            bi.start_vars.push_back(model.NewBoolVar());
        }

        blocks.push_back(bi);
    }

    // ── Per-lesson weekly quota via Bresenham distribution ─────────────────
    // For each lesson we know total_slots and how many "available weeks"
    // the group has. Quota for week w = Bresenham step, so that sum == total_slots.
    // Weeks where the group has zero available school days get quota 0 always.

    // group → sorted list of week positions that have ≥1 available day
    std::vector<std::vector<int>> group_avail_weeks(GROUPS);
    // group × week → list of available day indices in that week
    std::vector<std::map<int, std::vector<int>>> group_week_days(GROUPS);

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                int w = week_pos[week_index[d]];
                group_week_days[g][w].push_back(d);
            }
        }
        for (auto& kv : group_week_days[g]) {
            group_avail_weeks[g].push_back(kv.first);
        }
    }

    // Bresenham distributor: distribute `total` slots across `active_weeks`
    // Returns quota[i] for i-th active week.
    auto bresenham_quota = [](int total, int active_weeks) -> std::vector<int> {
        if (active_weeks <= 0) return {};
        std::vector<int> q(active_weeks, 0);
        int acc = 0;
        for (int i = 0; i < active_weeks; i++) {
            acc += total;
            int slots_here = acc / active_weeks;
            acc -= slots_here * active_weeks;
            q[i] = slots_here;
        }
        return q;
    };

    // lesson_week_quota[l][w] = how many placements of lesson l belong in week w
    // (w is a dense week position; groups with no available days get 0)
    std::vector<std::vector<int>> lesson_week_quota(num_lessons, std::vector<int>(num_weeks, 0));

    for (int l = 0; l < num_lessons; l++) {
        int g = lessons[l].group;
        const auto& avail_weeks = group_avail_weeks[g];
        int active = static_cast<int>(avail_weeks.size());
        if (active == 0) continue;
        auto q = bresenham_quota(lessons[l].total_slots, active);
        for (int i = 0; i < active; i++) {
            lesson_week_quota[l][avail_weeks[i]] = q[i];
        }
    }

    std::cout << "Недель в расписании: " << num_weeks << "\n";
    std::cout << "Дней в расписании: " << num_days << "\n";

    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].is_block) continue;

        LinearExpr sum;
        for (int t = 0; t < total_slots; t++) sum += x[l][t];
        model.AddEquality(sum, lessons[l].total_slots);
    }

    int total_block_start_vars = 0;

    for (auto& blk : blocks) {
        int l = blk.lesson_id;

        int required_starts = lessons[l].total_slots;
        total_block_start_vars += static_cast<int>(blk.start_vars.size());

        if (static_cast<int>(blk.possible_starts.size()) < required_starts) {
            std::cerr << "Недостаточно возможных стартов для блока: "
                      << lessons[l].name
                      << ", группа " << GROUP_NAME[lessons[l].group]
                      << ", доступно стартов " << blk.possible_starts.size()
                      << ", требуется " << required_starts
                      << "\n";
            return {false, "INPUT_ERROR", "Недостаточно возможных стартов для блока", output_dir};
        }

        // Global equality: exactly required_starts block starts total
        {
            LinearExpr start_sum;
            for (const auto& v : blk.start_vars) start_sum += v;
            model.AddEquality(start_sum, required_starts);
        }

        std::vector<std::vector<BoolVar>> covers(total_slots);

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int st = blk.possible_starts[i];

            covers[st].push_back(blk.start_vars[i]);
            covers[st + 1].push_back(blk.start_vars[i]);
        }

        for (int t = 0; t < total_slots; t++) {
            LinearExpr cover_sum;

            for (const auto& v : covers[t]) {
                cover_sum += v;
            }

            model.AddEquality(x[l][t], cover_sum);
        }
    }

    std::cout << "Агрегированных блоковых занятий УП: " << blocks.size() << "\n";
    std::cout << "Переменных старта УП после агрегации: "
              << total_block_start_vars << "\n";

    // ── Закрепление существующего расписания (lock_existing) ──
    if (!options.locked.empty()) {
        std::map<int, int> lesson_id_to_index;
        for (int l = 0; l < num_lessons; l++) {
            lesson_id_to_index[lessons[l].id] = l;
        }
        std::map<Date, int> date_to_day;
        for (int d = 0; d < num_days; d++) {
            date_to_day[all_days[d]] = d;
        }
        int applied = 0;
        int skipped_lesson = 0;
        int skipped_date = 0;
        int skipped_slot = 0;
        for (const LockedAssignment& a : options.locked) {
            auto lit = lesson_id_to_index.find(a.lesson_id);
            if (lit == lesson_id_to_index.end()) { skipped_lesson++; continue; }
            auto dit = date_to_day.find(a.date);
            if (dit == date_to_day.end()) { skipped_date++; continue; }
            if (a.slot < 0 || a.slot >= SLOTS_PER_DAY) { skipped_slot++; continue; }
            int t = dit->second * SLOTS_PER_DAY + a.slot;
            model.AddEquality(x[lit->second][t], 1);
            applied++;
        }
        std::cout << "Зафиксированных слотов (" << options.lock_source << "): " << applied
                  << " (пропущено: уроков " << skipped_lesson
                  << ", дат " << skipped_date
                  << ", слотов " << skipped_slot << ")\n";
    }

    for (int d = 0; d < num_days; d++) {
        for (int g = 0; g < GROUPS; g++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                continue;
            }

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;

                for (int l = 0; l < num_lessons; l++) {
                    if (lessons[l].group == g) {
                        model.AddEquality(x[l][t], 0);
                    }
                }
            }
        }
    }

    // ── ПП: разрешаем только последние ceil(total_slots/3) доступных дней группы ──
    {
        std::vector<std::vector<int>> group_avail_days(GROUPS);
        for (int g = 0; g < GROUPS; g++) {
            for (int d = 0; d < num_days; d++) {
                if (IsAvailable(all_days[d], g, unavailable)) {
                    group_avail_days[g].push_back(d);
                }
            }
        }

        for (int l = 0; l < num_lessons; l++) {
            if (!lessons[l].is_pp) continue;

            int g = lessons[l].group;
            int pp_days = (lessons[l].total_slots + 2) / 3;  // ceil(total_slots/3)
            const auto& avail = group_avail_days[g];

            if ((int)avail.size() <= pp_days) continue;

            int cutoff_idx = (int)avail.size() - pp_days;
            std::set<int> allowed_days(avail.begin() + cutoff_idx, avail.end());

            for (int d = 0; d < num_days; d++) {
                if (allowed_days.count(d)) continue;
                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    model.AddEquality(x[l][d * SLOTS_PER_DAY + s], 0);
                }
            }
        }
    }

    std::vector<std::vector<BoolVar>> group_busy(
        GROUPS,
        std::vector<BoolVar>(total_slots)
    );

    std::vector<std::vector<std::vector<BoolVar>>> part_busy(
        GROUPS,
        std::vector<std::vector<BoolVar>>(
            PARTS_PER_GROUP,
            std::vector<BoolVar>(total_slots)
        )
    );

    for (int g = 0; g < GROUPS; g++) {
        int base_subgroup = g * PARTS_PER_GROUP;

        for (int t = 0; t < total_slots; t++) {
            LinearExpr whole_sum;
            LinearExpr sub_sum[PARTS_PER_GROUP];

            for (int l = 0; l < num_lessons; l++) {
                if (lessons[l].group != g) continue;

                if (lessons[l].subgroup == -1) {
                    whole_sum += x[l][t];
                } else {
                    int part = lessons[l].subgroup - base_subgroup;

                    if (part >= 0 && part < PARTS_PER_GROUP) {
                        sub_sum[part] += x[l][t];
                    }
                }
            }

            model.AddLessOrEqual(whole_sum, 1);

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                model.AddLessOrEqual(sub_sum[p], 1);

                LinearExpr whole_plus_part;
                whole_plus_part += whole_sum;
                whole_plus_part += sub_sum[p];

                model.AddLessOrEqual(whole_plus_part, 1);
            }

            LinearExpr group_slot_sum;
            group_slot_sum += whole_sum;

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                group_slot_sum += sub_sum[p];
            }

            group_busy[g][t] = MakePositiveIndicator(model, group_slot_sum);

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                LinearExpr part_slot_sum;
                part_slot_sum += whole_sum;
                part_slot_sum += sub_sum[p];

                part_busy[g][p][t] = MakePositiveIndicator(model, part_slot_sum);
            }
        }
    }

    std::vector<std::vector<BoolVar>> student_entities;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            student_entities.push_back(part_busy[g][p]);
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                std::vector<BoolVar> up_starts;

                for (const auto& blk : blocks) {
                    const Lesson& lesson = lessons[blk.lesson_id];

                    if (!LessonAffectsPart(lesson, g, p)) {
                        continue;
                    }

                    for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                        int start_t = blk.possible_starts[i];

                        if (start_t / SLOTS_PER_DAY == d) {
                            up_starts.push_back(blk.start_vars[i]);
                        }
                    }
                }

                if (up_starts.empty()) {
                    continue;
                }

                LinearExpr up_start_sum;

                for (const auto& v : up_starts) {
                    up_start_sum += v;
                }

                model.AddLessOrEqual(up_start_sum, 1);

                LinearExpr part_day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    part_day_sum += part_busy[g][p][t];
                }

                BoolVar has_up = MakePositiveIndicator(model, up_start_sum);

                LinearExpr required_up_slots;
                required_up_slots += up_start_sum;
                required_up_slots += up_start_sum;

                model.AddEquality(part_day_sum, required_up_slots)
                    .OnlyEnforceIf(has_up);
            }
        }
    }

    std::vector<std::vector<BoolVar>> teacher_busy(
        TEACHERS,
        std::vector<BoolVar>(total_slots)
    );

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int t = 0; t < total_slots; t++) {
            LinearExpr sum;

            for (int l = 0; l < num_lessons; l++) {
                if (lessons[l].teacher == teacher) {
                    sum += x[l][t];
                }
            }

            model.AddLessOrEqual(sum, 1);

            teacher_busy[teacher][t] = MakePositiveIndicator(model, sum);
        }
    }

    for (const auto& blk : blocks) {
        int l = blk.lesson_id;
        int teacher = lessons[l].teacher;

        if (teacher < 0) continue;

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_t = blk.possible_starts[i];
            std::vector<int> blocked_slots = TeacherBlockedSlotsForUpStart(
                all_days,
                start_t
            );

            for (int blocked_t : blocked_slots) {
                LinearExpr ordinary_teacher_lessons;

                for (int other = 0; other < num_lessons; other++) {
                    if (lessons[other].is_block) {
                        continue;
                    }

                    if (lessons[other].teacher == teacher) {
                        ordinary_teacher_lessons += x[other][blocked_t];
                    }
                }

                model.AddEquality(ordinary_teacher_lessons, 0)
                    .OnlyEnforceIf(blk.start_vars[i]);
            }
        }
    }

    std::vector<UpStartRef> up_start_refs;

    for (int b = 0; b < static_cast<int>(blocks.size()); b++) {
        const BlockInfo& blk = blocks[b];
        const Lesson& lesson = lessons[blk.lesson_id];

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_t = blk.possible_starts[i];
            int day = start_t / SLOTS_PER_DAY;

            up_start_refs.push_back({
                b,
                i,
                lesson.teacher,
                start_t,
                UpIntervalForStartSlot(all_days[day], start_t % SLOTS_PER_DAY)
            });
        }
    }

    for (int a = 0; a < static_cast<int>(up_start_refs.size()); a++) {
        for (int b = a + 1; b < static_cast<int>(up_start_refs.size()); b++) {
            const UpStartRef& left = up_start_refs[a];
            const UpStartRef& right = up_start_refs[b];

            if (left.teacher < 0 || left.teacher != right.teacher) {
                continue;
            }

            if (left.start_t / SLOTS_PER_DAY != right.start_t / SLOTS_PER_DAY) {
                continue;
            }

            if (!IntervalsOverlap(left.interval, right.interval)) {
                continue;
            }

            LinearExpr both_up;
            both_up += blocks[left.block_index].start_vars[left.start_index];
            both_up += blocks[right.block_index].start_vars[right.start_index];

            model.AddLessOrEqual(both_up, 1);
        }
    }

    std::vector<std::vector<std::vector<BoolVar>>> student_day_has(
        GROUPS,
        std::vector<std::vector<BoolVar>>(
            PARTS_PER_GROUP,
            std::vector<BoolVar>(num_days)
        )
    );

    std::vector<BoolVar> student_five_pair_day_vars;

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            LinearExpr visible_group_day_sum;

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;
                visible_group_day_sum += group_busy[g][t];
            }

            model.AddLessOrEqual(visible_group_day_sum, MAX_STUDENT_PAIRS_PER_DAY);
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                LinearExpr day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    day_sum += part_busy[g][p][t];
                }

                BoolVar has = MakePositiveIndicator(model, day_sum);
                student_day_has[g][p][d] = has;

                AddMinIfPositive(
                    model,
                    day_sum,
                    has,
                    MIN_STUDENT_PAIRS_PER_STUDY_DAY
                );

                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY);

                BoolVar is_five_pair_day = model.NewBoolVar();
                model.AddEquality(day_sum, MAX_STUDENT_PAIRS_PER_DAY)
                    .OnlyEnforceIf(is_five_pair_day);
                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY - 1)
                    .OnlyEnforceIf(is_five_pair_day.Not());
                student_five_pair_day_vars.push_back(is_five_pair_day);
            }
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            model.AddEquality(student_day_has[g][0][d], student_day_has[g][1][d]);
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        std::map<int, std::vector<int>> week_days;

        for (int d = 0; d < num_days; d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                week_days[week_index[d]].push_back(d);
            }
        }

        for (const auto& item : week_days) {
            const std::vector<int>& days = item.second;
            if (days.empty()) {
                continue;
            }

            int required_days = std::min(
                MIN_STUDENT_STUDY_DAYS_PER_WEEK,
                static_cast<int>(days.size())
            );

            LinearExpr week_study_days;
            for (int d : days) {
                week_study_days += student_day_has[g][0][d];
            }

            if (HARD_MIN_STUDY_DAYS_PER_WEEK) {
                model.AddGreaterOrEqual(week_study_days, required_days);
            } else if (USE_QUALITY_OBJECTIVE) {
                IntVar missing = model.NewIntVar(Domain(0, required_days));
                LinearExpr week_with_missing;
                week_with_missing += week_study_days;
                week_with_missing += missing;

                model.AddGreaterOrEqual(week_with_missing, required_days);
                objective += missing * GROUP_WEEK_MISSING_DAY_WEIGHT;
            }
        }
    }

    if (HARD_MIN_2_TEACHER_PAIRS_PER_DAY) {
        for (int teacher = 0; teacher < TEACHERS; teacher++) {
            for (int d = 0; d < num_days; d++) {
                LinearExpr day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    day_sum += teacher_busy[teacher][t];
                }

                AddMin2IfPositive(model, day_sum);
            }
        }
    }

    if (USE_QUALITY_OBJECTIVE) {
        AddSubjectSpreadPenalties(
            model,
            lessons,
            x,
            all_days,
            unavailable,
            objective
        );
    }

    if (HARD_NO_STUDENT_WINDOWS) {
        AddNoWindowsHard(model, student_entities, num_days);
    }

    if (HARD_NO_TEACHER_WINDOWS) {
        AddNoWindowsHard(model, teacher_busy, num_days);
    }

    std::vector<std::vector<IntVar>> group_day_campus(
        GROUPS,
        std::vector<IntVar>(num_days)
    );

    std::vector<std::vector<IntVar>> teacher_day_campus(
        TEACHERS,
        std::vector<IntVar>(num_days)
    );

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            group_day_campus[g][d] = model.NewIntVar(Domain(0, 1));
        }
    }

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int d = 0; d < num_days; d++) {
            teacher_day_campus[teacher][d] = model.NewIntVar(Domain(0, 1));
        }
    }

    for (int l = 0; l < num_lessons; l++) {
        int group = lessons[l].group;
        int teacher = lessons[l].teacher;

        for (int d = 0; d < num_days; d++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;

                if (teacher >= 0) {
                    model.AddEquality(group_day_campus[group][d], teacher_day_campus[teacher][d])
                        .OnlyEnforceIf(x[l][t]);
                }

                if (lessons[l].allowed_campuses.size() == 1) {
                    int campus = static_cast<int>(*lessons[l].allowed_campuses.begin());

                    model.AddEquality(group_day_campus[group][d], campus)
                        .OnlyEnforceIf(x[l][t]);

                    if (teacher >= 0) {
                        model.AddEquality(teacher_day_campus[teacher][d], campus)
                            .OnlyEnforceIf(x[l][t]);
                    }
                }
            }
        }
    }

    if (USE_QUALITY_OBJECTIVE) {
        for (const auto& v : student_five_pair_day_vars) {
            objective += v * STUDENT_FIVE_PAIR_DAY_WEIGHT;
        }

        if (!HARD_NO_STUDENT_WINDOWS && OPTIMIZE_STUDENT_WINDOWS) {
            std::vector<BoolVar> student_gaps =
                CreateWindowPenaltyVars(model, student_entities, num_days);

            for (const auto& gap : student_gaps) {
                objective += gap * STUDENT_WINDOW_WEIGHT;
            }
        }

        if (!HARD_NO_TEACHER_WINDOWS && OPTIMIZE_TEACHER_WINDOWS) {
            std::vector<BoolVar> teacher_gaps =
                CreateWindowPenaltyVars(model, teacher_busy, num_days);

            for (const auto& gap : teacher_gaps) {
                objective += gap * TEACHER_WINDOW_WEIGHT;
            }
        }

        if (STUDENT_LATE_SLOT_WEIGHT > 0) {
            for (const auto& busy : student_entities) {
                for (int d = 0; d < num_days; d++) {
                    int base = d * SLOTS_PER_DAY;

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        objective += busy[base + s] * (s * STUDENT_LATE_SLOT_WEIGHT);
                    }
                }
            }
        }

        if (TEACHER_LATE_SLOT_WEIGHT > 0) {
            for (int teacher = 0; teacher < TEACHERS; teacher++) {
                for (int d = 0; d < num_days; d++) {
                    int base = d * SLOTS_PER_DAY;

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        objective += teacher_busy[teacher][base + s] * (s * TEACHER_LATE_SLOT_WEIGHT);
                    }
                }
            }
        }

        model.Minimize(objective);
    }

    long long est_x_vars = static_cast<long long>(num_lessons) * total_slots;
    long long est_group_busy = static_cast<long long>(GROUPS) * total_slots;
    long long est_part_busy = static_cast<long long>(GROUPS) * PARTS_PER_GROUP * total_slots;
    long long est_teacher_busy = static_cast<long long>(TEACHERS) * total_slots;
    long long est_block_starts = total_block_start_vars;
    long long est_student_day_has = static_cast<long long>(GROUPS) * PARTS_PER_GROUP * num_days;
    long long est_five_pair = static_cast<long long>(student_five_pair_day_vars.size());
    long long est_campus_int = static_cast<long long>(GROUPS) * num_days + static_cast<long long>(TEACHERS) * num_days;

    std::cout << "\n========== Категории переменных (предварительная оценка) ==========\n";
    std::cout << "x[lesson][slot]      : " << est_x_vars << "  (" << num_lessons << " уроков × " << total_slots << " слотов)\n";
    std::cout << "group_busy           : " << est_group_busy << "\n";
    std::cout << "part_busy            : " << est_part_busy << "\n";
    std::cout << "teacher_busy         : " << est_teacher_busy << "\n";
    std::cout << "block start_vars     : " << est_block_starts << "\n";
    std::cout << "student_day_has      : " << est_student_day_has << "\n";
    std::cout << "five_pair_day_vars   : " << est_five_pair << "\n";
    std::cout << "*_day_campus (Int)   : " << est_campus_int << "\n";
    std::cout << "ИТОГО (булевых +- )  : "
              << (est_x_vars + est_group_busy + est_part_busy + est_teacher_busy + est_block_starts + est_student_day_has + est_five_pair)
              << "\n";

    std::cout << "\nЗапуск решателя...\n";

    CpModelProto model_proto = model.Build();

    std::cout << "\n========== Размер модели (фактический) ==========\n";
    std::cout << "Всего переменных     : " << model_proto.variables_size() << "\n";
    std::cout << "Всего ограничений    : " << model_proto.constraints_size() << "\n";
    std::cout << "Размер proto         : " << (model_proto.ByteSizeLong() / (1024.0 * 1024.0)) << " МБ\n";
    std::cout << "Параметры солвера    : workers=" << SOLVER_WORKERS
              << ", time_limit=" << SOLVER_TIME_LIMIT_SECONDS << "s"
              << ", quality_obj=" << (USE_QUALITY_OBJECTIVE ? "true" : "false")
              << ", hard_no_student_windows=" << (HARD_NO_STUDENT_WINDOWS ? "true" : "false")
              << ", stop_first=" << (STOP_AFTER_FIRST_SOLUTION ? "true" : "false") << "\n";

    SatParameters params;
    params.set_num_search_workers(SOLVER_WORKERS);
    params.set_max_time_in_seconds(SOLVER_TIME_LIMIT_SECONDS);
    params.set_random_seed(g_solver_config.random_seed);
    params.set_max_memory_in_mb(SOLVER_MAX_MEMORY_MB);
    params.set_linearization_level(g_solver_config.linearization_level);
    params.set_symmetry_level(g_solver_config.symmetry_level);

    // Realtime-логи поиска: solver сам печатает прогресс каждые ~5 сек.
    params.set_log_search_progress(true);
    params.set_log_subsolver_statistics(true);
    params.set_log_to_stdout(true);

    if (STOP_AFTER_FIRST_SOLUTION) {
        params.set_stop_after_first_solution(true);
    }

    std::cout << "Random seed: " << g_solver_config.random_seed
              << ", linearization_level: " << g_solver_config.linearization_level
              << ", symmetry_level: " << g_solver_config.symmetry_level << "\n\n";

    operations_research::sat::Model sat_model;
    sat_model.Add(NewSatParameters(params));

    // Колбэк на каждое найденное feasible-решение — печатает время и objective.
    int solution_counter = 0;
    auto solve_start = std::chrono::steady_clock::now();
    sat_model.Add(operations_research::sat::NewFeasibleSolutionObserver(
        [&solution_counter, &solve_start](const CpSolverResponse& r) {
            solution_counter++;
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - solve_start).count();
            std::cout << "\n>>> [Решение #" << solution_counter << "] найдено за "
                      << std::fixed << std::setprecision(2) << elapsed << " сек";
            std::cout << ", objective=" << r.objective_value()
                      << ", bound=" << r.best_objective_bound();
            std::cout << " <<<\n" << std::flush;
        }
    ));

    CpSolverResponse response = SolveCpModel(model_proto, &sat_model);

    std::cout << "\n========== Результат ==========\n";
    std::cout << "Status: " << CpSolverStatus_Name(response.status()) << "\n";
    std::cout << CpSolverResponseStats(response) << "\n";

    if (response.status() == CpSolverStatus::OPTIMAL ||
        response.status() == CpSolverStatus::FEASIBLE) {

        int student_windows = CountWindows(response, student_entities, num_days);
        int teacher_windows = CountWindows(response, teacher_busy, num_days);
        int max_student_pairs = MaxStudentPairsInDay(response, part_busy, num_days);
        int five_pair_days = CountFivePairStudentDays(response, part_busy, num_days);
        int up_day_violations = CountUpDayRuleViolations(
            response,
            lessons,
            x,
            part_busy,
            num_days
        );
        int up_teacher_lock_violations = CountUpTeacherLockViolations(
            response,
            lessons,
            x,
            blocks,
            all_days
        );

        std::cout << "\nРасписание найдено.\n";
        std::cout << "Окон у студентов: " << student_windows << "\n";
        std::cout << "Окон у преподавателей: " << teacher_windows << "\n";
        std::cout << "Максимум пар у студента за день: " << max_student_pairs << "\n";
        std::cout << "Дней по 5 пар у подгрупп: " << five_pair_days << "\n";
        std::cout << "Нарушений правила УП-день: " << up_day_violations << "\n";
        std::cout << "Нарушений занятости преподавателя во время УП: "
                  << up_teacher_lock_violations << "\n";

        if (USE_QUALITY_OBJECTIVE) {
            std::cout << "Objective value: " << response.objective_value() << "\n";
            std::cout << "Best bound: " << response.best_objective_bound() << "\n";
        }

        const std::filesystem::path out_dir(output_dir);
        const std::filesystem::path groups_dir = out_dir / "groups";

        WriteAllGroupsTxt(
            (out_dir / "raspisanie_all.txt").string(),
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            unavailable_day_texts
        );

        for (int g = 0; g < GROUPS; g++) {
            WriteGroupScheduleTxt(
                (groups_dir / ("raspisanie_group_" + std::to_string(g) + ".txt")).string(),
                response,
                all_days,
                lessons,
                x,
                group_busy,
                group_day_campus,
                unavailable_day_texts,
                g
            );
        }

        WriteGroupsCsv(
            (out_dir / "raspisanie_groups.csv").string(),
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            unavailable_day_texts
        );

        WriteTeachersTxt(
            (out_dir / "raspisanie_teachers.txt").string(),
            response,
            all_days,
            lessons,
            x,
            blocks,
            teacher_busy,
            teacher_day_campus
        );

        WriteAllGroupsJson(
            (out_dir / "schedule_all.json").string(),
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            unavailable_day_texts
        );

        for (int g = 0; g < GROUPS; g++) {
            WriteGroupJson(
                (groups_dir / ("group_" + std::to_string(g) + ".json")).string(),
                response,
                all_days,
                lessons,
                x,
                group_busy,
                group_day_campus,
                unavailable_day_texts,
                g
            );
        }

        std::cout << "\nФайлы созданы:\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "raspisanie_all.txt").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "schedule_all.json").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "groups" / "group_*.json").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "raspisanie_groups.csv").string() << "\n";
        std::cout << "  " << (std::filesystem::path(output_dir) / "raspisanie_teachers.txt").string() << "\n";

        return {true, CpSolverStatus_Name(response.status()), "Расписание найдено", output_dir};

    } else if (response.status() == CpSolverStatus::INFEASIBLE) {
        std::cout << "\nМодель противоречива.\n";
        std::cout << "Что можно ослабить первым:\n";
        std::cout << "  1) HARD_NO_STUDENT_WINDOWS = false\n";
        std::cout << "  2) MIN_STUDENT_STUDY_DAYS_PER_WEEK = 1\n";
        std::cout << "  3) увеличить SUBJECT_BUCKET_EXTRA_SLOTS до 3 или 4\n";
        std::cout << "  4) уменьшить MIN_SUBJECT_SPREAD_TOTAL_SLOTS\n";
        std::cout << "  5) HARD_MIN_2_TEACHER_PAIRS_PER_DAY оставить false\n";
        std::cout << "  6) STRICT_ALL_THEORY_BEFORE_LABS оставить false\n";
        std::cout << "  7) если УП-дни слишком жёсткие, проверь правило "
                  << "student_day_has[g][0][d] == student_day_has[g][1][d]\n";
        return {false, CpSolverStatus_Name(response.status()), "Модель противоречива", output_dir};
    } else if (response.status() == CpSolverStatus::UNKNOWN) {
        std::cout << "\nРешатель не успел найти или доказать решение за лимит времени.\n";
        std::cout << "Что можно сделать:\n";
        std::cout << "  1) увеличить SOLVER_TIME_LIMIT_SECONDS\n";
        std::cout << "  2) уменьшить SOLVER_WORKERS до 2, если не хватает ОЗУ\n";
        std::cout << "  3) увеличить SUBJECT_BUCKET_EXTRA_SLOTS до 3 или 4\n";
        std::cout << "  4) временно поставить HARD_NO_STUDENT_WINDOWS = false\n";
        return {false, CpSolverStatus_Name(response.status()), "Решатель не успел найти или доказать решение за лимит времени", output_dir};
    } else if (response.status() == CpSolverStatus::MODEL_INVALID) {
        std::cout << "\nМодель некорректна. Проверь CpSolverResponseStats выше.\n";
        return {false, CpSolverStatus_Name(response.status()), "Модель некорректна", output_dir};
    } else {
        std::cout << "\nРешение не найдено. Статус: "
                  << CpSolverStatus_Name(response.status())
                  << "\n";
        return {false, CpSolverStatus_Name(response.status()), "Решение не найдено", output_dir};
    }

    return {false, "UNKNOWN", "Решение не найдено", output_dir};
}

// ─────────────────────────────────────────────────────────────────────────────
// Weekly generation: отдельная маленькая CP-SAT задача на каждую неделю.
// Квоты (сколько пар/стартов у каждого занятия в конкретную неделю) берутся
// из алгоритма Брезенхема, поэтому сумма квот == total_slots по каждому уроку.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct WeekSolveResult {
    bool success = false;
    std::string status;
    // x_vals[l][local_slot] — 0/1; для уроков с quota==0 не заполняется (всё 0)
    std::vector<std::vector<int>> x_vals;
};

// Реконструирует и записывает все файлы расписания из flat-массива global_x_vals.
// Вызывается как после каждой недели (частичное), так и в конце (итоговое).
// print_stats=true — печатает статистику (окна, нарушения и т.д.).
static bool WriteScheduleFiles(
    const std::string& output_dir,
    const std::vector<std::vector<int>>& global_x_vals,
    int num_days,
    const std::vector<Lesson>& lessons,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const std::map<int, std::map<Date, std::string>>& unavailable_day_texts,
    bool print_stats
) {
    using operations_research::Domain;
    using operations_research::sat::BoolVar;
    using operations_research::sat::CpModelBuilder;
    using operations_research::sat::CpModelProto;
    using operations_research::sat::CpSolverResponse;
    using operations_research::sat::CpSolverStatus;
    using operations_research::sat::IntVar;
    using operations_research::sat::LinearExpr;
    using operations_research::sat::NewSatParameters;
    using operations_research::sat::SatParameters;
    using operations_research::sat::SolveCpModel;

    const int num_lessons = static_cast<int>(lessons.size());
    const int total_slots = num_days * SLOTS_PER_DAY;

    // Производные busy-значения
    std::vector<std::vector<int>> gbusy_v(GROUPS, std::vector<int>(total_slots, 0));
    std::vector<std::vector<std::vector<int>>> pbusy_v(
        GROUPS, std::vector<std::vector<int>>(PARTS_PER_GROUP, std::vector<int>(total_slots, 0))
    );
    std::vector<std::vector<int>> tbusy_v(TEACHERS, std::vector<int>(total_slots, 0));

    for (int l = 0; l < num_lessons; l++) {
        int g = lessons[l].group;
        int teacher = lessons[l].teacher;
        int base_sg = g * PARTS_PER_GROUP;
        for (int t = 0; t < total_slots; t++) {
            if (!global_x_vals[l][t]) continue;
            gbusy_v[g][t] = 1;
            if (teacher >= 0) tbusy_v[teacher][t] = 1;
            if (lessons[l].subgroup == -1) {
                for (int p = 0; p < PARTS_PER_GROUP; p++) pbusy_v[g][p][t] = 1;
            } else {
                int part = lessons[l].subgroup - base_sg;
                if (part >= 0 && part < PARTS_PER_GROUP) pbusy_v[g][part][t] = 1;
            }
        }
    }

    // Кампусы
    std::vector<std::vector<int>> g_campus_v(GROUPS, std::vector<int>(num_days, 0));
    std::vector<std::vector<int>> t_campus_v(TEACHERS, std::vector<int>(num_days, 0));
    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].allowed_campuses.size() != 1) continue;
        int campus = static_cast<int>(*lessons[l].allowed_campuses.begin());
        int g = lessons[l].group;
        int teacher = lessons[l].teacher;
        for (int d = 0; d < num_days; d++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                if (global_x_vals[l][d * SLOTS_PER_DAY + s]) {
                    g_campus_v[g][d] = campus;
                    if (teacher >= 0) t_campus_v[teacher][d] = campus;
                }
            }
        }
    }

    // Блоки для WriteTeachersTxt
    std::vector<BlockInfo> rec_blocks;
    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_block) continue;
        BlockInfo bi;
        bi.lesson_id = l;
        for (int d = 0; d < num_days; d++) {
            if (!IsAvailable(all_days[d], lessons[l].group, unavailable)) continue;
            for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                if (!IsAllowedUpStartSlot(all_days[d], s)) continue;
                bi.possible_starts.push_back(d * SLOTS_PER_DAY + s);
            }
        }
        rec_blocks.push_back(bi);
    }

    // Тривиальная фиксирующая модель
    CpModelBuilder fix_model;

    std::vector<std::vector<BoolVar>> fx(num_lessons, std::vector<BoolVar>(total_slots));
    for (int l = 0; l < num_lessons; l++)
        for (int t = 0; t < total_slots; t++) {
            fx[l][t] = fix_model.NewBoolVar();
            fix_model.AddEquality(fx[l][t], global_x_vals[l][t]);
        }

    for (auto& blk : rec_blocks) {
        int l = blk.lesson_id;
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++)
            blk.start_vars.push_back(fix_model.NewBoolVar());
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int st = blk.possible_starts[i];
            int val = (global_x_vals[l][st] && global_x_vals[l][st + 1]) ? 1 : 0;
            fix_model.AddEquality(blk.start_vars[i], val);
        }
    }

    std::vector<std::vector<BoolVar>> fgb(GROUPS, std::vector<BoolVar>(total_slots));
    std::vector<std::vector<std::vector<BoolVar>>> fpb(
        GROUPS, std::vector<std::vector<BoolVar>>(PARTS_PER_GROUP, std::vector<BoolVar>(total_slots))
    );
    std::vector<std::vector<BoolVar>> ftb(TEACHERS, std::vector<BoolVar>(total_slots));

    for (int g = 0; g < GROUPS; g++)
        for (int t = 0; t < total_slots; t++) {
            fgb[g][t] = fix_model.NewBoolVar();
            fix_model.AddEquality(fgb[g][t], gbusy_v[g][t]);
        }
    for (int g = 0; g < GROUPS; g++)
        for (int p = 0; p < PARTS_PER_GROUP; p++)
            for (int t = 0; t < total_slots; t++) {
                fpb[g][p][t] = fix_model.NewBoolVar();
                fix_model.AddEquality(fpb[g][p][t], pbusy_v[g][p][t]);
            }
    for (int teacher = 0; teacher < TEACHERS; teacher++)
        for (int t = 0; t < total_slots; t++) {
            ftb[teacher][t] = fix_model.NewBoolVar();
            fix_model.AddEquality(ftb[teacher][t], tbusy_v[teacher][t]);
        }

    std::vector<std::vector<IntVar>> fgdc(GROUPS, std::vector<IntVar>(num_days));
    std::vector<std::vector<IntVar>> ftdc(TEACHERS, std::vector<IntVar>(num_days));
    for (int g = 0; g < GROUPS; g++)
        for (int d = 0; d < num_days; d++) {
            fgdc[g][d] = fix_model.NewIntVar(Domain(0, 1));
            fix_model.AddEquality(fgdc[g][d], g_campus_v[g][d]);
        }
    for (int teacher = 0; teacher < TEACHERS; teacher++)
        for (int d = 0; d < num_days; d++) {
            ftdc[teacher][d] = fix_model.NewIntVar(Domain(0, 1));
            fix_model.AddEquality(ftdc[teacher][d], t_campus_v[teacher][d]);
        }

    SatParameters fix_params;
    fix_params.set_num_search_workers(1);
    fix_params.set_max_time_in_seconds(10.0);
    fix_params.set_stop_after_first_solution(true);

    operations_research::sat::Model fix_sat;
    fix_sat.Add(NewSatParameters(fix_params));
    CpModelProto fix_proto = fix_model.Build();
    CpSolverResponse fix_resp = SolveCpModel(fix_proto, &fix_sat);

    if (fix_resp.status() != CpSolverStatus::OPTIMAL &&
        fix_resp.status() != CpSolverStatus::FEASIBLE) {
        std::cerr << "WriteScheduleFiles: reconstruction failed\n";
        return false;
    }

    if (print_stats) {
        std::vector<std::vector<BoolVar>> student_ents;
        for (int g = 0; g < GROUPS; g++)
            for (int p = 0; p < PARTS_PER_GROUP; p++)
                student_ents.push_back(fpb[g][p]);

        std::cout << "\nСтатистика расписания:\n";
        std::cout << "  Окон у студентов: " << CountWindows(fix_resp, student_ents, num_days) << "\n";
        std::cout << "  Окон у преподавателей: " << CountWindows(fix_resp, ftb, num_days) << "\n";
        std::cout << "  Макс. пар у студента в день: " << MaxStudentPairsInDay(fix_resp, fpb, num_days) << "\n";
        std::cout << "  Дней по 5 пар у подгрупп: " << CountFivePairStudentDays(fix_resp, fpb, num_days) << "\n";
        std::cout << "  Нарушений правила УП-день: " << CountUpDayRuleViolations(fix_resp, lessons, fx, fpb, num_days) << "\n";
        std::cout << "  Нарушений занятости препод. во время УП: "
                  << CountUpTeacherLockViolations(fix_resp, lessons, fx, rec_blocks, all_days) << "\n";
    }

    const std::filesystem::path out_dir(output_dir);
    const std::filesystem::path groups_dir = out_dir / "groups";
    std::filesystem::create_directories(out_dir);
    std::filesystem::create_directories(groups_dir);

    WriteAllGroupsTxt(
        (out_dir / "raspisanie_all.txt").string(),
        fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts);

    for (int g = 0; g < GROUPS; g++) {
        WriteGroupScheduleTxt(
            (groups_dir / ("raspisanie_group_" + std::to_string(g) + ".txt")).string(),
            fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts, g);
    }

    WriteGroupsCsv(
        (out_dir / "raspisanie_groups.csv").string(),
        fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts);

    WriteTeachersTxt(
        (out_dir / "raspisanie_teachers.txt").string(),
        fix_resp, all_days, lessons, fx, rec_blocks, ftb, ftdc);

    WriteAllGroupsJson(
        (out_dir / "schedule_all.json").string(),
        fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts);

    for (int g = 0; g < GROUPS; g++) {
        WriteGroupJson(
            (groups_dir / ("group_" + std::to_string(g) + ".json")).string(),
            fix_resp, all_days, lessons, fx, fgb, fgdc, unavailable_day_texts, g);
    }

    return true;
}

static WeekSolveResult SolveOneWeek(
    int week_num,
    const std::vector<int>& wdix,    // индексы в all_days для дней этой недели
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const std::vector<int>& quotas,  // quotas[l] = кол-во пар/стартов в эту неделю
    const std::vector<std::vector<int>>& pp_allowed_global,  // [g] -> set of global day idx
    const std::vector<LockedAssignment>& locked
) {
    using operations_research::Domain;
    using operations_research::sat::BoolVar;
    using operations_research::sat::CpModelBuilder;
    using operations_research::sat::CpModelProto;
    using operations_research::sat::CpSolverResponse;
    using operations_research::sat::CpSolverStatus;
    using operations_research::sat::CpSolverStatus_Name;
    using operations_research::sat::IntVar;
    using operations_research::sat::LinearExpr;
    using operations_research::sat::NewSatParameters;
    using operations_research::sat::SatParameters;
    using operations_research::sat::SolveCpModel;
    using operations_research::sat::SolutionIntegerValue;

    const int num_lessons = static_cast<int>(lessons.size());
    const int W = static_cast<int>(wdix.size());
    const int local_slots = W * SLOTS_PER_DAY;

    // Дни недели (локальный индекс → Date)
    std::vector<Date> week_days;
    week_days.reserve(W);
    for (int gd : wdix) week_days.push_back(all_days[gd]);

    // pp_allowed_global[g] хранится как вектор; переводим в set для быстрого поиска
    std::vector<std::set<int>> pp_allowed(GROUPS);
    for (int g = 0; g < GROUPS; g++) {
        for (int gd : pp_allowed_global[g]) pp_allowed[g].insert(gd);
    }

    CpModelBuilder model;
    LinearExpr objective;

    // x[l][lt] — только для уроков с quota > 0
    std::vector<std::vector<BoolVar>> x(num_lessons, std::vector<BoolVar>(local_slots));
    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0) continue;
        for (int lt = 0; lt < local_slots; lt++) x[l][lt] = model.NewBoolVar();
    }

    // ── Блочные (УП) уроки ─────────────────────────────────────────────────
    struct LocalBlockInfo {
        int lesson_id;
        std::vector<int> possible_starts;
        std::vector<BoolVar> start_vars;
    };
    std::vector<LocalBlockInfo> blocks;

    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_block || quotas[l] == 0) continue;

        LocalBlockInfo bi;
        bi.lesson_id = l;

        for (int ld = 0; ld < W; ld++) {
            if (!IsAvailable(week_days[ld], lessons[l].group, unavailable)) continue;
            for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                if (!IsAllowedUpStartSlot(week_days[ld], s)) continue;
                bi.possible_starts.push_back(ld * SLOTS_PER_DAY + s);
            }
        }
        for (int i = 0; i < static_cast<int>(bi.possible_starts.size()); i++)
            bi.start_vars.push_back(model.NewBoolVar());

        blocks.push_back(bi);
    }

    // ── Ограничения суммы ──────────────────────────────────────────────────
    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0 || lessons[l].is_block) continue;
        LinearExpr sum;
        for (int lt = 0; lt < local_slots; lt++) sum += x[l][lt];
        model.AddEquality(sum, quotas[l]);
    }

    for (auto& blk : blocks) {
        int l = blk.lesson_id;
        int req = quotas[l];

        if (static_cast<int>(blk.possible_starts.size()) < req) {
            WeekSolveResult res;
            res.success = false;
            res.status = "NO_STARTS_W" + std::to_string(week_num);
            return res;
        }

        {
            LinearExpr ss;
            for (const auto& v : blk.start_vars) ss += v;
            model.AddEquality(ss, req);
        }

        std::vector<std::vector<BoolVar>> covers(local_slots);
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int st = blk.possible_starts[i];
            covers[st].push_back(blk.start_vars[i]);
            covers[st + 1].push_back(blk.start_vars[i]);
        }
        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr cs;
            for (const auto& v : covers[lt]) cs += v;
            model.AddEquality(x[l][lt], cs);
        }
    }

    // ── Зафиксированные слоты (locked) ────────────────────────────────────
    {
        std::map<int, int> lid_to_l;
        for (int l = 0; l < num_lessons; l++) lid_to_l[lessons[l].id] = l;

        for (const LockedAssignment& a : locked) {
            auto it = lid_to_l.find(a.lesson_id);
            if (it == lid_to_l.end()) continue;
            int l = it->second;
            if (quotas[l] == 0) continue;
            int local_d = -1;
            for (int ld = 0; ld < W; ld++) {
                if (week_days[ld] == a.date) { local_d = ld; break; }
            }
            if (local_d < 0 || a.slot < 0 || a.slot >= SLOTS_PER_DAY) continue;
            int lt = local_d * SLOTS_PER_DAY + a.slot;
            model.AddEquality(x[l][lt], 1);
        }
    }

    // ── Недоступные дни ───────────────────────────────────────────────────
    for (int ld = 0; ld < W; ld++) {
        for (int g = 0; g < GROUPS; g++) {
            if (IsAvailable(week_days[ld], g, unavailable)) continue;
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int lt = ld * SLOTS_PER_DAY + s;
                for (int l = 0; l < num_lessons; l++) {
                    if (quotas[l] > 0 && lessons[l].group == g)
                        model.AddEquality(x[l][lt], 0);
                }
            }
        }
    }

    // ── ПП: только в разрешённые дни ──────────────────────────────────────
    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_pp || quotas[l] == 0) continue;
        int g = lessons[l].group;
        for (int ld = 0; ld < W; ld++) {
            if (pp_allowed[g].count(wdix[ld])) continue;
            for (int s = 0; s < SLOTS_PER_DAY; s++)
                model.AddEquality(x[l][ld * SLOTS_PER_DAY + s], 0);
        }
    }

    // ── group_busy, part_busy ─────────────────────────────────────────────
    std::vector<std::vector<BoolVar>> group_busy(GROUPS, std::vector<BoolVar>(local_slots));
    std::vector<std::vector<std::vector<BoolVar>>> part_busy(
        GROUPS,
        std::vector<std::vector<BoolVar>>(PARTS_PER_GROUP, std::vector<BoolVar>(local_slots))
    );

    for (int g = 0; g < GROUPS; g++) {
        int base_sg = g * PARTS_PER_GROUP;
        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr whole_sum;
            LinearExpr sub_sum[PARTS_PER_GROUP];

            for (int l = 0; l < num_lessons; l++) {
                if (quotas[l] == 0 || lessons[l].group != g) continue;
                if (lessons[l].subgroup == -1) {
                    whole_sum += x[l][lt];
                } else {
                    int part = lessons[l].subgroup - base_sg;
                    if (part >= 0 && part < PARTS_PER_GROUP) sub_sum[part] += x[l][lt];
                }
            }

            model.AddLessOrEqual(whole_sum, 1);
            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                model.AddLessOrEqual(sub_sum[p], 1);
                LinearExpr wp; wp += whole_sum; wp += sub_sum[p];
                model.AddLessOrEqual(wp, 1);
            }

            LinearExpr gss; gss += whole_sum;
            for (int p = 0; p < PARTS_PER_GROUP; p++) gss += sub_sum[p];
            group_busy[g][lt] = MakePositiveIndicator(model, gss);

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                LinearExpr pss; pss += whole_sum; pss += sub_sum[p];
                part_busy[g][p][lt] = MakePositiveIndicator(model, pss);
            }
        }
    }

    std::vector<std::vector<BoolVar>> student_entities;
    for (int g = 0; g < GROUPS; g++)
        for (int p = 0; p < PARTS_PER_GROUP; p++)
            student_entities.push_back(part_busy[g][p]);

    // ── Правило УП-день ───────────────────────────────────────────────────
    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int ld = 0; ld < W; ld++) {
                std::vector<BoolVar> up_starts;
                for (const auto& blk : blocks) {
                    if (!LessonAffectsPart(lessons[blk.lesson_id], g, p)) continue;
                    for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                        if (blk.possible_starts[i] / SLOTS_PER_DAY == ld)
                            up_starts.push_back(blk.start_vars[i]);
                    }
                }
                if (up_starts.empty()) continue;

                LinearExpr uss;
                for (const auto& v : up_starts) uss += v;
                model.AddLessOrEqual(uss, 1);

                LinearExpr day_sum;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    day_sum += part_busy[g][p][ld * SLOTS_PER_DAY + s];

                BoolVar has_up = MakePositiveIndicator(model, uss);
                LinearExpr req2; req2 += uss; req2 += uss;
                model.AddEquality(day_sum, req2).OnlyEnforceIf(has_up);
            }
        }
    }

    // ── teacher_busy ──────────────────────────────────────────────────────
    std::vector<std::vector<BoolVar>> teacher_busy(TEACHERS, std::vector<BoolVar>(local_slots));
    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int lt = 0; lt < local_slots; lt++) {
            LinearExpr sum;
            for (int l = 0; l < num_lessons; l++) {
                if (quotas[l] > 0 && lessons[l].teacher == teacher) sum += x[l][lt];
            }
            model.AddLessOrEqual(sum, 1);
            teacher_busy[teacher][lt] = MakePositiveIndicator(model, sum);
        }
    }

    // ── Преподаватель заблокирован во время УП ────────────────────────────
    for (const auto& blk : blocks) {
        int teacher = lessons[blk.lesson_id].teacher;
        if (teacher < 0) continue;
        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_lt = blk.possible_starts[i];
            // week_days используется как локальный all_days → возвращает локальные слоты
            std::vector<int> blocked = TeacherBlockedSlotsForUpStart(week_days, start_lt);
            for (int blt : blocked) {
                LinearExpr ord;
                for (int other = 0; other < num_lessons; other++) {
                    if (quotas[other] == 0 || lessons[other].is_block) continue;
                    if (lessons[other].teacher == teacher) ord += x[other][blt];
                }
                model.AddEquality(ord, 0).OnlyEnforceIf(blk.start_vars[i]);
            }
        }
    }

    // ── Нельзя двум УП одного преподавателя пересекаться по времени ──────
    {
        struct UpRef { int bi, si, teacher, lt; TimeInterval iv; };
        std::vector<UpRef> up_refs;
        for (int b = 0; b < static_cast<int>(blocks.size()); b++) {
            const auto& blk = blocks[b];
            int teacher = lessons[blk.lesson_id].teacher;
            for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                int lt = blk.possible_starts[i];
                int ld = lt / SLOTS_PER_DAY;
                up_refs.push_back({b, i, teacher, lt,
                    UpIntervalForStartSlot(week_days[ld], lt % SLOTS_PER_DAY)});
            }
        }
        for (int a = 0; a < static_cast<int>(up_refs.size()); a++) {
            for (int b = a + 1; b < static_cast<int>(up_refs.size()); b++) {
                const auto& la = up_refs[a]; const auto& lb = up_refs[b];
                if (la.teacher < 0 || la.teacher != lb.teacher) continue;
                if (la.lt / SLOTS_PER_DAY != lb.lt / SLOTS_PER_DAY) continue;
                if (!IntervalsOverlap(la.iv, lb.iv)) continue;
                LinearExpr both;
                both += blocks[la.bi].start_vars[la.si];
                both += blocks[lb.bi].start_vars[lb.si];
                model.AddLessOrEqual(both, 1);
            }
        }
    }

    // ── Макс/мин пар в день ───────────────────────────────────────────────
    std::vector<std::vector<std::vector<BoolVar>>> student_day_has(
        GROUPS,
        std::vector<std::vector<BoolVar>>(PARTS_PER_GROUP, std::vector<BoolVar>(W))
    );
    std::vector<BoolVar> five_pair_vars;

    for (int g = 0; g < GROUPS; g++) {
        for (int ld = 0; ld < W; ld++) {
            LinearExpr vgds;
            for (int s = 0; s < SLOTS_PER_DAY; s++)
                vgds += group_busy[g][ld * SLOTS_PER_DAY + s];
            model.AddLessOrEqual(vgds, MAX_STUDENT_PAIRS_PER_DAY);
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int ld = 0; ld < W; ld++) {
                LinearExpr ds;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    ds += part_busy[g][p][ld * SLOTS_PER_DAY + s];

                BoolVar has = MakePositiveIndicator(model, ds);
                student_day_has[g][p][ld] = has;
                AddMinIfPositive(model, ds, has, MIN_STUDENT_PAIRS_PER_STUDY_DAY);
                model.AddLessOrEqual(ds, MAX_STUDENT_PAIRS_PER_DAY);

                BoolVar is5 = model.NewBoolVar();
                model.AddEquality(ds, MAX_STUDENT_PAIRS_PER_DAY).OnlyEnforceIf(is5);
                model.AddLessOrEqual(ds, MAX_STUDENT_PAIRS_PER_DAY - 1).OnlyEnforceIf(is5.Not());
                five_pair_vars.push_back(is5);
            }
        }
    }

    // ── Синхронизация подгрупп ────────────────────────────────────────────
    for (int g = 0; g < GROUPS; g++)
        for (int ld = 0; ld < W; ld++)
            model.AddEquality(student_day_has[g][0][ld], student_day_has[g][1][ld]);

    // ── Мин. учебных дней в неделю ────────────────────────────────────────
    for (int g = 0; g < GROUPS; g++) {
        std::vector<int> avail_lds;
        for (int ld = 0; ld < W; ld++)
            if (IsAvailable(week_days[ld], g, unavailable)) avail_lds.push_back(ld);
        if (avail_lds.empty()) continue;

        int req_d = std::min(MIN_STUDENT_STUDY_DAYS_PER_WEEK, static_cast<int>(avail_lds.size()));
        LinearExpr wsd;
        for (int ld : avail_lds) wsd += student_day_has[g][0][ld];

        if (HARD_MIN_STUDY_DAYS_PER_WEEK) {
            model.AddGreaterOrEqual(wsd, req_d);
        } else if (USE_QUALITY_OBJECTIVE) {
            IntVar miss = model.NewIntVar(Domain(0, req_d));
            LinearExpr wm; wm += wsd; wm += miss;
            model.AddGreaterOrEqual(wm, req_d);
            objective += miss * GROUP_WEEK_MISSING_DAY_WEIGHT;
        }
    }

    // ── Мин 2 пары преподавателю ──────────────────────────────────────────
    if (HARD_MIN_2_TEACHER_PAIRS_PER_DAY) {
        for (int teacher = 0; teacher < TEACHERS; teacher++) {
            for (int ld = 0; ld < W; ld++) {
                LinearExpr ds;
                for (int s = 0; s < SLOTS_PER_DAY; s++)
                    ds += teacher_busy[teacher][ld * SLOTS_PER_DAY + s];
                AddMin2IfPositive(model, ds);
            }
        }
    }

    // ── Без окон (жёстко) ─────────────────────────────────────────────────
    if (HARD_NO_STUDENT_WINDOWS) AddNoWindowsHard(model, student_entities, W);
    if (HARD_NO_TEACHER_WINDOWS) AddNoWindowsHard(model, teacher_busy, W);

    // ── Кампус ────────────────────────────────────────────────────────────
    std::vector<std::vector<IntVar>> group_day_campus(GROUPS, std::vector<IntVar>(W));
    std::vector<std::vector<IntVar>> teacher_day_campus(TEACHERS, std::vector<IntVar>(W));
    for (int g = 0; g < GROUPS; g++)
        for (int ld = 0; ld < W; ld++)
            group_day_campus[g][ld] = model.NewIntVar(Domain(0, 1));
    for (int t = 0; t < TEACHERS; t++)
        for (int ld = 0; ld < W; ld++)
            teacher_day_campus[t][ld] = model.NewIntVar(Domain(0, 1));

    for (int l = 0; l < num_lessons; l++) {
        if (quotas[l] == 0) continue;
        int g = lessons[l].group;
        int teacher = lessons[l].teacher;
        for (int ld = 0; ld < W; ld++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int lt = ld * SLOTS_PER_DAY + s;
                if (teacher >= 0)
                    model.AddEquality(group_day_campus[g][ld], teacher_day_campus[teacher][ld])
                        .OnlyEnforceIf(x[l][lt]);
                if (lessons[l].allowed_campuses.size() == 1) {
                    int campus = static_cast<int>(*lessons[l].allowed_campuses.begin());
                    model.AddEquality(group_day_campus[g][ld], campus).OnlyEnforceIf(x[l][lt]);
                    if (teacher >= 0)
                        model.AddEquality(teacher_day_campus[teacher][ld], campus).OnlyEnforceIf(x[l][lt]);
                }
            }
        }
    }

    // ── Целевая функция качества ──────────────────────────────────────────
    if (USE_QUALITY_OBJECTIVE) {
        for (const auto& v : five_pair_vars)
            objective += v * STUDENT_FIVE_PAIR_DAY_WEIGHT;

        if (!HARD_NO_STUDENT_WINDOWS && OPTIMIZE_STUDENT_WINDOWS) {
            for (const auto& gap : CreateWindowPenaltyVars(model, student_entities, W))
                objective += gap * STUDENT_WINDOW_WEIGHT;
        }
        if (!HARD_NO_TEACHER_WINDOWS && OPTIMIZE_TEACHER_WINDOWS) {
            for (const auto& gap : CreateWindowPenaltyVars(model, teacher_busy, W))
                objective += gap * TEACHER_WINDOW_WEIGHT;
        }
        if (STUDENT_LATE_SLOT_WEIGHT > 0) {
            for (const auto& busy : student_entities)
                for (int ld = 0; ld < W; ld++)
                    for (int s = 0; s < SLOTS_PER_DAY; s++)
                        objective += busy[ld * SLOTS_PER_DAY + s] * (s * STUDENT_LATE_SLOT_WEIGHT);
        }
        if (TEACHER_LATE_SLOT_WEIGHT > 0) {
            for (int teacher = 0; teacher < TEACHERS; teacher++)
                for (int ld = 0; ld < W; ld++)
                    for (int s = 0; s < SLOTS_PER_DAY; s++)
                        objective += teacher_busy[teacher][ld * SLOTS_PER_DAY + s] * (s * TEACHER_LATE_SLOT_WEIGHT);
        }
        model.Minimize(objective);
    }

    // ── Решаем ────────────────────────────────────────────────────────────
    SatParameters params;
    params.set_num_search_workers(SOLVER_WORKERS);
    // Лимит на одну неделю: не более 120 с и не более общего лимита
    double week_limit = std::min(static_cast<double>(SOLVER_TIME_LIMIT_SECONDS), 120.0);
    params.set_max_time_in_seconds(week_limit);
    params.set_random_seed(g_solver_config.random_seed + week_num * 17);
    params.set_max_memory_in_mb(SOLVER_MAX_MEMORY_MB);
    params.set_linearization_level(g_solver_config.linearization_level);
    params.set_symmetry_level(g_solver_config.symmetry_level);
    if (STOP_AFTER_FIRST_SOLUTION) params.set_stop_after_first_solution(true);

    operations_research::sat::Model sat_model;
    sat_model.Add(NewSatParameters(params));

    CpModelProto proto = model.Build();
    CpSolverResponse resp = SolveCpModel(proto, &sat_model);

    WeekSolveResult result;
    result.status = CpSolverStatus_Name(resp.status());

    if (resp.status() == CpSolverStatus::OPTIMAL ||
        resp.status() == CpSolverStatus::FEASIBLE) {
        result.success = true;
        result.x_vals.assign(num_lessons, std::vector<int>(local_slots, 0));
        for (int l = 0; l < num_lessons; l++) {
            if (quotas[l] == 0) continue;
            for (int lt = 0; lt < local_slots; lt++)
                result.x_vals[l][lt] = static_cast<int>(SolutionIntegerValue(resp, x[l][lt]));
        }
    } else {
        result.success = false;
    }

    return result;
}

}  // anonymous namespace

GenerationResult GenerateScheduleWeekly(const std::string& output_dir) {
    GenerationOptions empty_opts;
    return GenerateScheduleWeekly(output_dir, empty_opts);
}

GenerationResult GenerateScheduleWeekly(
    const std::string& output_dir,
    const GenerationOptions& options
) {
    WeeklyGenCallbacks empty_cbs;
    return GenerateScheduleWeekly(output_dir, options, empty_cbs);
}

GenerationResult GenerateScheduleWeekly(
    const std::string& output_dir,
    const GenerationOptions& options,
    const WeeklyGenCallbacks& callbacks
) {

    // ── Загрузка входных данных ───────────────────────────────────────────
    ScheduleInputData input_data;
    std::string input_error;
    if (!LoadScheduleInputData(input_data, input_error)) {
        return {false, "INPUT_ERROR",
            "Не удалось загрузить data/timetable_data.json: " + input_error, output_dir};
    }

    const Date start_date = input_data.start_date;
    const Date end_date   = input_data.end_date;
    const auto& unavailable = input_data.unavailable;
    const auto& unavailable_day_texts = input_data.unavailable_day_texts;

    auto all_days = GenerateSchoolDays(start_date, end_date);
    const int num_days   = static_cast<int>(all_days.size());
    const int total_slots = num_days * SLOTS_PER_DAY;

    std::vector<Lesson> lessons = input_data.lessons;
    const int num_lessons = static_cast<int>(lessons.size());

    std::vector<std::string> validation_errors;
    if (!ValidateInputLessonsDetailed(lessons, validation_errors)) {
        std::string msg = "Входные данные содержат ошибки (" +
            std::to_string(validation_errors.size()) + "): ";
        for (size_t i = 0; i < validation_errors.size() && i < 5; i++) {
            if (i > 0) msg += " | ";
            msg += validation_errors[i];
        }
        if (validation_errors.size() > 5) msg += " | …";
        return {false, "INPUT_ERROR", msg, output_dir};
    }

    std::filesystem::create_directories(output_dir);
    std::filesystem::create_directories(std::filesystem::path(output_dir) / "groups");

    PrintInputDiagnostics(lessons, all_days, unavailable, start_date);

    // ── Структура недель ──────────────────────────────────────────────────
    std::vector<int> week_index(num_days);
    for (int d = 0; d < num_days; d++)
        week_index[d] = WeekIndexFromStart(start_date, all_days[d]);

    std::set<int> wk_set;
    for (int wi : week_index) wk_set.insert(wi);
    const std::vector<int> weeks_list(wk_set.begin(), wk_set.end());
    const int num_weeks = static_cast<int>(weeks_list.size());
    std::map<int, int> week_pos;
    for (int i = 0; i < num_weeks; i++) week_pos[weeks_list[i]] = i;

    // week_day_indices[w] = список глобальных индексов дней в неделе w
    std::vector<std::vector<int>> week_day_indices(num_weeks);
    for (int d = 0; d < num_days; d++)
        week_day_indices[week_pos[week_index[d]]].push_back(d);

    // ── group_avail_weeks: недели с доступными днями ──────────────────────
    std::vector<std::vector<int>> group_avail_weeks(GROUPS);
    std::vector<std::map<int, std::vector<int>>> group_week_days(GROUPS);

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                int w = week_pos[week_index[d]];
                group_week_days[g][w].push_back(d);
            }
        }
        for (auto& kv : group_week_days[g]) group_avail_weeks[g].push_back(kv.first);
    }

    // ── Алгоритм Брезенхема ───────────────────────────────────────────────
    auto bresenham_quota = [](int total, int active_weeks) -> std::vector<int> {
        if (active_weeks <= 0) return {};
        std::vector<int> q(active_weeks, 0);
        int acc = 0;
        for (int i = 0; i < active_weeks; i++) {
            acc += total;
            int slots_here = acc / active_weeks;
            acc -= slots_here * active_weeks;
            q[i] = slots_here;
        }
        return q;
    };

    // ── ПП: вычисляем разрешённые дни (последние ceil(total/3)) ──────────
    // pp_allowed_global[g] = список глобальных индексов дней, где ПП разрешена
    std::vector<std::vector<int>> pp_allowed_global(GROUPS);
    {
        std::vector<std::vector<int>> group_avail_days(GROUPS);
        for (int g = 0; g < GROUPS; g++) {
            for (int d = 0; d < num_days; d++)
                if (IsAvailable(all_days[d], g, unavailable))
                    group_avail_days[g].push_back(d);
        }
        for (int l = 0; l < num_lessons; l++) {
            if (!lessons[l].is_pp) continue;
            int g = lessons[l].group;
            const auto& avail = group_avail_days[g];
            int pp_days = (lessons[l].total_slots + 2) / 3;
            if (static_cast<int>(avail.size()) <= pp_days) {
                pp_allowed_global[g] = avail;
            } else {
                int cutoff = static_cast<int>(avail.size()) - pp_days;
                pp_allowed_global[g].assign(avail.begin() + cutoff, avail.end());
            }
        }
    }

    // ── lesson_week_quota[l][w] — квоты по Брезенхему ────────────────────
    // Для ПП уроков: активные недели — только те, где есть pp_allowed дни группы
    std::vector<std::vector<int>> lesson_week_quota(num_lessons, std::vector<int>(num_weeks, 0));

    for (int l = 0; l < num_lessons; l++) {
        int g = lessons[l].group;

        if (lessons[l].is_pp) {
            // Активные недели — только те, где есть разрешённый для ПП день
            std::set<int> pp_day_set(pp_allowed_global[g].begin(), pp_allowed_global[g].end());
            std::vector<int> pp_active_weeks;
            for (int w : group_avail_weeks[g]) {
                bool has_pp = false;
                for (int gd : week_day_indices[w])
                    if (pp_day_set.count(gd)) { has_pp = true; break; }
                if (has_pp) pp_active_weeks.push_back(w);
            }
            if (pp_active_weeks.empty()) continue;
            auto q = bresenham_quota(lessons[l].total_slots, static_cast<int>(pp_active_weeks.size()));
            for (int i = 0; i < static_cast<int>(pp_active_weeks.size()); i++)
                lesson_week_quota[l][pp_active_weeks[i]] = q[i];
        } else {
            const auto& avail_weeks = group_avail_weeks[g];
            int active = static_cast<int>(avail_weeks.size());
            if (active == 0) continue;
            auto q = bresenham_quota(lessons[l].total_slots, active);
            for (int i = 0; i < active; i++)
                lesson_week_quota[l][avail_weeks[i]] = q[i];
        }
    }

    // ── Корректировка квот под зафиксированные слоты ─────────────────────
    // Если locked-слот попадает в неделю с quota==0, повышаем квоту в этой неделе
    // и понижаем в неделе с максимальной квотой (чтобы sum == total_slots).
    if (!options.locked.empty()) {
        std::map<int, int> lid_to_l;
        for (int l = 0; l < num_lessons; l++) lid_to_l[lessons[l].id] = l;
        std::map<Date, int> date_to_day;
        for (int d = 0; d < num_days; d++) date_to_day[all_days[d]] = d;

        for (const LockedAssignment& a : options.locked) {
            auto lit = lid_to_l.find(a.lesson_id);
            if (lit == lid_to_l.end()) continue;
            int l = lit->second;
            auto dit = date_to_day.find(a.date);
            if (dit == date_to_day.end()) continue;
            int d = dit->second;
            int w = week_pos[week_index[d]];

            if (lesson_week_quota[l][w] == 0) {
                // Найти неделю с максимальной квотой для этого урока
                int max_w = -1, max_q = 0;
                for (int ww = 0; ww < num_weeks; ww++) {
                    if (lesson_week_quota[l][ww] > max_q) {
                        max_q = lesson_week_quota[l][ww];
                        max_w = ww;
                    }
                }
                if (max_w >= 0 && max_q > 0) {
                    lesson_week_quota[l][w]++;
                    lesson_week_quota[l][max_w]--;
                    std::cout << "  [weekly] lock: урок " << lessons[l].name
                              << " неделя " << w << " получила квоту +1 (с недели " << max_w << ")\n";
                }
            }
        }
    }

    std::cout << "\n══ Режим: генерация по неделям ══\n";
    std::cout << "Недель: " << num_weeks << ", Дней: " << num_days << "\n\n";

    // ── Решаем по неделям ─────────────────────────────────────────────────
    std::vector<std::vector<int>> global_x_vals(num_lessons, std::vector<int>(total_slots, 0));
    auto solve_start = std::chrono::steady_clock::now();

    for (int w = 0; w < num_weeks; w++) {
        // ── Проверка отмены ────────────────────────────────────────────────
        if (callbacks.cancel_flag && callbacks.cancel_flag->load()) {
            std::cout << "\nГенерация отменена пользователем на неделе " << (w + 1) << ".\n";
            // Сохраняем частичный результат перед выходом
            WriteScheduleFiles(output_dir, global_x_vals, num_days,
                lessons, all_days, unavailable, unavailable_day_texts, false);
            return {false, "CANCELLED", "Генерация отменена", output_dir};
        }

        const auto& wdix = week_day_indices[w];
        if (wdix.empty()) continue;

        bool any_lesson = false;
        for (int l = 0; l < num_lessons; l++) {
            if (lesson_week_quota[l][w] > 0) { any_lesson = true; break; }
        }

        std::string date_from = DateToString(all_days[wdix.front()]);
        std::string date_to   = DateToString(all_days[wdix.back()]);

        if (!any_lesson) {
            std::cout << "Неделя " << (w + 1) << "/" << num_weeks
                      << " [" << date_from << " … " << date_to << "] — пропуск (нет занятий)\n";
            if (callbacks.on_week_done)
                callbacks.on_week_done(w, num_weeks, date_from, date_to, "skipped", 0.0);
            continue;
        }

        if (callbacks.on_week_start)
            callbacks.on_week_start(w, num_weeks, date_from, date_to);

        std::cout << "Неделя " << (w + 1) << "/" << num_weeks
                  << " [" << date_from << " … " << date_to
                  << "] " << wdix.size() << " дней\n";

        std::vector<int> quotas(num_lessons);
        for (int l = 0; l < num_lessons; l++) quotas[l] = lesson_week_quota[l][w];

        auto t0 = std::chrono::steady_clock::now();
        WeekSolveResult wr = SolveOneWeek(
            w, wdix, all_days, lessons, unavailable,
            quotas, pp_allowed_global, options.locked
        );
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();

        if (!wr.success) {
            std::cerr << "  ОШИБКА недели " << (w + 1) << ": " << wr.status
                      << " (" << std::fixed << std::setprecision(1) << elapsed << " с)\n";
            if (callbacks.on_week_done)
                callbacks.on_week_done(w, num_weeks, date_from, date_to, "failed", elapsed);
            // Сохраняем частичный результат
            WriteScheduleFiles(output_dir, global_x_vals, num_days,
                lessons, all_days, unavailable, unavailable_day_texts, false);
            return {false, wr.status,
                "Не удалось решить неделю " + std::to_string(w + 1) + ": " + wr.status,
                output_dir};
        }

        std::cout << "  Решено за " << std::fixed << std::setprecision(1) << elapsed
                  << " с [" << wr.status << "]\n";

        // Копируем локальные x_vals в глобальные
        for (int l = 0; l < num_lessons; l++) {
            if (quotas[l] == 0) continue;
            for (int li = 0; li < static_cast<int>(wdix.size()); li++) {
                int gd = wdix[li];
                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    global_x_vals[l][gd * SLOTS_PER_DAY + s] = wr.x_vals[l][li * SLOTS_PER_DAY + s];
                }
            }
        }

        // ── Автосохранение после каждой недели ────────────────────────────
        WriteScheduleFiles(output_dir, global_x_vals, num_days,
            lessons, all_days, unavailable, unavailable_day_texts, false);

        if (callbacks.on_week_done)
            callbacks.on_week_done(w, num_weeks, date_from, date_to, "done", elapsed);
    }

    double total_elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - solve_start).count();
    std::cout << "\nВсего недель решено за " << std::fixed << std::setprecision(1)
              << total_elapsed << " с\n";

    // Финальная запись со статистикой
    WriteScheduleFiles(output_dir, global_x_vals, num_days,
        lessons, all_days, unavailable, unavailable_day_texts, true);

    std::cout << "\nФайлы созданы в: " << output_dir << "\n";

    return {true, "WEEKLY_FEASIBLE", "Расписание по неделям найдено", output_dir};
}

int RunScheduler() {
    GenerationResult result = GenerateSchedule("output/latest");
    return result.success ? 0 : 1;
}

}  // namespace timetable

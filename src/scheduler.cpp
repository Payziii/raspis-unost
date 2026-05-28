#include "scheduler.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
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

    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].is_block) continue;

        LinearExpr sum;

        for (int t = 0; t < total_slots; t++) {
            sum += x[l][t];
        }

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

        LinearExpr start_sum;

        for (const auto& v : blk.start_vars) {
            start_sum += v;
        }

        model.AddEquality(start_sum, required_starts);

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

            if (left.teacher != right.teacher) {
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

    std::vector<int> week_index(num_days);
    for (int d = 0; d < num_days; d++) {
        week_index[d] = WeekIndexFromStart(start_date, all_days[d]);
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

    std::map<std::pair<int, int>, std::vector<int>> theory_of;
    std::map<std::pair<int, int>, std::vector<int>> lab_of;

    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].subject_id < 0) continue;

        auto key = std::make_pair(lessons[l].group, lessons[l].subject_id);

        if (lessons[l].is_lab) {
            lab_of[key].push_back(l);
        } else {
            theory_of[key].push_back(l);
        }
    }

    for (const auto& item : lab_of) {
        const auto& key = item.first;
        const auto& labs = item.second;

        auto it = theory_of.find(key);
        if (it == theory_of.end()) continue;

        const auto& theories = it->second;
        if (theories.empty()) continue;

        if (STRICT_ALL_THEORY_BEFORE_LABS) {
            IntVar last_theory = model.NewIntVar(Domain(0, total_slots - 1));

            for (int l_th : theories) {
                for (int t = 0; t < total_slots; t++) {
                    model.AddGreaterOrEqual(last_theory, t).OnlyEnforceIf(x[l_th][t]);
                }
            }

            for (int l_lab : labs) {
                for (int t = 0; t < total_slots; t++) {
                    model.AddLessThan(last_theory, t).OnlyEnforceIf(x[l_lab][t]);
                }
            }
        } else {
            int group = key.first;
            std::vector<std::vector<int>> buckets =
                BuildAvailableDayBuckets(group, all_days, unavailable);

            if (buckets.empty()) {
                continue;
            }

            LinearExpr initial_theory_sum;
            LinearExpr initial_lab_sum;

            for (int l_th : theories) {
                for (int d : buckets.front()) {
                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        int t = d * SLOTS_PER_DAY + s;
                        initial_theory_sum += x[l_th][t];
                    }
                }
            }

            for (int l_lab : labs) {
                for (int d : buckets.front()) {
                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        int t = d * SLOTS_PER_DAY + s;
                        initial_lab_sum += x[l_lab][t];
                    }
                }
            }

            model.AddGreaterOrEqual(
                initial_theory_sum,
                MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS
            );
            model.AddEquality(initial_lab_sum, 0);
        }
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

                model.AddEquality(group_day_campus[group][d], teacher_day_campus[teacher][d])
                    .OnlyEnforceIf(x[l][t]);

                if (lessons[l].allowed_campuses.size() == 1) {
                    int campus = static_cast<int>(*lessons[l].allowed_campuses.begin());

                    model.AddEquality(group_day_campus[group][d], campus)
                        .OnlyEnforceIf(x[l][t]);

                    model.AddEquality(teacher_day_campus[teacher][d], campus)
                        .OnlyEnforceIf(x[l][t]);
                }
            }
        }
    }

    if (USE_QUALITY_OBJECTIVE) {
        for (const auto& v : student_five_pair_day_vars) {
            objective += v * STUDENT_FIVE_PAIR_DAY_WEIGHT;
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

int RunScheduler() {
    GenerationResult result = GenerateSchedule("output/latest");
    return result.success ? 0 : 1;
}

}  // namespace timetable

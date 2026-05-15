#include "scheduler.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"

#include "config.h"
#include "date_utils.h"
#include "diagnostics.h"
#include "lessons_data.h"
#include "model_utils.h"
#include "output_writers.h"
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

int RunScheduler() {
    Date start_date = {2026, 1, 12};
    Date end_date = {2026, 6, 19};

    std::map<int, std::vector<std::pair<Date, Date>>> unavailable;
    unavailable[0] = {{{2026, 4, 30}, {2026, 6, 19}}}; // ИСП-3304 ПП
    unavailable[1] = {{{2026, 4, 30}, {2026, 6, 19}}}; // ИСП-3305п ПП
    // unavailable[1] = {{{2026, 3, 20}, {2026, 3, 30}}}; // ИСП-3305п сборы

    auto all_days = GenerateSchoolDays(start_date, end_date);

    int num_days = static_cast<int>(all_days.size());
    int total_slots = num_days * SLOTS_PER_DAY;

    std::vector<Lesson> lessons = CreateLessons();
    int num_lessons = static_cast<int>(lessons.size());

    if (!ValidateInputLessons(lessons)) {
        std::cerr << "\nВходные данные содержат ошибки. Модель не построена.\n";
        return 1;
    }

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

        int required_starts = lessons[l].total_slots / 2;
        total_block_start_vars += static_cast<int>(blk.start_vars.size());

        if (static_cast<int>(blk.possible_starts.size()) < required_starts) {
            std::cerr << "Недостаточно возможных стартов для блока: "
                      << lessons[l].name
                      << ", группа " << GROUP_NAME[lessons[l].group]
                      << ", доступно стартов " << blk.possible_starts.size()
                      << ", требуется " << required_starts
                      << "\n";
            return 1;
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

    std::cout << "Запуск решателя...\n";

    CpModelProto model_proto = model.Build();

    std::cout << "\n========== Размер модели ==========\n";
    std::cout << "Переменных: " << model_proto.variables_size() << "\n";
    std::cout << "Ограничений: " << model_proto.constraints_size() << "\n";
    std::cout << "Размер proto: " << (model_proto.ByteSizeLong() / (1024.0 * 1024.0)) << " МБ\n";

    SatParameters params;
    params.set_num_search_workers(SOLVER_WORKERS);
    params.set_max_time_in_seconds(SOLVER_TIME_LIMIT_SECONDS);
    params.set_random_seed(1);
    params.set_max_memory_in_mb(SOLVER_MAX_MEMORY_MB);
    params.set_linearization_level(0);
    params.set_symmetry_level(2);

    if (STOP_AFTER_FIRST_SOLUTION) {
        params.set_stop_after_first_solution(true);
    }

    operations_research::sat::Model sat_model;
    sat_model.Add(NewSatParameters(params));

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

        WriteAllGroupsTxt(
            "raspisanie_all.txt",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus
        );

        WriteGroupScheduleTxt(
            "raspisanie_ISP-3304.txt",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            0
        );

        WriteGroupScheduleTxt(
            "raspisanie_ISP-3305p.txt",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            1
        );

        WriteGroupsCsv(
            "raspisanie_groups.csv",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus
        );

        WriteTeachersTxt(
            "raspisanie_teachers.txt",
            response,
            all_days,
            lessons,
            x,
            blocks,
            teacher_busy,
            teacher_day_campus
        );

        std::cout << "\nФайлы созданы:\n";
        std::cout << "  raspisanie_all.txt\n";
        std::cout << "  raspisanie_ISP-3304.txt\n";
        std::cout << "  raspisanie_ISP-3305p.txt\n";
        std::cout << "  raspisanie_groups.csv\n";
        std::cout << "  raspisanie_teachers.txt\n";

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
    } else if (response.status() == CpSolverStatus::UNKNOWN) {
        std::cout << "\nРешатель не успел найти или доказать решение за лимит времени.\n";
        std::cout << "Что можно сделать:\n";
        std::cout << "  1) увеличить SOLVER_TIME_LIMIT_SECONDS\n";
        std::cout << "  2) уменьшить SOLVER_WORKERS до 2, если не хватает ОЗУ\n";
        std::cout << "  3) увеличить SUBJECT_BUCKET_EXTRA_SLOTS до 3 или 4\n";
        std::cout << "  4) временно поставить HARD_NO_STUDENT_WINDOWS = false\n";
    } else if (response.status() == CpSolverStatus::MODEL_INVALID) {
        std::cout << "\nМодель некорректна. Проверь CpSolverResponseStats выше.\n";
    } else {
        std::cout << "\nРешение не найдено. Статус: "
                  << CpSolverStatus_Name(response.status())
                  << "\n";
    }

    return 0;
}

}  // namespace timetable

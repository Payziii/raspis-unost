#include "model_utils.h"

#include <algorithm>
#include <map>
#include <string>

#include "config.h"
#include "date_utils.h"
#include "format_utils.h"

namespace timetable {

using operations_research::Domain;
using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::IntVar;
using operations_research::sat::LinearExpr;

BoolVar MakePositiveIndicator(CpModelBuilder& model, const LinearExpr& sum) {
    BoolVar has = model.NewBoolVar();

    model.AddGreaterThan(sum, 0).OnlyEnforceIf(has);
    model.AddEquality(sum, 0).OnlyEnforceIf(has.Not());

    return has;
}

void AddMinIfPositive(
    CpModelBuilder& model,
    const LinearExpr& day_sum,
    const BoolVar& has,
    int minimum_value
) {
    LinearExpr rhs;
    rhs += has * minimum_value;

    model.AddGreaterOrEqual(day_sum, rhs);
}

void AddMin2IfPositive(CpModelBuilder& model, const LinearExpr& day_sum) {
    BoolVar has = MakePositiveIndicator(model, day_sum);

    LinearExpr rhs;
    rhs += has;
    rhs += has;

    model.AddGreaterOrEqual(day_sum, rhs);
}

void AddNoWindowsHard(
    CpModelBuilder& model,
    const std::vector<std::vector<BoolVar>>& busy_entities,
    int num_days
) {
    for (const auto& busy : busy_entities) {
        for (int d = 0; d < num_days; d++) {
            int base = d * SLOTS_PER_DAY;

            for (int left = 0; left < SLOTS_PER_DAY; left++) {
                for (int right = left + 2; right < SLOTS_PER_DAY; right++) {
                    for (int mid = left + 1; mid < right; mid++) {
                        LinearExpr expr;
                        expr += busy[base + left];
                        expr += busy[base + right];
                        expr -= busy[base + mid];

                        model.AddLessOrEqual(expr, 1);
                    }
                }
            }
        }
    }
}

std::vector<BoolVar> CreateWindowPenaltyVars(
    CpModelBuilder& model,
    const std::vector<std::vector<BoolVar>>& busy_entities,
    int num_days
) {
    std::vector<BoolVar> gaps;

    for (const auto& busy : busy_entities) {
        for (int d = 0; d < num_days; d++) {
            int base = d * SLOTS_PER_DAY;

            for (int s = 1; s < SLOTS_PER_DAY - 1; s++) {
                LinearExpr before_sum;
                LinearExpr after_sum;

                for (int q = 0; q < s; q++) {
                    before_sum += busy[base + q];
                }

                for (int q = s + 1; q < SLOTS_PER_DAY; q++) {
                    after_sum += busy[base + q];
                }

                BoolVar before = MakePositiveIndicator(model, before_sum);
                BoolVar after = MakePositiveIndicator(model, after_sum);
                BoolVar gap = model.NewBoolVar();

                model.AddImplication(gap, before);
                model.AddImplication(gap, after);
                model.AddImplication(gap, busy[base + s].Not());

                model.AddBoolOr({
                    before.Not(),
                    after.Not(),
                    busy[base + s],
                    gap
                });

                gaps.push_back(gap);
            }
        }
    }

    return gaps;
}

void AddSubjectSpreadPenalties(
    CpModelBuilder& model,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    LinearExpr& objective
) {
    for (int g = 0; g < GROUPS; g++) {
        std::vector<std::vector<int>> buckets =
            BuildAvailableDayBuckets(g, all_days, unavailable);

        int bucket_count = static_cast<int>(buckets.size());
        if (bucket_count <= 1) {
            continue;
        }

        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            std::map<std::string, std::vector<int>> stream_lessons;
            std::map<std::string, int> stream_total_slots;

            for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
                if (!LessonAffectsPart(lessons[l], g, p)) {
                    continue;
                }

                std::string key = SubjectFamilyKey(lessons[l]);
                stream_lessons[key].push_back(l);
                stream_total_slots[key] += lessons[l].total_slots;
            }

            for (const auto& item : stream_lessons) {
                const std::vector<int>& lesson_ids = item.second;
                int total = stream_total_slots[item.first];

                if (total < MIN_SUBJECT_SPREAD_TOTAL_SLOTS) {
                    continue;
                }

                bool all_lessons_are_whole_group = true;
                bool all_lessons_are_blocks = true;

                for (int l : lesson_ids) {
                    if (lessons[l].subgroup != -1) {
                        all_lessons_are_whole_group = false;
                    }

                    if (!lessons[l].is_block) {
                        all_lessons_are_blocks = false;
                    }
                }

                if (all_lessons_are_whole_group && p > 0) {
                    continue;
                }

                int ideal_per_bucket = CeilDiv(total, bucket_count);
                int soft_max_per_bucket = std::max(
                    SUBJECT_BUCKET_MIN_CAPACITY,
                    ideal_per_bucket + SUBJECT_BUCKET_EXTRA_SLOTS
                );

                int active_unit = all_lessons_are_blocks
                    ? BLOCK_SUBJECT_ACTIVE_BUCKET_UNIT
                    : NORMAL_SUBJECT_ACTIVE_BUCKET_UNIT;

                int target_active_buckets = std::min(
                    bucket_count,
                    CeilDiv(total, active_unit)
                );

                std::vector<BoolVar> bucket_has;

                for (int b = 0; b < bucket_count; b++) {
                    LinearExpr bucket_sum;

                    for (int l : lesson_ids) {
                        for (int d : buckets[b]) {
                            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                                int t = d * SLOTS_PER_DAY + s;
                                bucket_sum += x[l][t];
                            }
                        }
                    }

                    bucket_has.push_back(MakePositiveIndicator(model, bucket_sum));

                    IntVar overload = model.NewIntVar(Domain(0, total));
                    LinearExpr overload_guard;
                    overload_guard += overload;
                    overload_guard -= bucket_sum;
                    model.AddGreaterOrEqual(overload_guard, -soft_max_per_bucket);
                    objective += overload * SUBJECT_BUCKET_OVERLOAD_WEIGHT;
                }

                LinearExpr active_bucket_sum;
                for (const auto& has : bucket_has) {
                    active_bucket_sum += has;
                }

                IntVar missing_buckets = model.NewIntVar(Domain(0, target_active_buckets));
                LinearExpr active_with_missing;
                active_with_missing += active_bucket_sum;
                active_with_missing += missing_buckets;
                model.AddGreaterOrEqual(active_with_missing, target_active_buckets);
                objective += missing_buckets * SUBJECT_MISSING_BUCKET_WEIGHT;

                if (bucket_count >= 3 && target_active_buckets >= 2) {
                    LinearExpr segment_has[3];

                    for (int b = 0; b < bucket_count; b++) {
                        int segment = (b * 3) / bucket_count;
                        if (segment > 2) {
                            segment = 2;
                        }

                        segment_has[segment] += bucket_has[b];
                    }

                    int required_segments = std::min(
                        3,
                        CeilDiv(total, active_unit)
                    );

                    if (required_segments >= 2) {
                        IntVar missing_edges = model.NewIntVar(Domain(0, 2));
                        LinearExpr edges_with_missing;
                        edges_with_missing += segment_has[0];
                        edges_with_missing += segment_has[2];
                        edges_with_missing += missing_edges;
                        model.AddGreaterOrEqual(edges_with_missing, 2);
                        objective += missing_edges * SUBJECT_MISSING_SEGMENT_WEIGHT;
                    }

                    if (required_segments >= 3) {
                        IntVar missing_middle = model.NewIntVar(Domain(0, 1));
                        LinearExpr middle_with_missing;
                        middle_with_missing += segment_has[1];
                        middle_with_missing += missing_middle;
                        model.AddGreaterOrEqual(middle_with_missing, 1);
                        objective += missing_middle * SUBJECT_MISSING_SEGMENT_WEIGHT;
                    }
                }
            }
        }
    }
}

int CountWindows(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& busy_entities,
    int num_days
) {
    int windows = 0;

    for (const auto& busy : busy_entities) {
        for (int d = 0; d < num_days; d++) {
            int base = d * SLOTS_PER_DAY;

            for (int s = 1; s < SLOTS_PER_DAY - 1; s++) {
                bool before = false;
                bool after = false;

                for (int q = 0; q < s; q++) {
                    if (BoolValue(response, busy[base + q])) {
                        before = true;
                        break;
                    }
                }

                for (int q = s + 1; q < SLOTS_PER_DAY; q++) {
                    if (BoolValue(response, busy[base + q])) {
                        after = true;
                        break;
                    }
                }

                bool current = BoolValue(response, busy[base + s]);

                if (before && after && !current) {
                    windows++;
                }
            }
        }
    }

    return windows;
}

int CountFivePairStudentDays(
    const CpSolverResponse& response,
    const std::vector<std::vector<std::vector<BoolVar>>>& part_busy,
    int num_days
) {
    int result = 0;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                int day_sum = 0;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    if (BoolValue(response, part_busy[g][p][t])) {
                        day_sum++;
                    }
                }

                if (day_sum == MAX_STUDENT_PAIRS_PER_DAY) {
                    result++;
                }
            }
        }
    }

    return result;
}

int MaxStudentPairsInDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<std::vector<BoolVar>>>& part_busy,
    int num_days
) {
    int result = 0;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                int day_sum = 0;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    if (BoolValue(response, part_busy[g][p][t])) {
                        day_sum++;
                    }
                }

                result = std::max(result, day_sum);
            }
        }
    }

    return result;
}

int CountUpDayRuleViolations(
    const CpSolverResponse& response,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<std::vector<BoolVar>>>& part_busy,
    int num_days
) {
    int violations = 0;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                int up_slots = 0;
                int part_slots = 0;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;

                    if (BoolValue(response, part_busy[g][p][t])) {
                        part_slots++;
                    }

                    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
                        if (!lessons[l].is_block) {
                            continue;
                        }

                        if (!LessonAffectsPart(lessons[l], g, p)) {
                            continue;
                        }

                        if (BoolValue(response, x[l][t])) {
                            up_slots++;
                        }
                    }
                }

                if (up_slots > 0 && (up_slots != 2 || part_slots != 2)) {
                    violations++;
                }
            }
        }
    }

    return violations;
}

int CountUpTeacherLockViolations(
    const CpSolverResponse& response,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<Date>& all_days
) {
    int violations = 0;

    struct SelectedUp {
        int lesson_id;
        TimeInterval interval;
    };

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            std::vector<SelectedUp> selected_up;

            for (const auto& blk : blocks) {
                int up_lesson_id = blk.lesson_id;

                if (lessons[up_lesson_id].teacher != teacher) {
                    continue;
                }

                for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                    if (!BoolValue(response, blk.start_vars[i])) {
                        continue;
                    }

                    int start_t = blk.possible_starts[i];
                    int day = start_t / SLOTS_PER_DAY;

                    if (day != d) {
                        continue;
                    }

                    selected_up.push_back({
                        up_lesson_id,
                        UpIntervalForStartSlot(all_days[day], start_t % SLOTS_PER_DAY)
                    });
                }
            }

            for (int i = 0; i < static_cast<int>(selected_up.size()); i++) {
                for (int j = i + 1; j < static_cast<int>(selected_up.size()); j++) {
                    if (IntervalsOverlap(selected_up[i].interval, selected_up[j].interval)) {
                        violations++;
                    }
                }
            }

            for (const auto& up : selected_up) {
                for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
                    if (lessons[l].is_block) {
                        continue;
                    }

                    if (lessons[l].teacher != teacher) {
                        continue;
                    }

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        int t = d * SLOTS_PER_DAY + s;

                        if (!BoolValue(response, x[l][t])) {
                            continue;
                        }

                        TimeInterval pair_interval = PairSlotInterval(DayOfWeek(all_days[d]), s);
                        if (IntervalsOverlap(up.interval, pair_interval)) {
                            violations++;
                        }
                    }
                }
            }
        }
    }

    return violations;
}

}  // namespace timetable

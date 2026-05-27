#pragma once

#include <string>
#include <vector>

#include "runtime_config.h"

namespace timetable {

constexpr int SLOTS_PER_DAY = 7;

constexpr int UP_MORNING_MODEL_START_SLOT = 0;
constexpr int UP_AFTERNOON_MODEL_START_SLOT = 2;

constexpr int PARTS_PER_GROUP = 2;

int GroupCount();
int TeacherCount();
const std::string& GroupName(int index);
const std::string& TeacherName(int index);
void SetRuntimeNames(const std::vector<std::string>& groups, const std::vector<std::string>& teachers);

#define GROUPS (timetable::GroupCount())
#define TEACHERS (timetable::TeacherCount())

// Все настройки ниже вынесены в g_solver_config (см. include/runtime_config.h).
// Эти макросы оставлены для обратной совместимости c существующим кодом,
// чтобы переход на runtime-config не потребовал переписать scheduler.cpp/model_utils.cpp.
#define SOLVER_TIME_LIMIT_SECONDS (timetable::g_solver_config.solver_time_limit_seconds)
#define SOLVER_WORKERS (timetable::g_solver_config.solver_workers)
#define SOLVER_MAX_MEMORY_MB (timetable::g_solver_config.solver_max_memory_mb)
#define STOP_AFTER_FIRST_SOLUTION (timetable::g_solver_config.stop_after_first_solution)

#define MIN_STUDENT_PAIRS_PER_STUDY_DAY (timetable::g_solver_config.min_student_pairs_per_study_day)
#define MAX_STUDENT_PAIRS_PER_DAY (timetable::g_solver_config.max_student_pairs_per_day)

#define MIN_STUDENT_STUDY_DAYS_PER_WEEK (timetable::g_solver_config.min_student_study_days_per_week)
#define HARD_MIN_STUDY_DAYS_PER_WEEK (timetable::g_solver_config.hard_min_study_days_per_week)
#define GROUP_WEEK_MISSING_DAY_WEIGHT (timetable::g_solver_config.group_week_missing_day_weight)

#define SUBJECT_SPREAD_BUCKET_AVAILABLE_DAYS (timetable::g_solver_config.subject_spread_bucket_available_days)
#define MIN_SUBJECT_SPREAD_TOTAL_SLOTS (timetable::g_solver_config.min_subject_spread_total_slots)
#define SUBJECT_BUCKET_EXTRA_SLOTS (timetable::g_solver_config.subject_bucket_extra_slots)
#define SUBJECT_BUCKET_MIN_CAPACITY (timetable::g_solver_config.subject_bucket_min_capacity)
#define SUBJECT_MISSING_BUCKET_WEIGHT (timetable::g_solver_config.subject_missing_bucket_weight)
#define SUBJECT_BUCKET_OVERLOAD_WEIGHT (timetable::g_solver_config.subject_bucket_overload_weight)
#define SUBJECT_MISSING_SEGMENT_WEIGHT (timetable::g_solver_config.subject_missing_segment_weight)

#define NORMAL_SUBJECT_ACTIVE_BUCKET_UNIT (timetable::g_solver_config.normal_subject_active_bucket_unit)
#define BLOCK_SUBJECT_ACTIVE_BUCKET_UNIT (timetable::g_solver_config.block_subject_active_bucket_unit)

#define HARD_NO_STUDENT_WINDOWS (timetable::g_solver_config.hard_no_student_windows)
#define HARD_NO_TEACHER_WINDOWS (timetable::g_solver_config.hard_no_teacher_windows)
#define USE_QUALITY_OBJECTIVE (timetable::g_solver_config.use_quality_objective)
#define HARD_MIN_2_TEACHER_PAIRS_PER_DAY (timetable::g_solver_config.hard_min_2_teacher_pairs_per_day)

#define STRICT_ALL_THEORY_BEFORE_LABS (timetable::g_solver_config.strict_all_theory_before_labs)
#define MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS (timetable::g_solver_config.min_initial_theory_slots_before_labs)

#define OPTIMIZE_TEACHER_WINDOWS (timetable::g_solver_config.optimize_teacher_windows)
#define TEACHER_WINDOW_WEIGHT (timetable::g_solver_config.teacher_window_weight)

#define STUDENT_FIVE_PAIR_DAY_WEIGHT (timetable::g_solver_config.student_five_pair_day_weight)
#define STUDENT_LATE_SLOT_WEIGHT (timetable::g_solver_config.student_late_slot_weight)
#define TEACHER_LATE_SLOT_WEIGHT (timetable::g_solver_config.teacher_late_slot_weight)

constexpr int T_NOVOSELOVA = 0;
constexpr int T_DAVYDOVA = 1;
constexpr int T_NUROV = 2;
constexpr int T_POTAPOVA = 3;
constexpr int T_SERYANINA = 4;
constexpr int T_GOBOV = 5;
constexpr int T_SAMTSOVA = 6;
constexpr int T_GARBUZOV = 7;

extern std::vector<std::string> GROUP_NAME;
extern std::vector<std::string> TEACHER_NAME;
extern const std::vector<std::string> WEEKDAY_NAME;

}  // namespace timetable

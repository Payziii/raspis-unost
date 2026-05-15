#pragma once

#include <array>
#include <string>

namespace timetable {

constexpr int SLOTS_PER_DAY = 7;

constexpr int UP_MORNING_MODEL_START_SLOT = 0;
constexpr int UP_AFTERNOON_MODEL_START_SLOT = 2;

constexpr int GROUPS = 2;
constexpr int PARTS_PER_GROUP = 2;
constexpr int TEACHERS = 8;

constexpr double SOLVER_TIME_LIMIT_SECONDS = 500.0;
constexpr int SOLVER_WORKERS = 4;
constexpr double SOLVER_MAX_MEMORY_MB = 8072.0;
constexpr bool STOP_AFTER_FIRST_SOLUTION = false;

constexpr int MIN_STUDENT_PAIRS_PER_STUDY_DAY = 2;
constexpr int MAX_STUDENT_PAIRS_PER_DAY = 5;

constexpr int MIN_STUDENT_STUDY_DAYS_PER_WEEK = 2;
constexpr bool HARD_MIN_STUDY_DAYS_PER_WEEK = false;
constexpr int GROUP_WEEK_MISSING_DAY_WEIGHT = 2000;

constexpr int SUBJECT_SPREAD_BUCKET_AVAILABLE_DAYS = 12;
constexpr int MIN_SUBJECT_SPREAD_TOTAL_SLOTS = 4;
constexpr int SUBJECT_BUCKET_EXTRA_SLOTS = 2;
constexpr int SUBJECT_BUCKET_MIN_CAPACITY = 2;
constexpr int SUBJECT_MISSING_BUCKET_WEIGHT = 1200;
constexpr int SUBJECT_BUCKET_OVERLOAD_WEIGHT = 120;
constexpr int SUBJECT_MISSING_SEGMENT_WEIGHT = 1500;

constexpr int NORMAL_SUBJECT_ACTIVE_BUCKET_UNIT = 2;
constexpr int BLOCK_SUBJECT_ACTIVE_BUCKET_UNIT = 4;

constexpr bool HARD_NO_STUDENT_WINDOWS = true;
constexpr bool HARD_NO_TEACHER_WINDOWS = false;
constexpr bool USE_QUALITY_OBJECTIVE = true;
constexpr bool HARD_MIN_2_TEACHER_PAIRS_PER_DAY = false;

constexpr bool STRICT_ALL_THEORY_BEFORE_LABS = false;
constexpr int MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS = 1;

constexpr bool OPTIMIZE_TEACHER_WINDOWS = false;
constexpr int TEACHER_WINDOW_WEIGHT = 1;

constexpr int STUDENT_FIVE_PAIR_DAY_WEIGHT = 100;
constexpr int STUDENT_LATE_SLOT_WEIGHT = 1;
constexpr int TEACHER_LATE_SLOT_WEIGHT = 0;

constexpr int T_NOVOSELOVA = 0;
constexpr int T_DAVYDOVA = 1;
constexpr int T_NUROV = 2;
constexpr int T_POTAPOVA = 3;
constexpr int T_SERYANINA = 4;
constexpr int T_GOBOV = 5;
constexpr int T_SAMTSOVA = 6;
constexpr int T_GARBUZOV = 7;

extern const std::array<std::string, GROUPS> GROUP_NAME;
extern const std::array<std::string, TEACHERS> TEACHER_NAME;
extern const std::array<std::string, 7> WEEKDAY_NAME;

}  // namespace timetable

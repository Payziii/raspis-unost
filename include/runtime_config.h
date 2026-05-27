#pragma once

#include <string>
#include <vector>

#include "json_utils.h"

namespace timetable {

struct RuntimeSolverConfig {
    double solver_time_limit_seconds = 500.0;
    int solver_workers = 4;
    double solver_max_memory_mb = 8072.0;
    bool stop_after_first_solution = false;
    int linearization_level = 0;
    int symmetry_level = 2;
    int random_seed = 1;

    bool hard_no_student_windows = true;
    bool hard_no_teacher_windows = false;
    bool hard_min_study_days_per_week = false;
    bool hard_min_2_teacher_pairs_per_day = false;
    bool use_quality_objective = true;
    bool strict_all_theory_before_labs = false;
    bool optimize_teacher_windows = false;

    int group_week_missing_day_weight = 2000;
    int subject_missing_bucket_weight = 1200;
    int subject_bucket_overload_weight = 120;
    int subject_missing_segment_weight = 1500;
    int student_five_pair_day_weight = 100;
    int student_late_slot_weight = 1;
    int teacher_late_slot_weight = 0;
    int teacher_window_weight = 1;

    int min_student_pairs_per_study_day = 1;
    int max_student_pairs_per_day = 5;
    int min_student_study_days_per_week = 2;
    int subject_spread_bucket_available_days = 12;
    int min_subject_spread_total_slots = 4;
    int subject_bucket_extra_slots = 2;
    int subject_bucket_min_capacity = 2;
    int normal_subject_active_bucket_unit = 2;
    int block_subject_active_bucket_unit = 4;
    int min_initial_theory_slots_before_labs = 1;
};

extern RuntimeSolverConfig g_solver_config;

struct SolverConfigField {
    std::string key;
    std::string label;
    std::string description;
    std::string category;  // "solver" | "hard_soft" | "weights" | "shape"
    std::string type;      // "int" | "double" | "bool"
};

const std::vector<SolverConfigField>& SolverConfigSchema();

RuntimeSolverConfig DefaultSolverConfig();
void ApplySolverConfig(const RuntimeSolverConfig& cfg);
void LoadSolverConfigFromJson(const JsonValue& solver_config_json);
JsonValue SolverConfigToJson(const RuntimeSolverConfig& cfg);

void UpdateSolverConfigFromPatch(const JsonValue& patch);

}  // namespace timetable

#include "runtime_config.h"

#include <algorithm>

namespace timetable {

RuntimeSolverConfig g_solver_config = DefaultSolverConfig();

RuntimeSolverConfig DefaultSolverConfig() {
    // Дефолты подобраны под большие наборы данных (десятки групп).
    // Они отличаются от исходных constexpr-значений в include/config.h —
    // солвер должен находить FEASIBLE-решение за разумное время.
    RuntimeSolverConfig cfg;
    cfg.solver_time_limit_seconds = 1800.0;
    cfg.week_time_limit_seconds = 60.0;
    cfg.solver_workers = 4;
    cfg.solver_max_memory_mb = 8072.0;
    cfg.stop_after_first_solution = false;
    cfg.linearization_level = 0;
    cfg.symmetry_level = 2;
    cfg.random_seed = 1;

    cfg.hard_no_student_windows = false;
    cfg.hard_no_teacher_windows = false;
    cfg.hard_min_study_days_per_week = false;
    cfg.hard_min_2_teacher_pairs_per_day = false;
    cfg.use_quality_objective = false;
    cfg.strict_all_theory_before_labs = false;
    cfg.optimize_teacher_windows = false;
    cfg.optimize_student_windows = false;

    cfg.group_week_missing_day_weight = 2000;
    cfg.subject_missing_bucket_weight = 1200;
    cfg.subject_bucket_overload_weight = 120;
    cfg.subject_missing_segment_weight = 1500;
    cfg.student_five_pair_day_weight = 100;
    cfg.student_late_slot_weight = 0;
    cfg.teacher_late_slot_weight = 0;
    cfg.teacher_window_weight = 1;
    cfg.student_window_weight = 300;

    cfg.min_student_pairs_per_study_day = 1;
    cfg.max_student_pairs_per_day = 5;
    cfg.min_student_study_days_per_week = 2;
    cfg.subject_spread_bucket_available_days = 12;
    // Spread полностью отключён: ни один урок не имеет 9999 слотов,
    // поэтому штрафы spread не активируются. Чтобы включить, опусти до ~4.
    cfg.min_subject_spread_total_slots = 9999;
    cfg.subject_bucket_extra_slots = 8;
    cfg.subject_bucket_min_capacity = 2;
    cfg.normal_subject_active_bucket_unit = 2;
    cfg.block_subject_active_bucket_unit = 4;
    cfg.min_initial_theory_slots_before_labs = 1;
    return cfg;
}

void ApplySolverConfig(const RuntimeSolverConfig& cfg) {
    g_solver_config = cfg;
}

namespace {

void PullDouble(const JsonValue& obj, const std::string& key, double& dst) {
    if (!obj.IsObject() || !obj.Has(key)) return;
    const JsonValue& v = obj.At(key);
    if (v.IsNumber()) dst = v.number_value;
}

void PullInt(const JsonValue& obj, const std::string& key, int& dst) {
    if (!obj.IsObject() || !obj.Has(key)) return;
    const JsonValue& v = obj.At(key);
    if (v.IsNumber()) dst = static_cast<int>(v.number_value);
}

void PullBool(const JsonValue& obj, const std::string& key, bool& dst) {
    if (!obj.IsObject() || !obj.Has(key)) return;
    const JsonValue& v = obj.At(key);
    if (v.IsBool()) dst = v.bool_value;
    else if (v.IsNumber()) dst = v.number_value != 0.0;
}

}  // namespace

void LoadSolverConfigFromJson(const JsonValue& solver_config_json) {
    RuntimeSolverConfig cfg = DefaultSolverConfig();
    if (solver_config_json.IsObject()) {
        PullDouble(solver_config_json, "solver_time_limit_seconds", cfg.solver_time_limit_seconds);
        PullDouble(solver_config_json, "week_time_limit_seconds", cfg.week_time_limit_seconds);
        PullInt(solver_config_json, "solver_workers", cfg.solver_workers);
        PullDouble(solver_config_json, "solver_max_memory_mb", cfg.solver_max_memory_mb);
        PullBool(solver_config_json, "stop_after_first_solution", cfg.stop_after_first_solution);
        PullInt(solver_config_json, "linearization_level", cfg.linearization_level);
        PullInt(solver_config_json, "symmetry_level", cfg.symmetry_level);
        PullInt(solver_config_json, "random_seed", cfg.random_seed);

        PullBool(solver_config_json, "hard_no_student_windows", cfg.hard_no_student_windows);
        PullBool(solver_config_json, "hard_no_teacher_windows", cfg.hard_no_teacher_windows);
        PullBool(solver_config_json, "hard_min_study_days_per_week", cfg.hard_min_study_days_per_week);
        PullBool(solver_config_json, "hard_min_2_teacher_pairs_per_day", cfg.hard_min_2_teacher_pairs_per_day);
        PullBool(solver_config_json, "use_quality_objective", cfg.use_quality_objective);
        PullBool(solver_config_json, "strict_all_theory_before_labs", cfg.strict_all_theory_before_labs);
        PullBool(solver_config_json, "optimize_teacher_windows", cfg.optimize_teacher_windows);
        PullBool(solver_config_json, "optimize_student_windows", cfg.optimize_student_windows);

        PullInt(solver_config_json, "group_week_missing_day_weight", cfg.group_week_missing_day_weight);
        PullInt(solver_config_json, "subject_missing_bucket_weight", cfg.subject_missing_bucket_weight);
        PullInt(solver_config_json, "subject_bucket_overload_weight", cfg.subject_bucket_overload_weight);
        PullInt(solver_config_json, "subject_missing_segment_weight", cfg.subject_missing_segment_weight);
        PullInt(solver_config_json, "student_five_pair_day_weight", cfg.student_five_pair_day_weight);
        PullInt(solver_config_json, "student_late_slot_weight", cfg.student_late_slot_weight);
        PullInt(solver_config_json, "teacher_late_slot_weight", cfg.teacher_late_slot_weight);
        PullInt(solver_config_json, "teacher_window_weight", cfg.teacher_window_weight);
        PullInt(solver_config_json, "student_window_weight", cfg.student_window_weight);

        PullInt(solver_config_json, "min_student_pairs_per_study_day", cfg.min_student_pairs_per_study_day);
        PullInt(solver_config_json, "max_student_pairs_per_day", cfg.max_student_pairs_per_day);
        PullInt(solver_config_json, "min_student_study_days_per_week", cfg.min_student_study_days_per_week);
        PullInt(solver_config_json, "subject_spread_bucket_available_days", cfg.subject_spread_bucket_available_days);
        PullInt(solver_config_json, "min_subject_spread_total_slots", cfg.min_subject_spread_total_slots);
        PullInt(solver_config_json, "subject_bucket_extra_slots", cfg.subject_bucket_extra_slots);
        PullInt(solver_config_json, "subject_bucket_min_capacity", cfg.subject_bucket_min_capacity);
        PullInt(solver_config_json, "normal_subject_active_bucket_unit", cfg.normal_subject_active_bucket_unit);
        PullInt(solver_config_json, "block_subject_active_bucket_unit", cfg.block_subject_active_bucket_unit);
        PullInt(solver_config_json, "min_initial_theory_slots_before_labs", cfg.min_initial_theory_slots_before_labs);
    }

    cfg.subject_spread_bucket_available_days = std::max(1, cfg.subject_spread_bucket_available_days);
    cfg.solver_workers = std::max(1, cfg.solver_workers);
    cfg.solver_time_limit_seconds = std::max(1.0, cfg.solver_time_limit_seconds);
    cfg.week_time_limit_seconds = std::max(5.0, cfg.week_time_limit_seconds);
    cfg.solver_max_memory_mb = std::max(64.0, cfg.solver_max_memory_mb);

    ApplySolverConfig(cfg);
}

JsonValue SolverConfigToJson(const RuntimeSolverConfig& cfg) {
    JsonValue v = JsonValue::MakeObject();
    v.At("solver_time_limit_seconds") = JsonValue::MakeNumber(cfg.solver_time_limit_seconds);
    v.At("week_time_limit_seconds") = JsonValue::MakeNumber(cfg.week_time_limit_seconds);
    v.At("solver_workers") = JsonValue::MakeNumber(cfg.solver_workers);
    v.At("solver_max_memory_mb") = JsonValue::MakeNumber(cfg.solver_max_memory_mb);
    v.At("stop_after_first_solution") = JsonValue::MakeBool(cfg.stop_after_first_solution);
    v.At("linearization_level") = JsonValue::MakeNumber(cfg.linearization_level);
    v.At("symmetry_level") = JsonValue::MakeNumber(cfg.symmetry_level);
    v.At("random_seed") = JsonValue::MakeNumber(cfg.random_seed);

    v.At("hard_no_student_windows") = JsonValue::MakeBool(cfg.hard_no_student_windows);
    v.At("hard_no_teacher_windows") = JsonValue::MakeBool(cfg.hard_no_teacher_windows);
    v.At("hard_min_study_days_per_week") = JsonValue::MakeBool(cfg.hard_min_study_days_per_week);
    v.At("hard_min_2_teacher_pairs_per_day") = JsonValue::MakeBool(cfg.hard_min_2_teacher_pairs_per_day);
    v.At("use_quality_objective") = JsonValue::MakeBool(cfg.use_quality_objective);
    v.At("strict_all_theory_before_labs") = JsonValue::MakeBool(cfg.strict_all_theory_before_labs);
    v.At("optimize_teacher_windows") = JsonValue::MakeBool(cfg.optimize_teacher_windows);
    v.At("optimize_student_windows") = JsonValue::MakeBool(cfg.optimize_student_windows);

    v.At("group_week_missing_day_weight") = JsonValue::MakeNumber(cfg.group_week_missing_day_weight);
    v.At("subject_missing_bucket_weight") = JsonValue::MakeNumber(cfg.subject_missing_bucket_weight);
    v.At("subject_bucket_overload_weight") = JsonValue::MakeNumber(cfg.subject_bucket_overload_weight);
    v.At("subject_missing_segment_weight") = JsonValue::MakeNumber(cfg.subject_missing_segment_weight);
    v.At("student_five_pair_day_weight") = JsonValue::MakeNumber(cfg.student_five_pair_day_weight);
    v.At("student_late_slot_weight") = JsonValue::MakeNumber(cfg.student_late_slot_weight);
    v.At("teacher_late_slot_weight") = JsonValue::MakeNumber(cfg.teacher_late_slot_weight);
    v.At("teacher_window_weight") = JsonValue::MakeNumber(cfg.teacher_window_weight);
    v.At("student_window_weight") = JsonValue::MakeNumber(cfg.student_window_weight);

    v.At("min_student_pairs_per_study_day") = JsonValue::MakeNumber(cfg.min_student_pairs_per_study_day);
    v.At("max_student_pairs_per_day") = JsonValue::MakeNumber(cfg.max_student_pairs_per_day);
    v.At("min_student_study_days_per_week") = JsonValue::MakeNumber(cfg.min_student_study_days_per_week);
    v.At("subject_spread_bucket_available_days") = JsonValue::MakeNumber(cfg.subject_spread_bucket_available_days);
    v.At("min_subject_spread_total_slots") = JsonValue::MakeNumber(cfg.min_subject_spread_total_slots);
    v.At("subject_bucket_extra_slots") = JsonValue::MakeNumber(cfg.subject_bucket_extra_slots);
    v.At("subject_bucket_min_capacity") = JsonValue::MakeNumber(cfg.subject_bucket_min_capacity);
    v.At("normal_subject_active_bucket_unit") = JsonValue::MakeNumber(cfg.normal_subject_active_bucket_unit);
    v.At("block_subject_active_bucket_unit") = JsonValue::MakeNumber(cfg.block_subject_active_bucket_unit);
    v.At("min_initial_theory_slots_before_labs") = JsonValue::MakeNumber(cfg.min_initial_theory_slots_before_labs);
    return v;
}

void UpdateSolverConfigFromPatch(const JsonValue& patch) {
    if (!patch.IsObject()) return;
    RuntimeSolverConfig cfg = g_solver_config;

    PullDouble(patch, "solver_time_limit_seconds", cfg.solver_time_limit_seconds);
    PullDouble(patch, "week_time_limit_seconds", cfg.week_time_limit_seconds);
    PullInt(patch, "solver_workers", cfg.solver_workers);
    PullDouble(patch, "solver_max_memory_mb", cfg.solver_max_memory_mb);
    PullBool(patch, "stop_after_first_solution", cfg.stop_after_first_solution);
    PullInt(patch, "linearization_level", cfg.linearization_level);
    PullInt(patch, "symmetry_level", cfg.symmetry_level);
    PullInt(patch, "random_seed", cfg.random_seed);

    PullBool(patch, "hard_no_student_windows", cfg.hard_no_student_windows);
    PullBool(patch, "hard_no_teacher_windows", cfg.hard_no_teacher_windows);
    PullBool(patch, "hard_min_study_days_per_week", cfg.hard_min_study_days_per_week);
    PullBool(patch, "hard_min_2_teacher_pairs_per_day", cfg.hard_min_2_teacher_pairs_per_day);
    PullBool(patch, "use_quality_objective", cfg.use_quality_objective);
    PullBool(patch, "strict_all_theory_before_labs", cfg.strict_all_theory_before_labs);
    PullBool(patch, "optimize_teacher_windows", cfg.optimize_teacher_windows);
    PullBool(patch, "optimize_student_windows", cfg.optimize_student_windows);

    PullInt(patch, "group_week_missing_day_weight", cfg.group_week_missing_day_weight);
    PullInt(patch, "subject_missing_bucket_weight", cfg.subject_missing_bucket_weight);
    PullInt(patch, "subject_bucket_overload_weight", cfg.subject_bucket_overload_weight);
    PullInt(patch, "subject_missing_segment_weight", cfg.subject_missing_segment_weight);
    PullInt(patch, "student_five_pair_day_weight", cfg.student_five_pair_day_weight);
    PullInt(patch, "student_late_slot_weight", cfg.student_late_slot_weight);
    PullInt(patch, "teacher_late_slot_weight", cfg.teacher_late_slot_weight);
    PullInt(patch, "teacher_window_weight", cfg.teacher_window_weight);
    PullInt(patch, "student_window_weight", cfg.student_window_weight);

    PullInt(patch, "min_student_pairs_per_study_day", cfg.min_student_pairs_per_study_day);
    PullInt(patch, "max_student_pairs_per_day", cfg.max_student_pairs_per_day);
    PullInt(patch, "min_student_study_days_per_week", cfg.min_student_study_days_per_week);
    PullInt(patch, "subject_spread_bucket_available_days", cfg.subject_spread_bucket_available_days);
    PullInt(patch, "min_subject_spread_total_slots", cfg.min_subject_spread_total_slots);
    PullInt(patch, "subject_bucket_extra_slots", cfg.subject_bucket_extra_slots);
    PullInt(patch, "subject_bucket_min_capacity", cfg.subject_bucket_min_capacity);
    PullInt(patch, "normal_subject_active_bucket_unit", cfg.normal_subject_active_bucket_unit);
    PullInt(patch, "block_subject_active_bucket_unit", cfg.block_subject_active_bucket_unit);
    PullInt(patch, "min_initial_theory_slots_before_labs", cfg.min_initial_theory_slots_before_labs);

    cfg.subject_spread_bucket_available_days = std::max(1, cfg.subject_spread_bucket_available_days);
    cfg.solver_workers = std::max(1, cfg.solver_workers);
    cfg.solver_time_limit_seconds = std::max(1.0, cfg.solver_time_limit_seconds);
    cfg.week_time_limit_seconds = std::max(5.0, cfg.week_time_limit_seconds);
    cfg.solver_max_memory_mb = std::max(64.0, cfg.solver_max_memory_mb);

    ApplySolverConfig(cfg);
}

const std::vector<SolverConfigField>& SolverConfigSchema() {
    static const std::vector<SolverConfigField> schema = {
        // === Solver runtime ===
        {"solver_time_limit_seconds", "Лимит времени (монолитный), сек",
         "Максимальное время для монолитного режима. На больших задачах 1800–5000 секунд.",
         "solver", "double"},
        {"week_time_limit_seconds", "Лимит на одну неделю, сек",
         "Максимальное время, которое solver тратит на каждую неделю в недельном режиме. "
         "При UNKNOWN неделя перезапускается с ослабленными ограничениями. "
         "Рекомендуется 30–120 с.",
         "solver", "double"},
        {"solver_workers", "Параллельных воркеров",
         "Сколько потоков использует solver. Больше — быстрее, но больше памяти. Не больше числа ядер CPU.",
         "solver", "int"},
        {"solver_max_memory_mb", "Лимит памяти, МБ",
         "Если solver превысит — упадёт. Поставь чуть меньше доступной RAM.",
         "solver", "double"},
        {"stop_after_first_solution", "Стоп после первого решения",
         "true → solver сразу возвращает первое feasible решение, не оптимизирует качество. Полезно для проверки модели.",
         "solver", "bool"},
        {"linearization_level", "Уровень линеаризации",
         "0..2. Большее значение помогает на задачах с тяжёлой LP-релаксацией, но дольше предобработка.",
         "solver", "int"},
        {"symmetry_level", "Уровень симметрии",
         "0..2. Поиск и обрезка симметричных решений. 2 — максимум.",
         "solver", "int"},
        {"random_seed", "Random seed",
         "Меняй чтобы попробовать другой ход поиска. Может сильно повлиять на время.",
         "solver", "int"},

        // === Жёсткие/мягкие переключатели ===
        {"use_quality_objective", "Оптимизировать качество",
         "true = solver минимизирует штрафы (компактность, окна, поздние пары). false = ищет любое feasible. Включай только когда модель влезает в память.",
         "hard_soft", "bool"},
        {"hard_no_student_windows", "Запрет окон у студентов (HARD)",
         "Самый тяжёлый флаг — генерирует ~26 млрд ограничений на больших данных. Лучше держать false.",
         "hard_soft", "bool"},
        {"hard_no_teacher_windows", "Запрет окон у преподов (HARD)",
         "Аналогично студентам, но обычно нужно реже.",
         "hard_soft", "bool"},
        {"hard_min_study_days_per_week", "Минимум учебных дней в неделю (HARD)",
         "true = неделя обязана иметь min_student_study_days_per_week учебных дней. false = это soft (штраф).",
         "hard_soft", "bool"},
        {"hard_min_2_teacher_pairs_per_day", "Минимум 2 пары у препода в день (HARD)",
         "true = если препод приходит — даём ему минимум 2 пары. Очень жёстко.",
         "hard_soft", "bool"},
        {"strict_all_theory_before_labs", "Вся теория до лаб (HARD)",
         "true = ни одна лаба не идёт до теории. false = хотя бы min_initial_theory_slots_before_labs теоретических до первой лабы.",
         "hard_soft", "bool"},
        {"optimize_teacher_windows", "Оптимизировать окна у преподов",
         "Включает штраф за окна у преподов. Только если use_quality_objective=true.",
         "hard_soft", "bool"},
        {"optimize_student_windows", "Оптимизировать окна у студентов (soft)",
         "Включает штраф за окна у студентов через objective. Только если use_quality_objective=true и hard_no_student_windows=false.",
         "hard_soft", "bool"},

        // === Веса ===
        {"group_week_missing_day_weight", "Штраф: пропущенный учебный день недели",
         "Чем выше — тем сильнее solver старается уложить min_student_study_days_per_week.",
         "weights", "int"},
        {"subject_missing_bucket_weight", "Штраф: пропущенный bucket предмета",
         "Контролирует, чтобы предмет не пропадал на несколько недель подряд.",
         "weights", "int"},
        {"subject_bucket_overload_weight", "Штраф: перегруз bucket предмета",
         "Контролирует, чтобы предмет не собирался плотным комком.",
         "weights", "int"},
        {"subject_missing_segment_weight", "Штраф: пропуск сегмента",
         "Дополнительный штраф за пропущенные краевые сегменты семестра.",
         "weights", "int"},
        {"student_five_pair_day_weight", "Штраф: 5 пар у студента в день",
         "Чем выше — тем меньше «убойных» дней у студентов.",
         "weights", "int"},
        {"student_late_slot_weight", "Штраф: поздние пары у студентов",
         "Линейно растёт с номером пары. 0 = solver не предпочитает ранние пары (экономит ~67k термов цели).",
         "weights", "int"},
        {"teacher_late_slot_weight", "Штраф: поздние пары у преподов",
         "Аналогично студентам, обычно держим 0.",
         "weights", "int"},
        {"teacher_window_weight", "Штраф: окно у препода",
         "Только при optimize_teacher_windows=true.",
         "weights", "int"},
        {"student_window_weight", "Штраф: окно у студента",
         "Только при optimize_student_windows=true. Рекомендуемый диапазон: 200–600.",
         "weights", "int"},

        // === Размерности ===
        {"min_student_pairs_per_study_day", "Мин. пар у студента в день",
         "Если подгруппа учится в этот день — даём ей минимум столько пар.",
         "shape", "int"},
        {"max_student_pairs_per_day", "Макс. пар у студента в день",
         "Жёсткий верхний предел.",
         "shape", "int"},
        {"min_student_study_days_per_week", "Мин. учебных дней в неделю",
         "Целевое значение (hard или soft в зависимости от флага выше).",
         "shape", "int"},
        {"subject_spread_bucket_available_days", "Размер bucket-а в днях",
         "Сколько доступных дней в одном bucket-е предмета. 12 ≈ 2 недели.",
         "shape", "int"},
        {"min_subject_spread_total_slots", "Мин. слотов чтобы применять spread",
         "Предметы с total_slots ниже не получают bucket-ограничений.",
         "shape", "int"},
        {"subject_bucket_extra_slots", "Допуск bucket-а",
         "Сколько слотов сверх «среднего на bucket» допустимо без штрафа. Больше = мягче spread.",
         "shape", "int"},
        {"subject_bucket_min_capacity", "Минимальная вместимость bucket-а",
         "Нижняя граница soft_max_per_bucket.",
         "shape", "int"},
        {"normal_subject_active_bucket_unit", "Юнит активного bucket-а: обычные",
         "Сколько слотов «оплачивают» один активный bucket для обычных предметов.",
         "shape", "int"},
        {"block_subject_active_bucket_unit", "Юнит активного bucket-а: блочные (УП)",
         "То же для УП-практик.",
         "shape", "int"},
        {"min_initial_theory_slots_before_labs", "Мин. теории до первой лабы",
         "Сколько слотов теории должно быть в первом bucket-е перед лабами.",
         "shape", "int"},
    };
    return schema;
}

}  // namespace timetable

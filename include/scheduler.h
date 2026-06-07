#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "types.h"

namespace timetable {

struct GenerationResult {
    bool success = false;
    std::string status;
    std::string message;
    std::string output_dir;
};

struct LockedAssignment {
    int lesson_id = -1;
    Date date{};
    int slot = -1;
};

struct GenerationOptions {
    std::vector<LockedAssignment> locked;
    std::string lock_source;  // "none" | "manual" | "auto" — для диагностики
};

// Колбэки для недельной генерации (прогресс + отмена)
struct WeeklyGenCallbacks {
    // Если cancel_flag != nullptr и *cancel_flag == true, генерация прерывается между неделями
    std::atomic<bool>* cancel_flag = nullptr;

    // Вызывается после завершения/ошибки каждой недели
    // week_idx (0-based), total_weeks, date_from, date_to, status ("done"|"failed"|"skipped"), elapsed_s
    std::function<void(int, int, std::string, std::string, std::string, double)> on_week_done;

    // Вызывается в начале каждой недели
    // week_idx (0-based), total_weeks, date_from, date_to
    std::function<void(int, int, std::string, std::string)> on_week_start;
};

GenerationResult GenerateSchedule(const std::string& output_dir);
GenerationResult GenerateSchedule(const std::string& output_dir, const GenerationOptions& options);

// Генерация по неделям: отдельная маленькая CP-SAT задача на каждую неделю.
// Квоты из Брезенхема гарантируют корректное распределение нагрузки.
// После каждой успешной недели автоматически сохраняет частичное расписание.
GenerationResult GenerateScheduleWeekly(const std::string& output_dir);
GenerationResult GenerateScheduleWeekly(const std::string& output_dir, const GenerationOptions& options);
GenerationResult GenerateScheduleWeekly(const std::string& output_dir, const GenerationOptions& options, const WeeklyGenCallbacks& callbacks);

int RunScheduler();

}  // namespace timetable

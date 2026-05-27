#pragma once

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

GenerationResult GenerateSchedule(const std::string& output_dir);
GenerationResult GenerateSchedule(const std::string& output_dir, const GenerationOptions& options);
int RunScheduler();

}  // namespace timetable

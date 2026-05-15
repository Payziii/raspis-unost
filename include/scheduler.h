#pragma once

#include <string>

namespace timetable {

struct GenerationResult {
    bool success = false;
    std::string status;
    std::string message;
    std::string output_dir;
};

GenerationResult GenerateSchedule(const std::string& output_dir);
int RunScheduler();

}  // namespace timetable

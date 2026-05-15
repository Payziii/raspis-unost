#pragma once

#include <map>
#include <utility>
#include <vector>

#include "types.h"

namespace timetable {

bool ValidateInputLessons(const std::vector<Lesson>& lessons);

void PrintInputDiagnostics(
    const std::vector<Lesson>& lessons,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const Date& start_date
);

}  // namespace timetable

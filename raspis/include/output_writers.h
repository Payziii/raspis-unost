#pragma once

#include <string>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"

#include "types.h"

namespace timetable {

std::string BuildGroupSlotText(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<std::vector<operations_research::sat::IntVar>>& group_day_campus,
    int group,
    int day,
    int slot
);

std::string BuildTeacherSlotText(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<operations_research::sat::IntVar>>& teacher_day_campus,
    int teacher,
    int day,
    int slot
);

bool HasGroupDay(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& group_busy,
    int group,
    int day
);

bool HasTeacherDay(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& teacher_busy,
    int teacher,
    int day
);

void WriteGroupScheduleTxt(
    const std::string& file_name,
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& group_busy,
    const std::vector<std::vector<operations_research::sat::IntVar>>& group_day_campus,
    int group
);

void WriteAllGroupsTxt(
    const std::string& file_name,
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& group_busy,
    const std::vector<std::vector<operations_research::sat::IntVar>>& group_day_campus
);

void WriteGroupsCsv(
    const std::string& file_name,
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& group_busy,
    const std::vector<std::vector<operations_research::sat::IntVar>>& group_day_campus
);

void WriteTeachersTxt(
    const std::string& file_name,
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& teacher_busy,
    const std::vector<std::vector<operations_research::sat::IntVar>>& teacher_day_campus
);

}  // namespace timetable

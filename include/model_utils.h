#pragma once

#include <map>
#include <utility>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"

#include "types.h"

namespace timetable {

operations_research::sat::BoolVar MakePositiveIndicator(
    operations_research::sat::CpModelBuilder& model,
    const operations_research::sat::LinearExpr& sum
);

void AddMinIfPositive(
    operations_research::sat::CpModelBuilder& model,
    const operations_research::sat::LinearExpr& day_sum,
    const operations_research::sat::BoolVar& has,
    int minimum_value
);

void AddMin2IfPositive(
    operations_research::sat::CpModelBuilder& model,
    const operations_research::sat::LinearExpr& day_sum
);

void AddNoWindowsHard(
    operations_research::sat::CpModelBuilder& model,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& busy_entities,
    int num_days
);

std::vector<operations_research::sat::BoolVar> CreateWindowPenaltyVars(
    operations_research::sat::CpModelBuilder& model,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& busy_entities,
    int num_days
);

void AddSubjectSpreadPenalties(
    operations_research::sat::CpModelBuilder& model,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    operations_research::sat::LinearExpr& objective
);

int CountWindows(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& busy_entities,
    int num_days
);

int CountFivePairStudentDays(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<std::vector<std::vector<operations_research::sat::BoolVar>>>& part_busy,
    int num_days
);

int MaxStudentPairsInDay(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<std::vector<std::vector<operations_research::sat::BoolVar>>>& part_busy,
    int num_days
);

int CountUpDayRuleViolations(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<std::vector<std::vector<operations_research::sat::BoolVar>>>& part_busy,
    int num_days
);

int CountUpTeacherLockViolations(
    const operations_research::sat::CpSolverResponse& response,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<Date>& all_days
);

}  // namespace timetable

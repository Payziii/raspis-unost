#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"

#include "types.h"

namespace timetable {

std::string CampusName(int campus);
std::string SubgroupName(int subgroup);
std::string Join(const std::vector<std::string>& parts, const std::string& sep);

void WriteUtf8Bom(std::ofstream& out);
std::string CsvEscape(const std::string& s);

bool BoolValue(
    const operations_research::sat::CpSolverResponse& response,
    const operations_research::sat::BoolVar& v
);

int IntValue(
    const operations_research::sat::CpSolverResponse& response,
    const operations_research::sat::IntVar& v
);

}  // namespace timetable

#include "format_utils.h"

#include <sstream>

namespace timetable {

using operations_research::sat::BoolVar;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::IntVar;
using operations_research::sat::SolutionIntegerValue;

std::string CampusName(int campus) {
    return campus == LESNAYA ? "Лесная" : "Кривоусова";
}

std::string SubgroupName(int subgroup) {
    if (subgroup == -1) {
        return "вся группа";
    }

    return (subgroup % 2 == 0) ? "1 п/г" : "2 п/г";
}

std::string Join(const std::vector<std::string>& parts, const std::string& sep) {
    std::ostringstream ss;

    for (int i = 0; i < static_cast<int>(parts.size()); i++) {
        if (i > 0) ss << sep;
        ss << parts[i];
    }

    return ss.str();
}

void WriteUtf8Bom(std::ofstream& out) {
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), 3);
}

std::string CsvEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 2);

    result += "\"";

    for (char ch : s) {
        if (ch == '"') {
            result += "\"\"";
        } else {
            result += ch;
        }
    }

    result += "\"";
    return result;
}

bool BoolValue(const CpSolverResponse& response, const BoolVar& v) {
    return SolutionIntegerValue(response, v) != 0;
}

int IntValue(const CpSolverResponse& response, const IntVar& v) {
    return static_cast<int>(SolutionIntegerValue(response, v));
}

}  // namespace timetable

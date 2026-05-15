#pragma once

#include <string>

namespace timetable {

int RunApiServer(const std::string& host, int port, const std::string& output_dir);

}  // namespace timetable

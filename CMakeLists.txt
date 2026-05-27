cmake_minimum_required(VERSION 3.20)

project(timetable_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OR-Tools должен быть установлен в системе или доступен через CMAKE_PREFIX_PATH.
find_package(ortools CONFIG REQUIRED)

add_executable(timetable_solver
    src/main.cpp
    src/config.cpp
    src/types.cpp
    src/date_utils.cpp
    src/format_utils.cpp
    src/model_utils.cpp
    src/diagnostics.cpp
    src/output_writers.cpp
    src/lessons_data.cpp
    src/scheduler.cpp
    src/http_server.cpp
    src/json_utils.cpp
    src/data_store.cpp
    src/runtime_config.cpp
)

target_include_directories(timetable_solver PRIVATE include)

target_link_libraries(timetable_solver PRIVATE ortools::ortools)

if (WIN32)
    target_link_libraries(timetable_solver PRIVATE ws2_32)
endif()

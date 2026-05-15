#include "lessons_data.h"

#include <iostream>

#include "data_store.h"

namespace timetable {

std::vector<Lesson> CreateLessons() {
    ScheduleInputData data;
    std::string error;
    if (!LoadScheduleInputData(data, error)) {
        std::cerr << "Не удалось загрузить data/timetable_data.json: " << error << "\n";
        return {};
    }
    return data.lessons;
}

}  // namespace timetable

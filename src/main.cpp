#include <clocale>

#include "scheduler.h"

int main() {
    std::setlocale(LC_ALL, "ru_RU.UTF-8");
    return timetable::RunScheduler();
}

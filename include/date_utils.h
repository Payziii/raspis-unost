#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "types.h"

namespace timetable {

int DayOfWeek(const Date& d);
int MakeMinute(int hour, int minute);
bool IntervalsOverlap(const TimeInterval& a, const TimeInterval& b);

TimeInterval PairSlotInterval(int day_of_week, int slot);
bool IsAllowedUpStartSlot(const Date& d, int slot);
TimeInterval UpIntervalForStartSlot(const Date& d, int start_slot);

std::vector<int> TeacherBlockedSlotsForUpStart(
    const std::vector<Date>& all_days,
    int start_t
);

std::string MinuteToString(int minute_of_day);
std::string IntervalToString(const TimeInterval& interval);
std::string PairSlotLabel(const Date& d, int slot);
std::string UpIntervalLabelForStartSlot(const Date& d, int start_slot);
std::string UpShiftLabelForDisplaySlot(const Date& d, int slot);

int DaysInMonth(int month, int year);
Date NextDay(const Date& d);
int DaysBetween(const Date& from, const Date& to);
int WeekIndexFromStart(const Date& start, const Date& d);
std::vector<Date> GenerateSchoolDays(const Date& start, const Date& end);

bool IsAvailable(
    const Date& d,
    int group,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable
);

std::string DateToString(const Date& d);

std::vector<std::vector<int>> BuildAvailableDayBuckets(
    int group,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable
);

int CeilDiv(int a, int b);

}  // namespace timetable

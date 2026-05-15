#include "date_utils.h"

#include <iomanip>
#include <sstream>

#include "config.h"

namespace timetable {

int DayOfWeek(const Date& d) {
    int m = d.month;
    int y = d.year;

    if (m < 3) {
        m += 12;
        y--;
    }

    int K = y % 100;
    int J = y / 100;

    int h = (d.day + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 - 2 * J) % 7;
    if (h < 0) h += 7;

    return ((h + 5) % 7) + 1;
}

int MakeMinute(int hour, int minute) {
    return hour * 60 + minute;
}

bool IntervalsOverlap(const TimeInterval& a, const TimeInterval& b) {
    return a.from_minute < b.to_minute && b.from_minute < a.to_minute;
}

TimeInterval PairSlotInterval(int day_of_week, int slot) {
    if (slot < 0 || slot >= SLOTS_PER_DAY) {
        return {0, 0};
    }

    if (day_of_week == 1) {
        static const TimeInterval monday[SLOTS_PER_DAY] = {
            {MakeMinute(9, 15),  MakeMinute(10, 40)},
            {MakeMinute(10, 50), MakeMinute(12, 15)},
            {MakeMinute(13, 10), MakeMinute(14, 35)},
            {MakeMinute(14, 45), MakeMinute(16, 10)},
            {MakeMinute(16, 20), MakeMinute(17, 40)},
            {MakeMinute(17, 50), MakeMinute(19, 10)},
            {MakeMinute(19, 20), MakeMinute(20, 40)}
        };

        return monday[slot];
    }

    if (day_of_week >= 2 && day_of_week <= 5) {
        static const TimeInterval weekday[SLOTS_PER_DAY] = {
            {MakeMinute(8, 30),  MakeMinute(9, 55)},
            {MakeMinute(10, 5),  MakeMinute(11, 30)},
            {MakeMinute(12, 25), MakeMinute(13, 50)},
            {MakeMinute(14, 0),  MakeMinute(15, 25)},
            {MakeMinute(15, 35), MakeMinute(16, 55)},
            {MakeMinute(17, 5),  MakeMinute(18, 25)},
            {MakeMinute(18, 35), MakeMinute(19, 55)}
        };

        return weekday[slot];
    }

    if (day_of_week == 6) {
        static const TimeInterval saturday[SLOTS_PER_DAY] = {
            {MakeMinute(8, 30),  MakeMinute(9, 45)},
            {MakeMinute(9, 55),  MakeMinute(11, 10)},
            {MakeMinute(11, 30), MakeMinute(12, 45)},
            {MakeMinute(12, 55), MakeMinute(14, 10)},
            {MakeMinute(14, 20), MakeMinute(15, 30)},
            {MakeMinute(15, 40), MakeMinute(16, 50)},
            {MakeMinute(17, 0),  MakeMinute(18, 10)}
        };

        return saturday[slot];
    }

    return {0, 0};
}

bool IsAllowedUpStartSlot(const Date& d, int slot) {
    int day_of_week = DayOfWeek(d);

    if (day_of_week == 7) {
        return false;
    }

    return slot == UP_MORNING_MODEL_START_SLOT ||
        slot == UP_AFTERNOON_MODEL_START_SLOT;
}

TimeInterval UpIntervalForStartSlot(const Date& d, int start_slot) {
    int day_of_week = DayOfWeek(d);
    bool morning = (start_slot == UP_MORNING_MODEL_START_SLOT);

    if (day_of_week == 1) {
        if (morning) {
            return {MakeMinute(9, 0), MakeMinute(13, 0)};
        }

        return {MakeMinute(13, 30), MakeMinute(17, 30)};
    }

    if (day_of_week >= 2 && day_of_week <= 5) {
        if (morning) {
            return {MakeMinute(8, 30), MakeMinute(12, 30)};
        }

        return {MakeMinute(13, 0), MakeMinute(17, 0)};
    }

    if (day_of_week == 6) {
        if (morning) {
            return {MakeMinute(8, 30), MakeMinute(12, 0)};
        }

        return {MakeMinute(12, 30), MakeMinute(16, 30)};
    }

    return {0, 0};
}

std::vector<int> TeacherBlockedSlotsForUpStart(
    const std::vector<Date>& all_days,
    int start_t
) {
    std::vector<int> blocked_slots;

    int day = start_t / SLOTS_PER_DAY;
    int start_slot = start_t % SLOTS_PER_DAY;

    if (day < 0 || day >= static_cast<int>(all_days.size())) {
        return blocked_slots;
    }

    const Date& d = all_days[day];
    int day_of_week = DayOfWeek(d);
    TimeInterval up_interval = UpIntervalForStartSlot(d, start_slot);

    for (int s = 0; s < SLOTS_PER_DAY; s++) {
        TimeInterval pair_interval = PairSlotInterval(day_of_week, s);

        if (IntervalsOverlap(up_interval, pair_interval)) {
            blocked_slots.push_back(day * SLOTS_PER_DAY + s);
        }
    }

    return blocked_slots;
}

std::string MinuteToString(int minute_of_day) {
    std::ostringstream ss;
    ss << std::setfill('0')
       << std::setw(2) << (minute_of_day / 60)
       << ":"
       << std::setw(2) << (minute_of_day % 60);
    return ss.str();
}

std::string IntervalToString(const TimeInterval& interval) {
    return MinuteToString(interval.from_minute) + "-" +
        MinuteToString(interval.to_minute);
}

std::string PairSlotLabel(const Date& d, int slot) {
    std::ostringstream ss;
    ss << slot + 1 << " пара ("
       << IntervalToString(PairSlotInterval(DayOfWeek(d), slot))
       << ")";
    return ss.str();
}

std::string UpIntervalLabelForStartSlot(const Date& d, int start_slot) {
    std::string shift_name =
        start_slot == UP_MORNING_MODEL_START_SLOT ? "утро" : "день";

    return "УП " + shift_name + " " +
        IntervalToString(UpIntervalForStartSlot(d, start_slot));
}

std::string UpShiftLabelForDisplaySlot(const Date& d, int slot) {
    if (slot == UP_MORNING_MODEL_START_SLOT ||
        slot == UP_MORNING_MODEL_START_SLOT + 1) {
        return UpIntervalLabelForStartSlot(d, UP_MORNING_MODEL_START_SLOT);
    }

    if (slot == UP_AFTERNOON_MODEL_START_SLOT ||
        slot == UP_AFTERNOON_MODEL_START_SLOT + 1) {
        return UpIntervalLabelForStartSlot(d, UP_AFTERNOON_MODEL_START_SLOT);
    }

    return "";
}

int DaysInMonth(int month, int year) {
    static const int days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month == 2 &&
        (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        return 29;
    }

    return days[month - 1];
}

Date NextDay(const Date& d) {
    Date nd = d;
    nd.day++;

    if (nd.day > DaysInMonth(nd.month, nd.year)) {
        nd.day = 1;
        nd.month++;

        if (nd.month > 12) {
            nd.month = 1;
            nd.year++;
        }
    }

    return nd;
}

int DaysBetween(const Date& from, const Date& to) {
    int result = 0;
    Date cur = from;

    while (cur < to) {
        cur = NextDay(cur);
        result++;
    }

    return result;
}

int WeekIndexFromStart(const Date& start, const Date& d) {
    return DaysBetween(start, d) / 7;
}

std::vector<Date> GenerateSchoolDays(const Date& start, const Date& end) {
    std::vector<Date> days;
    Date d = start;

    while (d <= end) {
        if (DayOfWeek(d) != 7) {
            days.push_back(d);
        }

        d = NextDay(d);
    }

    return days;
}

bool IsAvailable(
    const Date& d,
    int group,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable
) {
    auto it = unavailable.find(group);
    if (it == unavailable.end()) return true;

    for (const auto& range : it->second) {
        const Date& from = range.first;
        const Date& to = range.second;

        if (d >= from && d <= to) {
            return false;
        }
    }

    return true;
}

std::string DateToString(const Date& d) {
    std::ostringstream ss;
    ss << std::setfill('0')
       << std::setw(2) << d.day << "."
       << std::setw(2) << d.month << "."
       << d.year;

    return ss.str();
}

std::vector<std::vector<int>> BuildAvailableDayBuckets(
    int group,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable
) {
    std::vector<std::vector<int>> buckets;
    int available_index = 0;

    for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
        if (!IsAvailable(all_days[d], group, unavailable)) {
            continue;
        }

        if (available_index % SUBJECT_SPREAD_BUCKET_AVAILABLE_DAYS == 0) {
            buckets.push_back(std::vector<int>());
        }

        buckets.back().push_back(d);
        available_index++;
    }

    return buckets;
}

int CeilDiv(int a, int b) {
    return (a + b - 1) / b;
}

}  // namespace timetable

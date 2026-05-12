#include <algorithm>
#include <array>
#include <clocale>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/sat/model.h"

using namespace operations_research;
using namespace operations_research::sat;

// ====================== Настройки ======================

constexpr int SLOTS_PER_DAY = 7;

// В модели остаётся 7 пар в день. Поднятие флага и 0 урок здесь
// не используются и не добавляются.
// УП в расписании показывается как 2 соседние пары:
// утренний блок — пары 1-2, дневной блок — пары 3-4.
// Дополнительно преподаватель блокируется на реальное время УП
// по расписанию звонков.
constexpr int UP_MORNING_MODEL_START_SLOT = 0;
constexpr int UP_AFTERNOON_MODEL_START_SLOT = 2;

constexpr int GROUPS = 11;
constexpr int PARTS_PER_GROUP = 2;
constexpr int TEACHERS = 41;

constexpr int G_ISP_3304 = 0;
constexpr int G_ISP_3305P = 1;
constexpr int G_TAKHCS_2201 = 2;
constexpr int G_MCM_201 = 3;
constexpr int G_TEO_2501 = 4;
constexpr int G_SP_2601 = 5;
constexpr int G_SP_2602P = 6;
constexpr int G_TORD_2706 = 7;
constexpr int G_TORD_2707P = 8;
constexpr int G_TM_2415 = 9;
constexpr int G_TM_2416P = 10;

constexpr double SOLVER_TIME_LIMIT_SECONDS = 500.0;
constexpr int SOLVER_WORKERS = 4;
constexpr double SOLVER_MAX_MEMORY_MB = 8072.0;
constexpr bool STOP_AFTER_FIRST_SOLUTION = true;

constexpr int MIN_STUDENT_PAIRS_PER_STUDY_DAY = 2;
constexpr int MAX_STUDENT_PAIRS_PER_DAY = 5;
constexpr int MIN_STUDENT_STUDY_DAYS_PER_WEEK = 2;
constexpr bool HARD_MIN_STUDY_DAYS_PER_WEEK = false;
constexpr int GROUP_WEEK_MISSING_DAY_WEIGHT = 2000;

constexpr int SUBJECT_SPREAD_BUCKET_AVAILABLE_DAYS = 12;
constexpr int MIN_SUBJECT_SPREAD_TOTAL_SLOTS = 4;
constexpr int SUBJECT_BUCKET_EXTRA_SLOTS = 4;
constexpr int SUBJECT_BUCKET_MIN_CAPACITY = 1;
constexpr int SUBJECT_MISSING_BUCKET_WEIGHT = 1200;
constexpr int SUBJECT_BUCKET_OVERLOAD_WEIGHT = 120;
constexpr int SUBJECT_MISSING_SEGMENT_WEIGHT = 1500;

constexpr int NORMAL_SUBJECT_ACTIVE_BUCKET_UNIT = 2;
constexpr int BLOCK_SUBJECT_ACTIVE_BUCKET_UNIT = 4;

constexpr bool HARD_NO_STUDENT_WINDOWS = true;
constexpr bool HARD_NO_TEACHER_WINDOWS = false;
constexpr bool USE_QUALITY_OBJECTIVE = true;
constexpr bool HARD_MIN_2_TEACHER_PAIRS_PER_DAY = false;
constexpr bool STRICT_ALL_THEORY_BEFORE_LABS = false;
constexpr int MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS = 1;
constexpr bool OPTIMIZE_TEACHER_WINDOWS = false;
constexpr int TEACHER_WINDOW_WEIGHT = 1;
constexpr int STUDENT_FIVE_PAIR_DAY_WEIGHT = 100;
constexpr int STUDENT_LATE_SLOT_WEIGHT = 1;
constexpr int TEACHER_LATE_SLOT_WEIGHT = 0;

constexpr bool LIMIT_VISIBLE_GROUP_PAIRS_PER_DAY = false;
constexpr bool FORCE_BOTH_SUBGROUPS_SAME_STUDY_DAYS = false;
constexpr bool ENFORCE_THEORY_BEFORE_LABS = false;
constexpr bool ENFORCE_DAY_CAMPUS_RULES = false;

// Главная разгрузка поиска: УП ставим жадно до CP-SAT и фиксируем старты.
// Это убирает тысячи симметричных переменных выбора УП и резко ускоряет поиск.
constexpr bool FIX_UP_STARTS_GREEDY = true;


// Индексы преподавателей
constexpr int T_NOVOSELOVA = 0;
constexpr int T_DAVYDOVA = 1;
constexpr int T_NUROV = 2;
constexpr int T_POTAPOVA = 3;
constexpr int T_SERYANINA = 4;
constexpr int T_GOBOV = 5;
constexpr int T_SAMTSOVA = 6;
constexpr int T_GARBUZOV = 7;
constexpr int T_TSIMFER = 8;
constexpr int T_SINELNIKOVA = 9;
constexpr int T_NIFONTOVA = 10;
constexpr int T_AZARYAN = 11;
constexpr int T_KALCHEVSKAYA = 12;
constexpr int T_SEMENOVA = 13;
constexpr int T_DINMUKHAMETOV = 14;
constexpr int T_ALSHAEVA = 15;
constexpr int T_KROPOTOVA = 16;
constexpr int T_SOBOLEVA = 17;
constexpr int T_ELAGINA = 18;
constexpr int T_POPOVA = 19;
constexpr int T_GALUZIN = 20;
constexpr int T_KOSTAREVA = 21;
constexpr int T_SADRIEVA = 22;
constexpr int T_PISMAK = 23;
constexpr int T_KOSHELEV = 24;
constexpr int T_SHABUROV = 25;
constexpr int T_ERMOLINA = 26;
constexpr int T_ABRAMCHUK = 27;
constexpr int T_SIVILKAEV = 28;
constexpr int T_AKHMETOV = 29;
constexpr int T_PODCHINENNOV = 30;
constexpr int T_GORIN = 31;
constexpr int T_VERHNEV = 32;
constexpr int T_LIMONOVA = 33;
constexpr int T_OTRAK = 34;
constexpr int T_SALAMATINA = 35;
constexpr int T_SAMTSOV = 36;
constexpr int T_TIMEROV = 37;
constexpr int T_SIMAKOV = 38;
constexpr int T_VALDIYANOV = 39;
constexpr int T_KLABUKOV = 40;

const std::array<std::string, GROUPS> GROUP_NAME = {
    "ИСП-3304",
    "ИСП-3305п",
    "ТАКХС-Пф-2201",
    "МЦМ-Пф-201",
    "ТЭО-Пф-2501",
    "СП-Пф-2601",
    "СП-Пф-2602п",
    "ТОРД-2706",
    "ТОРД-2707п",
    "ТМ-2415",
    "ТМ-2416п"
};

const std::array<std::string, GROUPS> GROUP_FILE_NAME = {
    "ISP-3304",
    "ISP-3305p",
    "TAKHCS-Pf-2201",
    "MCM-Pf-201",
    "TEO-Pf-2501",
    "SP-Pf-2601",
    "SP-Pf-2602p",
    "TORD-2706",
    "TORD-2707p",
    "TM-2415",
    "TM-2416p"
};

// Для диагностики можно временно отключать группы, не меняя остальной код.
// Например, сначала оставить true только у ИСП, потом добавлять новые группы по одной.
const std::array<bool, GROUPS> GROUP_ENABLED = {
    true,  // ИСП-3304
    true,  // ИСП-3305п
    true,  // ТАКХС-Пф-2201
    true,  // МЦМ-Пф-201
    true,  // ТЭО-Пф-2501
    true,  // СП-Пф-2601
    true,  // СП-Пф-2602п
    true,  // ТОРД-2706
    true,  // ТОРД-2707п
    true,  // ТМ-2415
    true   // ТМ-2416п
};

const std::array<std::string, TEACHERS> TEACHER_NAME = {
    "Новосёлова Светлана Юрьевна",
    "Давыдова Валентина Алексеевна",
    "Нуров Мирзо Нуралиевич",
    "Потапова Регина Александровна",
    "Серянина",
    "Гобов",
    "Самцова",
    "Гарбузов Андрей Евгеньевич",
    "Цимфер Татьяна Ивановна",
    "Синельникова Елена Владимировна",
    "Нифонтова Ирина Геннадьевна",
    "Азарян Карине Айказовна",
    "Кальчевская Наталья Владимировна",
    "Семенова Лилиана Ивановна",
    "Динмухаметов Владислав Дуферович",
    "Альшаева Алина Павловна",
    "Кропотова Анастасия Андреевна",
    "Соболева Любовь Анатольевна",
    "Елагина Ольга Александровна",
    "Попова Татьяна Вильевна",
    "Галузин Антон Илюсович",
    "Костарева Наталья Викторовна",
    "Садриева Татьяна Геннадьевна",
    "Письмак Владимир Николаевич",
    "Кошелев Дмитрий Александрович",
    "Шабуров Анатолий Анатольевич",
    "Ермолина Ирина Павловна",
    "Абрамчук Раида Филимазовна",
    "Сивилькаев Вадим Михайлович",
    "Ахметов Артур Фанависович",
    "Подчиненнов Александр Юрьевич",
    "Горин Максим Валерьевич",
    "Верхнев Николай Александрович",
    "Лимонова Евгения Николаевна",
    "Отрак Елена Сергеевна",
    "Саламатина Вера Викторовна",
    "Самцов Андрей Евгеньевич",
    "Тимеров Валерий Вагизович",
    "Симаков Семен Данилович",
    "Вальдиянов Ян Мансурович",
    "Клабуков Василий Витальевич"
};

const std::array<std::string, 7> WEEKDAY_NAME = {
    "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ", "ВС"
};

enum Campus {
    LESNAYA = 0,
    KRIVOUSOVA = 1
};

struct Date {
    int year;
    int month;
    int day;

    bool operator<(const Date& o) const {
        if (year != o.year) return year < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }

    bool operator==(const Date& o) const {
        return year == o.year && month == o.month && day == o.day;
    }

    bool operator<=(const Date& o) const {
        return *this < o || *this == o;
    }

    bool operator>(const Date& o) const {
        return o < *this;
    }

    bool operator>=(const Date& o) const {
        return !(*this < o);
    }
};

struct Lesson {
    int id;
    int group;
    int subgroup;
    int teacher;
    int total_slots;
    std::string name;
    int subject_id;
    bool is_lab;
    bool is_block;
    std::set<Campus> allowed_campuses;
};

struct BlockInfo {
    int lesson_id;
    std::vector<int> possible_starts;
    std::vector<BoolVar> start_vars;
};

std::string SubjectFamilyKey(const Lesson& lesson) {
    if (lesson.subject_id >= 0) {
        return "subject_" + std::to_string(lesson.subject_id);
    }

    return lesson.name;
}

bool LessonAffectsPart(const Lesson& lesson, int group, int part) {
    if (lesson.group != group) {
        return false;
    }

    if (lesson.subgroup == -1) {
        return true;
    }

    int base_subgroup = group * PARTS_PER_GROUP;
    return lesson.subgroup == base_subgroup + part;
}

// ====================== Даты ======================

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

struct TimeInterval {
    int from_minute;
    int to_minute;
};

int MakeMinute(int hour, int minute) {
    return hour * 60 + minute;
}

bool IntervalsOverlap(const TimeInterval& a, const TimeInterval& b) {
    return a.from_minute < b.to_minute && b.from_minute < a.to_minute;
}

TimeInterval PairSlotInterval(int day_of_week, int slot) {
    if (slot < 0 || slot >= SLOTS_PER_DAY) {
        return { 0, 0 };
    }

    if (day_of_week == 1) {
        static const TimeInterval monday[SLOTS_PER_DAY] = {
            { MakeMinute(9, 15),  MakeMinute(10, 40) },
            { MakeMinute(10, 50), MakeMinute(12, 15) },
            { MakeMinute(13, 10), MakeMinute(14, 35) },
            { MakeMinute(14, 45), MakeMinute(16, 10) },
            { MakeMinute(16, 20), MakeMinute(17, 40) },
            { MakeMinute(17, 50), MakeMinute(19, 10) },
            { MakeMinute(19, 20), MakeMinute(20, 40) }
        };

        return monday[slot];
    }

    if (day_of_week >= 2 && day_of_week <= 5) {
        static const TimeInterval weekday[SLOTS_PER_DAY] = {
            { MakeMinute(8, 30),  MakeMinute(9, 55) },
            { MakeMinute(10, 5),  MakeMinute(11, 30) },
            { MakeMinute(12, 25), MakeMinute(13, 50) },
            { MakeMinute(14, 0),  MakeMinute(15, 25) },
            { MakeMinute(15, 35), MakeMinute(16, 55) },
            { MakeMinute(17, 5),  MakeMinute(18, 25) },
            { MakeMinute(18, 35), MakeMinute(19, 55) }
        };

        return weekday[slot];
    }

    if (day_of_week == 6) {
        static const TimeInterval saturday[SLOTS_PER_DAY] = {
            { MakeMinute(8, 30),  MakeMinute(9, 45) },
            { MakeMinute(9, 55),  MakeMinute(11, 10) },
            { MakeMinute(11, 30), MakeMinute(12, 45) },
            { MakeMinute(12, 55), MakeMinute(14, 10) },
            { MakeMinute(14, 20), MakeMinute(15, 30) },
            { MakeMinute(15, 40), MakeMinute(16, 50) },
            { MakeMinute(17, 0),  MakeMinute(18, 10) }
        };

        return saturday[slot];
    }

    return { 0, 0 };
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
            return { MakeMinute(9, 0), MakeMinute(13, 0) };
        }

        return { MakeMinute(13, 30), MakeMinute(17, 30) };
    }

    if (day_of_week >= 2 && day_of_week <= 5) {
        if (morning) {
            return { MakeMinute(8, 30), MakeMinute(12, 30) };
        }

        return { MakeMinute(13, 0), MakeMinute(17, 0) };
    }

    if (day_of_week == 6) {
        if (morning) {
            return { MakeMinute(8, 30), MakeMinute(12, 0) };
        }

        return { MakeMinute(12, 30), MakeMinute(16, 30) };
    }

    return { 0, 0 };
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

// ====================== Метки УП для вывода ======================

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

// ====================== Строки / вывод ======================

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
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    out.write(reinterpret_cast<const char*>(bom), 3);
}

std::string CsvEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 2);

    result += "\"";

    for (char ch : s) {
        if (ch == '"') {
            result += "\"\"";
        }
        else {
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

// ====================== Вспомогательные ограничения ======================

BoolVar MakePositiveIndicator(CpModelBuilder& model, const LinearExpr& sum) {
    BoolVar has = model.NewBoolVar();

    model.AddGreaterThan(sum, 0).OnlyEnforceIf(has);
    model.AddEquality(sum, 0).OnlyEnforceIf(has.Not());

    return has;
}

void AddMinIfPositive(
    CpModelBuilder& model,
    const LinearExpr& day_sum,
    const BoolVar& has,
    int minimum_value
) {
    LinearExpr rhs;
    rhs += has * minimum_value;

    model.AddGreaterOrEqual(day_sum, rhs);
}

void AddMin2IfPositive(CpModelBuilder& model, const LinearExpr& day_sum) {
    BoolVar has = MakePositiveIndicator(model, day_sum);

    LinearExpr rhs;
    rhs += has;
    rhs += has;

    model.AddGreaterOrEqual(day_sum, rhs);
}

void AddNoWindowsHard(
    CpModelBuilder& model,
    const std::vector<std::vector<BoolVar>>& busy_entities,
    int num_days
) {
    for (const auto& busy : busy_entities) {
        for (int d = 0; d < num_days; d++) {
            int base = d * SLOTS_PER_DAY;

            for (int left = 0; left < SLOTS_PER_DAY; left++) {
                for (int right = left + 2; right < SLOTS_PER_DAY; right++) {
                    for (int mid = left + 1; mid < right; mid++) {
                        LinearExpr expr;
                        expr += busy[base + left];
                        expr += busy[base + right];
                        expr -= busy[base + mid];

                        model.AddLessOrEqual(expr, 1);
                    }
                }
            }
        }
    }
}

std::vector<BoolVar> CreateWindowPenaltyVars(
    CpModelBuilder& model,
    const std::vector<std::vector<BoolVar>>& busy_entities,
    int num_days
) {
    std::vector<BoolVar> gaps;

    for (const auto& busy : busy_entities) {
        for (int d = 0; d < num_days; d++) {
            int base = d * SLOTS_PER_DAY;

            for (int s = 1; s < SLOTS_PER_DAY - 1; s++) {
                LinearExpr before_sum;
                LinearExpr after_sum;

                for (int q = 0; q < s; q++) {
                    before_sum += busy[base + q];
                }

                for (int q = s + 1; q < SLOTS_PER_DAY; q++) {
                    after_sum += busy[base + q];
                }

                BoolVar before = MakePositiveIndicator(model, before_sum);
                BoolVar after = MakePositiveIndicator(model, after_sum);
                BoolVar gap = model.NewBoolVar();

                model.AddImplication(gap, before);
                model.AddImplication(gap, after);
                model.AddImplication(gap, busy[base + s].Not());
                model.AddBoolOr({
                    before.Not(),
                    after.Not(),
                    busy[base + s],
                    gap
                    });

                gaps.push_back(gap);
            }
        }
    }

    return gaps;
}

void AddSubjectSpreadPenalties(
    CpModelBuilder& model,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    LinearExpr& objective
) {
    for (int g = 0; g < GROUPS; g++) {
        std::vector<std::vector<int>> buckets =
            BuildAvailableDayBuckets(g, all_days, unavailable);

        int bucket_count = static_cast<int>(buckets.size());
        if (bucket_count <= 1) {
            continue;
        }

        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            std::map<std::string, std::vector<int>> stream_lessons;
            std::map<std::string, int> stream_total_slots;

            for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
                if (!LessonAffectsPart(lessons[l], g, p)) {
                    continue;
                }

                std::string key = SubjectFamilyKey(lessons[l]);
                stream_lessons[key].push_back(l);
                stream_total_slots[key] += lessons[l].total_slots;
            }

            for (const auto& item : stream_lessons) {
                const std::vector<int>& lesson_ids = item.second;
                int total = stream_total_slots[item.first];

                if (total < MIN_SUBJECT_SPREAD_TOTAL_SLOTS) {
                    continue;
                }

                bool all_lessons_are_whole_group = true;
                bool all_lessons_are_blocks = true;

                for (int l : lesson_ids) {
                    if (lessons[l].subgroup != -1) {
                        all_lessons_are_whole_group = false;
                    }

                    if (!lessons[l].is_block) {
                        all_lessons_are_blocks = false;
                    }
                }

                if (all_lessons_are_whole_group && p > 0) {
                    continue;
                }

                int ideal_per_bucket = CeilDiv(total, bucket_count);
                int soft_max_per_bucket = std::max(
                    SUBJECT_BUCKET_MIN_CAPACITY,
                    ideal_per_bucket + SUBJECT_BUCKET_EXTRA_SLOTS
                );

                int active_unit = all_lessons_are_blocks
                    ? BLOCK_SUBJECT_ACTIVE_BUCKET_UNIT
                    : NORMAL_SUBJECT_ACTIVE_BUCKET_UNIT;

                int target_active_buckets = std::min(
                    bucket_count,
                    CeilDiv(total, active_unit)
                );

                std::vector<BoolVar> bucket_has;

                for (int b = 0; b < bucket_count; b++) {
                    LinearExpr bucket_sum;

                    for (int l : lesson_ids) {
                        for (int d : buckets[b]) {
                            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                                int t = d * SLOTS_PER_DAY + s;
                                bucket_sum += x[l][t];
                            }
                        }
                    }

                    bucket_has.push_back(MakePositiveIndicator(model, bucket_sum));

                    IntVar overload = model.NewIntVar(Domain(0, total));
                    LinearExpr overload_guard;
                    overload_guard += overload;
                    overload_guard -= bucket_sum;
                    model.AddGreaterOrEqual(overload_guard, -soft_max_per_bucket);
                    objective += overload * SUBJECT_BUCKET_OVERLOAD_WEIGHT;
                }

                LinearExpr active_bucket_sum;
                for (const auto& has : bucket_has) {
                    active_bucket_sum += has;
                }

                IntVar missing_buckets = model.NewIntVar(Domain(0, target_active_buckets));
                LinearExpr active_with_missing;
                active_with_missing += active_bucket_sum;
                active_with_missing += missing_buckets;
                model.AddGreaterOrEqual(active_with_missing, target_active_buckets);
                objective += missing_buckets * SUBJECT_MISSING_BUCKET_WEIGHT;

                if (bucket_count >= 3 && target_active_buckets >= 2) {
                    LinearExpr segment_has[3];

                    for (int b = 0; b < bucket_count; b++) {
                        int segment = (b * 3) / bucket_count;
                        if (segment > 2) {
                            segment = 2;
                        }

                        segment_has[segment] += bucket_has[b];
                    }

                    int required_segments = std::min(
                        3,
                        CeilDiv(total, active_unit)
                    );

                    if (required_segments >= 2) {
                        IntVar missing_edges = model.NewIntVar(Domain(0, 2));
                        LinearExpr edges_with_missing;
                        edges_with_missing += segment_has[0];
                        edges_with_missing += segment_has[2];
                        edges_with_missing += missing_edges;
                        model.AddGreaterOrEqual(edges_with_missing, 2);
                        objective += missing_edges * SUBJECT_MISSING_SEGMENT_WEIGHT;
                    }

                    if (required_segments >= 3) {
                        IntVar missing_middle = model.NewIntVar(Domain(0, 1));
                        LinearExpr middle_with_missing;
                        middle_with_missing += segment_has[1];
                        middle_with_missing += missing_middle;
                        model.AddGreaterOrEqual(middle_with_missing, 1);
                        objective += missing_middle * SUBJECT_MISSING_SEGMENT_WEIGHT;
                    }
                }
            }
        }
    }
}

int CountWindows(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& busy_entities,
    int num_days
) {
    int windows = 0;

    for (const auto& busy : busy_entities) {
        for (int d = 0; d < num_days; d++) {
            int base = d * SLOTS_PER_DAY;

            for (int s = 1; s < SLOTS_PER_DAY - 1; s++) {
                bool before = false;
                bool after = false;

                for (int q = 0; q < s; q++) {
                    if (BoolValue(response, busy[base + q])) {
                        before = true;
                        break;
                    }
                }

                for (int q = s + 1; q < SLOTS_PER_DAY; q++) {
                    if (BoolValue(response, busy[base + q])) {
                        after = true;
                        break;
                    }
                }

                bool current = BoolValue(response, busy[base + s]);

                if (before && after && !current) {
                    windows++;
                }
            }
        }
    }

    return windows;
}

int CountFivePairStudentDays(
    const CpSolverResponse& response,
    const std::vector<std::vector<std::vector<BoolVar>>>& part_busy,
    int num_days
) {
    int result = 0;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                int day_sum = 0;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    if (BoolValue(response, part_busy[g][p][t])) {
                        day_sum++;
                    }
                }

                if (day_sum == MAX_STUDENT_PAIRS_PER_DAY) {
                    result++;
                }
            }
        }
    }

    return result;
}

int MaxStudentPairsInDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<std::vector<BoolVar>>>& part_busy,
    int num_days
) {
    int result = 0;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                int day_sum = 0;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    if (BoolValue(response, part_busy[g][p][t])) {
                        day_sum++;
                    }
                }

                result = std::max(result, day_sum);
            }
        }
    }

    return result;
}

int CountUpDayRuleViolations(
    const CpSolverResponse& response,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<std::vector<BoolVar>>>& part_busy,
    int num_days
) {
    int violations = 0;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                int up_slots = 0;
                int part_slots = 0;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;

                    if (BoolValue(response, part_busy[g][p][t])) {
                        part_slots++;
                    }

                    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
                        if (!lessons[l].is_block) {
                            continue;
                        }

                        if (!LessonAffectsPart(lessons[l], g, p)) {
                            continue;
                        }

                        if (BoolValue(response, x[l][t])) {
                            up_slots++;
                        }
                    }
                }

                if (up_slots > 0 && (up_slots != 2 || part_slots != 2)) {
                    violations++;
                }
            }
        }
    }

    return violations;
}

int CountUpTeacherLockViolations(
    const CpSolverResponse& response,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<Date>& all_days
) {
    int violations = 0;

    struct SelectedUp {
        int lesson_id;
        TimeInterval interval;
    };

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            std::vector<SelectedUp> selected_up;

            for (const auto& blk : blocks) {
                int up_lesson_id = blk.lesson_id;

                if (lessons[up_lesson_id].teacher != teacher) {
                    continue;
                }

                for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                    if (!BoolValue(response, blk.start_vars[i])) {
                        continue;
                    }

                    int start_t = blk.possible_starts[i];
                    int day = start_t / SLOTS_PER_DAY;

                    if (day != d) {
                        continue;
                    }

                    selected_up.push_back({
                        up_lesson_id,
                        UpIntervalForStartSlot(all_days[day], start_t % SLOTS_PER_DAY)
                        });
                }
            }

            for (int i = 0; i < static_cast<int>(selected_up.size()); i++) {
                for (int j = i + 1; j < static_cast<int>(selected_up.size()); j++) {
                    if (IntervalsOverlap(selected_up[i].interval, selected_up[j].interval)) {
                        violations++;
                    }
                }
            }

            for (const auto& up : selected_up) {
                for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
                    if (lessons[l].is_block) {
                        continue;
                    }

                    if (lessons[l].teacher != teacher) {
                        continue;
                    }

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        int t = d * SLOTS_PER_DAY + s;

                        if (!BoolValue(response, x[l][t])) {
                            continue;
                        }

                        TimeInterval pair_interval = PairSlotInterval(DayOfWeek(all_days[d]), s);
                        if (IntervalsOverlap(up.interval, pair_interval)) {
                            violations++;
                        }
                    }
                }
            }
        }
    }

    return violations;
}

// ====================== Диагностика ======================

bool ValidateInputLessons(const std::vector<Lesson>& lessons) {
    bool ok = true;
    std::set<int> seen_ids;

    for (const auto& lesson : lessons) {
        if (!seen_ids.insert(lesson.id).second) {
            std::cerr << "Повторяется id занятия: " << lesson.id << "\n";
            ok = false;
        }

        if (lesson.group < 0 || lesson.group >= GROUPS) {
            std::cerr << "Некорректная группа у занятия: "
                << lesson.name << ", group=" << lesson.group << "\n";
            ok = false;
        }

        if (lesson.teacher < 0 || lesson.teacher >= TEACHERS) {
            std::cerr << "Некорректный преподаватель у занятия: "
                << lesson.name << ", teacher=" << lesson.teacher << "\n";
            ok = false;
        }

        if (lesson.total_slots <= 0) {
            std::cerr << "Некорректное число пар у занятия: "
                << lesson.name << ", total_slots=" << lesson.total_slots << "\n";
            ok = false;
        }

        if (lesson.group >= 0 && lesson.group < GROUPS && lesson.subgroup != -1) {
            int base_subgroup = lesson.group * PARTS_PER_GROUP;
            int last_subgroup = base_subgroup + PARTS_PER_GROUP - 1;

            if (lesson.subgroup < base_subgroup || lesson.subgroup > last_subgroup) {
                std::cerr << "Некорректная подгруппа у занятия: "
                    << lesson.name
                    << ", group=" << GROUP_NAME[lesson.group]
                    << ", subgroup=" << lesson.subgroup
                    << ", ожидается -1 или диапазон "
                    << base_subgroup << ".." << last_subgroup << "\n";
                ok = false;
            }
        }

        if (lesson.allowed_campuses.empty()) {
            std::cerr << "У занятия нет разрешённых кампусов: "
                << lesson.name << "\n";
            ok = false;
        }

        for (Campus campus : lesson.allowed_campuses) {
            if (campus != LESNAYA && campus != KRIVOUSOVA) {
                std::cerr << "Некорректный кампус у занятия: "
                    << lesson.name << "\n";
                ok = false;
            }
        }

        if (lesson.is_block) {
            if (lesson.total_slots % 2 != 0) {
                std::cerr << "Блоковое занятие должно иметь чётное число пар: "
                    << lesson.name
                    << ", группа " << GROUP_NAME[lesson.group]
                    << ", total_slots=" << lesson.total_slots << "\n";
                ok = false;
            }

            if (lesson.total_slots < 2) {
                std::cerr << "Блоковое занятие должно иметь минимум 2 пары: "
                    << lesson.name
                    << ", группа " << GROUP_NAME[lesson.group]
                    << "\n";
                ok = false;
            }
        }
    }

    return ok;
}

void PrintInputDiagnostics(
    const std::vector<Lesson>& lessons,
    const std::vector<Date>& all_days,
    const std::map<int, std::vector<std::pair<Date, Date>>>& unavailable,
    const Date& start_date
) {
    std::vector<int> group_load(GROUPS, 0);
    std::vector<int> teacher_load(TEACHERS, 0);

    int block_lessons = 0;
    int required_double_blocks = 0;

    for (const auto& lesson : lessons) {
        group_load[lesson.group] += lesson.total_slots;
        teacher_load[lesson.teacher] += lesson.total_slots;

        if (lesson.is_block) {
            block_lessons++;
            required_double_blocks += lesson.total_slots / 2;
        }
    }

    std::cout << "\n========== Диагностика входных данных ==========\n";
    std::cout << "Учебных дней: " << all_days.size() << "\n";
    std::cout << "Всего строк предметов после агрегации: " << lessons.size() << "\n";
    std::cout << "Агрегированных строк УП: " << block_lessons << "\n";
    std::cout << "Требуется двойных блоков УП: " << required_double_blocks << "\n\n";

    for (int g = 0; g < GROUPS; g++) {
        int available_days = 0;
        std::set<int> available_weeks;

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                available_days++;
                available_weeks.insert(WeekIndexFromStart(start_date, all_days[d]));
            }
        }

        std::cout << "Группа " << GROUP_NAME[g]
            << ": нагрузка " << group_load[g]
            << " назначений, доступно "
            << available_days * SLOTS_PER_DAY
            << " временных слотов, доступных недель примерно "
            << available_weeks.size()
            << "\n";
    }

    std::cout << "\nНагрузка преподавателей:\n";

    for (int t = 0; t < TEACHERS; t++) {
        std::cout << "  " << TEACHER_NAME[t]
            << ": " << teacher_load[t]
            << " пар\n";
    }

    std::cout << "================================================\n\n";
}

// ====================== Формирование текста расписания ======================

std::string BuildGroupSlotText(
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    int group,
    int day,
    int slot
) {
    int t = day * SLOTS_PER_DAY + slot;
    std::vector<std::string> items;

    int campus = IntValue(response, group_day_campus[group][day]);

    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].group != group) continue;

        if (BoolValue(response, x[l][t])) {
            std::ostringstream ss;
            std::string lesson_name = lessons[l].name;

            if (lessons[l].is_block) {
                std::string up_label = UpShiftLabelForDisplaySlot(all_days[day], slot);
                if (!up_label.empty()) {
                    lesson_name += " (" + up_label + ")";
                }
            }

            ss << lesson_name
                << " — " << SubgroupName(lessons[l].subgroup)
                << ", " << TEACHER_NAME[lessons[l].teacher]
                << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    if (items.empty()) {
        return "-";
    }

    return Join(items, " | ");
}

std::string BuildTeacherSlotText(
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<IntVar>>& teacher_day_campus,
    int teacher,
    int day,
    int slot
) {
    int t = day * SLOTS_PER_DAY + slot;
    std::vector<std::string> items;

    int campus = IntValue(response, teacher_day_campus[teacher][day]);
    TimeInterval pair_interval = PairSlotInterval(DayOfWeek(all_days[day]), slot);

    for (int l = 0; l < static_cast<int>(lessons.size()); l++) {
        if (lessons[l].teacher != teacher) continue;
        if (lessons[l].is_block) continue;

        if (BoolValue(response, x[l][t])) {
            std::ostringstream ss;

            ss << GROUP_NAME[lessons[l].group]
                << ", " << lessons[l].name
                << " — " << SubgroupName(lessons[l].subgroup)
                << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    for (const auto& blk : blocks) {
        const Lesson& lesson = lessons[blk.lesson_id];

        if (lesson.teacher != teacher) {
            continue;
        }

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            if (!BoolValue(response, blk.start_vars[i])) {
                continue;
            }

            int start_t = blk.possible_starts[i];
            int start_day = start_t / SLOTS_PER_DAY;
            int start_slot = start_t % SLOTS_PER_DAY;

            if (start_day != day) {
                continue;
            }

            TimeInterval up_interval = UpIntervalForStartSlot(all_days[day], start_slot);

            if (!IntervalsOverlap(up_interval, pair_interval)) {
                continue;
            }

            std::ostringstream ss;
            ss << GROUP_NAME[lesson.group]
                << ", " << lesson.name
                << " (" << UpIntervalLabelForStartSlot(all_days[day], start_slot) << ")"
                << " — " << SubgroupName(lesson.subgroup)
                << ", " << CampusName(campus);

            items.push_back(ss.str());
        }
    }

    if (items.empty()) {
        return "-";
    }

    return Join(items, " | ");
}

bool HasGroupDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& group_busy,
    int group,
    int day
) {
    for (int s = 0; s < SLOTS_PER_DAY; s++) {
        int t = day * SLOTS_PER_DAY + s;

        if (BoolValue(response, group_busy[group][t])) {
            return true;
        }
    }

    return false;
}

bool HasTeacherDay(
    const CpSolverResponse& response,
    const std::vector<std::vector<BoolVar>>& teacher_busy,
    int teacher,
    int day
) {
    for (int s = 0; s < SLOTS_PER_DAY; s++) {
        int t = day * SLOTS_PER_DAY + s;

        if (BoolValue(response, teacher_busy[teacher][t])) {
            return true;
        }
    }

    return false;
}

// ====================== Запись файлов ======================

void WriteGroupScheduleTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus,
    int group
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Расписание группы " << GROUP_NAME[group] << "\n";
    out << "========================================\n\n";

    for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
        if (!HasGroupDay(response, group_busy, group, d)) {
            continue;
        }

        const Date& dt = all_days[d];

        out << DateToString(dt)
            << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

        for (int s = 0; s < SLOTS_PER_DAY; s++) {
            out << "  " << PairSlotLabel(dt, s) << ": "
                << BuildGroupSlotText(
                    response,
                    all_days,
                    lessons,
                    x,
                    group_day_campus,
                    group,
                    d,
                    s
                )
                << "\n";
        }

        out << "\n";
    }
}

void WriteAllGroupsTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Общее расписание групп\n";
    out << "======================\n\n";

    for (int g = 0; g < GROUPS; g++) {
        out << "\n\n========== " << GROUP_NAME[g] << " ==========\n\n";

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasGroupDay(response, group_busy, g, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            out << DateToString(dt)
                << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                out << "  " << PairSlotLabel(dt, s) << ": "
                    << BuildGroupSlotText(
                        response,
                        all_days,
                        lessons,
                        x,
                        group_day_campus,
                        g,
                        d,
                        s
                    )
                    << "\n";
            }

            out << "\n";
        }
    }
}

void WriteGroupsCsv(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<std::vector<BoolVar>>& group_busy,
    const std::vector<std::vector<IntVar>>& group_day_campus
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << CsvEscape("Группа") << ";"
        << CsvEscape("Дата") << ";"
        << CsvEscape("День") << ";"
        << CsvEscape("Пара") << ";"
        << CsvEscape("Занятия") << "\n";

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasGroupDay(response, group_busy, g, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                std::string text = BuildGroupSlotText(
                    response,
                    all_days,
                    lessons,
                    x,
                    group_day_campus,
                    g,
                    d,
                    s
                );

                out << CsvEscape(GROUP_NAME[g]) << ";"
                    << CsvEscape(DateToString(dt)) << ";"
                    << CsvEscape(WEEKDAY_NAME[DayOfWeek(dt) - 1]) << ";"
                    << CsvEscape(PairSlotLabel(dt, s)) << ";"
                    << CsvEscape(text) << "\n";
            }
        }
    }
}

void WriteTeachersTxt(
    const std::string& file_name,
    const CpSolverResponse& response,
    const std::vector<Date>& all_days,
    const std::vector<Lesson>& lessons,
    const std::vector<std::vector<BoolVar>>& x,
    const std::vector<BlockInfo>& blocks,
    const std::vector<std::vector<BoolVar>>& teacher_busy,
    const std::vector<std::vector<IntVar>>& teacher_day_campus
) {
    std::ofstream out(file_name, std::ios::binary);
    if (!out) {
        std::cerr << "Не удалось открыть файл: " << file_name << "\n";
        return;
    }

    WriteUtf8Bom(out);

    out << "Расписание преподавателей\n";
    out << "=========================\n\n";

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        out << "\n\n========== " << TEACHER_NAME[teacher] << " ==========\n\n";

        for (int d = 0; d < static_cast<int>(all_days.size()); d++) {
            if (!HasTeacherDay(response, teacher_busy, teacher, d)) {
                continue;
            }

            const Date& dt = all_days[d];

            out << DateToString(dt)
                << " (" << WEEKDAY_NAME[DayOfWeek(dt) - 1] << ")\n";

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                out << "  " << PairSlotLabel(dt, s) << ": "
                    << BuildTeacherSlotText(
                        response,
                        all_days,
                        lessons,
                        x,
                        blocks,
                        teacher_day_campus,
                        teacher,
                        d,
                        s
                    )
                    << "\n";
            }

            out << "\n";
        }
    }
}


// ====================== Жадная фиксация УП ======================

std::vector<int> PartsAffectedByLesson(const Lesson& lesson) {
    std::vector<int> parts;

    if (lesson.subgroup == -1) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            parts.push_back(p);
        }
        return parts;
    }

    int base_subgroup = lesson.group * PARTS_PER_GROUP;
    int part = lesson.subgroup - base_subgroup;

    if (part >= 0 && part < PARTS_PER_GROUP) {
        parts.push_back(part);
    }

    return parts;
}

std::vector<std::vector<int>> SelectGreedyUpStarts(
    const std::vector<Lesson>& lessons,
    const std::vector<BlockInfo>& blocks,
    const std::vector<Date>& all_days,
    int num_days,
    bool& ok
) {
    ok = true;

    std::vector<std::vector<int>> selected(blocks.size());
    std::vector<std::set<int>> selected_lookup(blocks.size());

    std::vector<std::vector<std::vector<bool>>> part_has_up_day(
        GROUPS,
        std::vector<std::vector<bool>>(
            PARTS_PER_GROUP,
            std::vector<bool>(num_days, false)
        )
    );

    std::vector<std::vector<std::vector<TimeInterval>>> teacher_up_intervals(
        TEACHERS,
        std::vector<std::vector<TimeInterval>>(num_days)
    );

    std::vector<std::vector<int>> teacher_day_up_count(
        TEACHERS,
        std::vector<int>(num_days, 0)
    );

    std::vector<std::vector<int>> group_day_up_count(
        GROUPS,
        std::vector<int>(num_days, 0)
    );

    std::vector<int> global_day_up_count(num_days, 0);

    std::vector<int> order(blocks.size());
    for (int i = 0; i < static_cast<int>(blocks.size()); i++) {
        order[i] = i;
    }

    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Lesson& la = lessons[blocks[a].lesson_id];
        const Lesson& lb = lessons[blocks[b].lesson_id];

        int ra = la.total_slots / 2;
        int rb = lb.total_slots / 2;

        if (ra != rb) return ra > rb;
        if (la.teacher != lb.teacher) return la.teacher < lb.teacher;
        if (la.group != lb.group) return la.group < lb.group;
        return la.id < lb.id;
        });

    for (int block_index : order) {
        const BlockInfo& blk = blocks[block_index];
        const Lesson& lesson = lessons[blk.lesson_id];
        int required_starts = lesson.total_slots / 2;
        std::vector<int> affected_parts = PartsAffectedByLesson(lesson);

        for (int need = 0; need < required_starts; need++) {
            int best_i = -1;
            int best_score = 0;

            for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                if (selected_lookup[block_index].count(i) != 0) {
                    continue;
                }

                int start_t = blk.possible_starts[i];
                int day = start_t / SLOTS_PER_DAY;
                int start_slot = start_t % SLOTS_PER_DAY;

                bool can = true;

                for (int part : affected_parts) {
                    if (part_has_up_day[lesson.group][part][day]) {
                        can = false;
                        break;
                    }
                }

                if (!can) {
                    continue;
                }

                TimeInterval interval = UpIntervalForStartSlot(all_days[day], start_slot);

                for (const TimeInterval& other : teacher_up_intervals[lesson.teacher][day]) {
                    if (IntervalsOverlap(interval, other)) {
                        can = false;
                        break;
                    }
                }

                if (!can) {
                    continue;
                }

                // Чем меньше счёт, тем лучше.
                // 1) не перегружаем один день конкретного преподавателя УП;
                // 2) не сваливаем все УП одной группы в один день;
                // 3) распределяем УП по семестру;
                // 4) слегка предпочитаем утро, чтобы дневные блоки оставались резервом.
                int score = 0;
                score += teacher_day_up_count[lesson.teacher][day] * 100000;
                score += group_day_up_count[lesson.group][day] * 10000;
                score += global_day_up_count[day] * 100;
                score += start_slot == UP_AFTERNOON_MODEL_START_SLOT ? 10 : 0;

                // Мягкое равномерное распределение по календарю.
                // Разные блоки одного предмета стараемся разводить, но без жёсткого запрета.
                int bucket = day / 6;
                int same_bucket = 0;
                for (int already_i : selected[block_index]) {
                    int already_day = blk.possible_starts[already_i] / SLOTS_PER_DAY;
                    if (already_day / 6 == bucket) {
                        same_bucket++;
                    }
                }
                score += same_bucket * 1000;

                if (best_i < 0 || score < best_score) {
                    best_i = i;
                    best_score = score;
                }
            }

            if (best_i < 0) {
                std::cerr << "Не удалось жадно поставить УП: "
                    << GROUP_NAME[lesson.group] << ", "
                    << lesson.name << ", "
                    << SubgroupName(lesson.subgroup) << ", преподаватель "
                    << TEACHER_NAME[lesson.teacher] << ", нужно блоков "
                    << required_starts << ", поставлено "
                    << selected[block_index].size() << "\n";
                ok = false;
                continue;
            }

            selected[block_index].push_back(best_i);
            selected_lookup[block_index].insert(best_i);

            int start_t = blk.possible_starts[best_i];
            int day = start_t / SLOTS_PER_DAY;
            int start_slot = start_t % SLOTS_PER_DAY;
            TimeInterval interval = UpIntervalForStartSlot(all_days[day], start_slot);

            for (int part : affected_parts) {
                part_has_up_day[lesson.group][part][day] = true;
            }

            teacher_up_intervals[lesson.teacher][day].push_back(interval);
            teacher_day_up_count[lesson.teacher][day]++;
            group_day_up_count[lesson.group][day]++;
            global_day_up_count[day]++;
        }
    }

    int fixed_blocks = 0;
    for (const auto& item : selected) {
        fixed_blocks += static_cast<int>(item.size());
    }

    std::cout << "Жадно зафиксировано УП-блоков: " << fixed_blocks << "\n";

    return selected;
}

// ====================== Основная программа ======================

int main() {
    std::setlocale(LC_ALL, "ru_RU.UTF-8");

    Date start_date = { 2026, 1, 12 };
    Date end_date = { 2026, 5, 30 };

    std::map<int, std::vector<std::pair<Date, Date>>> unavailable;
    unavailable[G_ISP_3304] = { {{2026, 4, 30}, {2026, 6, 19}} };
    unavailable[G_ISP_3305P] = { {{2026, 4, 30}, {2026, 6, 19}} };
    // unavailable[G_ISP_3305P] = { {{2026, 3, 20}, {2026, 3, 30}} }; // сборы

    auto all_days = GenerateSchoolDays(start_date, end_date);

    int num_days = static_cast<int>(all_days.size());
    int total_slots = num_days * SLOTS_PER_DAY;

    // ---------------------- Создание уроков ----------------------

    std::vector<Lesson> lessons;
    int id = 0;
    int subj = 0;

    auto add = [&](int group,
        int sub,
        int teacher,
        int slots,
        const std::string& name,
        int subject_id,
        bool is_lab,
        bool is_block,
        std::set<Campus> camps) {
            if (!GROUP_ENABLED[group]) {
                return;
            }

            lessons.push_back({
                id++,
                group,
                sub,
                teacher,
                slots,
                name,
                subject_id,
                is_lab,
                is_block,
                camps
                });
        };

    const std::set<Campus> ANY = { LESNAYA, KRIVOUSOVA };
    const std::set<Campus> LES = { LESNAYA };

    auto addHours = [&](int group,
        int sub,
        int teacher,
        int hours,
        const std::string& name,
        int subject_id,
        bool is_lab,
        bool is_up,
        std::set<Campus> camps) {
            int slots = 0;
            bool is_block = false;

            if (is_up) {
                // В таблице УП дано в академических часах.
                // Один фактический блок УП длится 6 академических часов
                // и в расписании показывается как 2 соседние пары.
                // Поэтому 18 часов УП = 3 блока = 6 отображаемых пар.
                if (hours % 6 != 0) {
                    std::cerr << "Некорректные часы УП: ожидается кратность 6: "
                        << GROUP_NAME[group] << ", " << name
                        << ", часов=" << hours << "\n";
                }

                int up_blocks = hours / 6;
                slots = up_blocks * 2;
                is_block = true;
            }
            else {
                if (hours % 2 != 0) {
                    std::cerr << "Предупреждение: нечётное количество часов, деление на 2 отбросит остаток: "
                        << GROUP_NAME[group] << ", " << name
                        << ", часов=" << hours << "\n";
                }

                slots = hours / 2;
            }

            add(group, sub, teacher, slots, name, subject_id, is_lab, is_block, camps);
        };

    // ====================== Старые группы ИСП ======================

    // Иностранный язык — только Лесная
    int engId = subj++;

    for (int g = G_ISP_3304; g <= G_ISP_3305P; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, bs, T_NOVOSELOVA, 13, "Ин. язык", engId, false, false, { LESNAYA });
        add(g, bs + 1, T_DAVYDOVA, 13, "Ин. язык", engId, false, false, { LESNAYA });
    }

    // Физкультура
    for (int g = G_ISP_3304; g <= G_ISP_3305P; g++) {
        add(g, -1, T_NUROV, 14, "Физическая культура", -1, false, false, { LESNAYA, KRIVOUSOVA });
    }

    // БЖД
    for (int g = G_ISP_3304; g <= G_ISP_3305P; g++) {
        add(g, -1, T_POTAPOVA, 17, "БЖД", -1, false, false, { LESNAYA, KRIVOUSOVA });
    }

    // Экономика
    add(G_ISP_3304, -1, T_SERYANINA, 12, "Экономика", -1, false, false, { LESNAYA, KRIVOUSOVA });
    add(G_ISP_3305P, -1, T_GARBUZOV, 12, "Экономика", -1, false, false, { LESNAYA, KRIVOUSOVA });

    // МДК.01.01 Разработка программных модулей
    int pmId = subj++;

    for (int g = G_ISP_3304; g <= G_ISP_3305P; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_GARBUZOV, 8, "МДК.01.01 теория", pmId, false, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, -1, T_GARBUZOV, 15, "МДК.01.01 КП", -1, false, false, { LESNAYA, KRIVOUSOVA });
    }

    // МДК.04.01 Технология разработки и защиты БД
    int dbId = subj++;

    for (int g = G_ISP_3304; g <= G_ISP_3305P; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_SAMTSOVA, 36, "МДК.04.01 теория", dbId, false, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, { LESNAYA, KRIVOUSOVA });

        add(g, bs, T_SAMTSOVA, 36, "УП.04", -1, false, true, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_SAMTSOVA, 36, "УП.04", -1, false, true, { LESNAYA, KRIVOUSOVA });
    }

    // ВМДК.05.01 Управление и автоматизация БД
    int autoId = subj++;

    for (int g = G_ISP_3304; g <= G_ISP_3305P; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_GARBUZOV, 16, "ВМДК.05.01 теория", autoId, false, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, { LESNAYA, KRIVOUSOVA });

        add(g, bs, T_GOBOV, 18, "УП.05", -1, false, true, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GOBOV, 18, "УП.05", -1, false, true, { LESNAYA, KRIVOUSOVA });
    }

    // ====================== Группы 2 курса ======================

    {
        int g = G_TAKHCS_2201;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_TSIMFER, 44, "История", -1, false, false, ANY);
        addHours(g, bs, T_SINELNIKOVA, 40, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_SINELNIKOVA, 40, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_NUROV, 42, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_NIFONTOVA, 28, "Математика", -1, false, false, ANY);
        addHours(g, bs, T_AZARYAN, 46, "Информационные технологии в профессиональной деятельности / Адаптивные информационные и коммуникационные технологии", -1, false, false, ANY);
        addHours(g, bs + 1, T_AZARYAN, 46, "Информационные технологии в профессиональной деятельности / Адаптивные информационные и коммуникационные технологии", -1, false, false, ANY);
        addHours(g, -1, T_KALCHEVSKAYA, 60, "Органическая химия", -1, false, false, ANY);
        addHours(g, -1, T_SEMENOVA, 56, "Аналитическая химия", -1, false, false, ANY);
        addHours(g, -1, T_KALCHEVSKAYA, 56, "Физическая и коллоидная химия", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 40, "Электротехника и электроника", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_KALCHEVSKAYA, 6, "МДК.01.01 Основы аналитической химии и физико-химических методов анализа", sid, false, false, ANY);
        addHours(g, bs, T_KALCHEVSKAYA, 54, "ЛПЗ МДК.01.01 Основы аналитической химии и физико-химических методов анализа", sid, true, false, ANY);
        addHours(g, bs + 1, T_KALCHEVSKAYA, 54, "ЛПЗ МДК.01.01 Основы аналитической химии и физико-химических методов анализа", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SEMENOVA, 2, "МДК.02.01 Основы качественного и количественного анализа природных и промышленных материалов", sid, false, false, ANY);
        addHours(g, bs, T_SEMENOVA, 72, "ЛПЗ МДК.02.01 Основы качественного и количественного анализа природных и промышленных материалов", sid, true, false, ANY);
        addHours(g, bs + 1, T_SEMENOVA, 72, "ЛПЗ МДК.02.01 Основы качественного и количественного анализа природных и промышленных материалов", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_KALCHEVSKAYA, 24, "ВМДК.04.02 Химический анализ сырья, материалов и готовой продукции", sid, false, false, ANY);
        addHours(g, bs, T_KALCHEVSKAYA, 30, "ЛПЗ ВМДК.04.02 Химический анализ сырья, материалов и готовой продукции", sid, true, false, ANY);
        addHours(g, bs + 1, T_KALCHEVSKAYA, 30, "ЛПЗ ВМДК.04.02 Химический анализ сырья, материалов и готовой продукции", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_KALCHEVSKAYA, 70, "ВМДК.04.03 Основы приготовления проб и растворов различной концентрации", sid, false, false, ANY);
        addHours(g, bs, T_KALCHEVSKAYA, 46, "ЛПЗ ВМДК.04.03 Основы приготовления проб и растворов различной концентрации", sid, true, false, ANY);
        addHours(g, bs + 1, T_KALCHEVSKAYA, 46, "ЛПЗ ВМДК.04.03 Основы приготовления проб и растворов различной концентрации", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_ALSHAEVA, 24, "ВМДК.04.04 Основы экологического контроля производства и технологического процесса", sid, false, false, ANY);
        addHours(g, bs, T_ALSHAEVA, 16, "ЛПЗ ВМДК.04.04 Основы экологического контроля производства и технологического процесса", sid, true, false, ANY);
        addHours(g, bs + 1, T_ALSHAEVA, 16, "ЛПЗ ВМДК.04.04 Основы экологического контроля производства и технологического процесса", sid, true, false, ANY);

        addHours(g, bs, T_KALCHEVSKAYA, 72, "УП.04", -1, false, true, ANY);
        addHours(g, bs + 1, T_KALCHEVSKAYA, 72, "УП.04", -1, false, true, ANY);
    }

    {
        int g = G_MCM_201;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_KROPOTOVA, 18, "География", -1, false, false, ANY);
        addHours(g, -1, T_SEMENOVA, 18, "Химия", -1, false, false, ANY);
        addHours(g, -1, T_TSIMFER, 36, "История России", -1, false, false, ANY);
        addHours(g, bs, T_DAVYDOVA, 44, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_DAVYDOVA, 44, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_NUROV, 62, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_ELAGINA, 36, "Основы финансовой грамотности", -1, false, false, ANY);
        addHours(g, -1, T_POPOVA, 40, "Основы металлургического производства", -1, false, false, ANY);
        addHours(g, bs, T_GALUZIN, 38, "Информационные технологии в профессиональной деятельности", -1, false, false, ANY);
        addHours(g, bs + 1, T_GALUZIN, 38, "Информационные технологии в профессиональной деятельности", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_GARBUZOV, 10, "Компас 3D теория", sid, false, false, ANY);
        addHours(g, bs, T_GARBUZOV, 26, "ЛПЗ Компас 3D", sid, true, false, ANY);
        addHours(g, bs + 1, T_GARBUZOV, 26, "ЛПЗ Компас 3D", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_POPOVA, 24, "Пылеулавливание и очистка промышленных газов", sid, false, false, ANY);
        addHours(g, bs, T_POPOVA, 12, "ЛПЗ Пылеулавливание и очистка промышленных газов", sid, true, false, ANY);
        addHours(g, bs + 1, T_POPOVA, 12, "ЛПЗ Пылеулавливание и очистка промышленных газов", sid, true, false, ANY);

        addHours(g, -1, T_KOSTAREVA, 36, "Экологические основы природопользования", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 36, "Электрооборудование металлургических цехов", -1, false, false, ANY);
        addHours(g, -1, T_ALSHAEVA, 36, "Физическая химия", -1, false, false, ANY);
        addHours(g, -1, T_SADRIEVA, 54, "Метрология и стандартизация", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 36, "Электротехника и электроника", -1, false, false, ANY);
        addHours(g, -1, T_POPOVA, 48, "МДК.02.01 Металлургия цветных металлов", -1, false, false, ANY);
        addHours(g, -1, T_POPOVA, 60, "МДК.02.02 Технология производства цветных металлов и сплавов", -1, false, false, ANY);

        addHours(g, bs, T_POPOVA, 36, "УП.02", -1, false, true, ANY);
        addHours(g, bs + 1, T_POPOVA, 36, "УП.02", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_POPOVA, 44, "ВМДК.03.02 Технология производства цветных металлов и сплавов по типам производств", sid, false, false, ANY);
        addHours(g, bs, T_POPOVA, 12, "ЛПЗ ВМДК.03.02 Технология производства цветных металлов и сплавов по типам производств", sid, true, false, ANY);
        addHours(g, bs + 1, T_POPOVA, 12, "ЛПЗ ВМДК.03.02 Технология производства цветных металлов и сплавов по типам производств", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_PISMAK, 32, "ВМДК.03.03 Анодная медь", sid, false, false, ANY);
        addHours(g, bs, T_PISMAK, 10, "ЛПЗ ВМДК.03.03 Анодная медь", sid, true, false, ANY);
        addHours(g, bs + 1, T_PISMAK, 10, "ЛПЗ ВМДК.03.03 Анодная медь", sid, true, false, ANY);

        addHours(g, bs, T_PISMAK, 36, "УП.03", -1, false, true, ANY);
        addHours(g, bs + 1, T_PISMAK, 36, "УП.03", -1, false, true, ANY);
    }

    {
        int g = G_TEO_2501;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_TSIMFER, 48, "История России", -1, false, false, ANY);
        addHours(g, bs, T_SINELNIKOVA, 40, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_SINELNIKOVA, 40, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_POTAPOVA, 44, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_KOSHELEV, 40, "Инженерная графика", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 72, "Электротехника и электроника", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 60, "Электрические машины и электропривод", -1, false, false, ANY);
        addHours(g, bs, T_AZARYAN, 66, "Информационные технологии в профессиональной деятельности", -1, false, false, ANY);
        addHours(g, bs + 1, T_AZARYAN, 66, "Информационные технологии в профессиональной деятельности", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 72, "Электроснабжение", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SHABUROV, 18, "МДК.01.01 Технология ремонта, монтажа и наладки электрического и электромеханического оборудования", sid, false, false, ANY);
        addHours(g, bs, T_SHABUROV, 20, "ЛПЗ МДК.01.01 Технология ремонта, монтажа и наладки электрического и электромеханического оборудования", sid, true, false, ANY);
        addHours(g, bs + 1, T_SHABUROV, 20, "ЛПЗ МДК.01.01 Технология ремонта, монтажа и наладки электрического и электромеханического оборудования", sid, true, false, ANY);

        addHours(g, bs, T_KOSHELEV, 36, "УП.01", -1, false, true, ANY);
        addHours(g, bs + 1, T_KOSHELEV, 36, "УП.01", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_SHABUROV, 20, "МДК.03.01 Основы энергоснабжения объектов отрасли", sid, false, false, ANY);
        addHours(g, bs, T_SHABUROV, 32, "ЛПЗ МДК.03.01 Основы энергоснабжения объектов отрасли", sid, true, false, ANY);
        addHours(g, bs + 1, T_SHABUROV, 32, "ЛПЗ МДК.03.01 Основы энергоснабжения объектов отрасли", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SHABUROV, 40, "МДК.04.01 Выполнение монтажных работ, монтажное оборудование и контрольно-измерительные приборы", sid, false, false, ANY);
        addHours(g, bs, T_SHABUROV, 62, "ЛПЗ МДК.04.01 Выполнение монтажных работ, монтажное оборудование и контрольно-измерительные приборы", sid, true, false, ANY);
        addHours(g, bs + 1, T_SHABUROV, 62, "ЛПЗ МДК.04.01 Выполнение монтажных работ, монтажное оборудование и контрольно-измерительные приборы", sid, true, false, ANY);

        addHours(g, bs, T_KOSHELEV, 36, "УП.04", -1, false, true, ANY);
        addHours(g, bs + 1, T_KOSHELEV, 36, "УП.04", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_SHABUROV, 20, "ВМДК.05.01 Организация работ по ремонту и обслуживанию электрооборудования", sid, false, false, ANY);
        addHours(g, bs, T_SHABUROV, 30, "ЛПЗ ВМДК.05.01 Организация работ по ремонту и обслуживанию электрооборудования", sid, true, false, ANY);
        addHours(g, bs + 1, T_SHABUROV, 30, "ЛПЗ ВМДК.05.01 Организация работ по ремонту и обслуживанию электрооборудования", sid, true, false, ANY);

        addHours(g, bs, T_SHABUROV, 72, "УП.05", -1, false, true, ANY);
        addHours(g, bs + 1, T_SHABUROV, 72, "УП.05", -1, false, true, ANY);
    }

    {
        int g = G_SP_2601;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_KROPOTOVA, 18, "География", -1, false, false, ANY);
        addHours(g, -1, T_SEMENOVA, 18, "Химия", -1, false, false, ANY);
        addHours(g, -1, T_TSIMFER, 36, "История России", -1, false, false, ANY);
        addHours(g, bs, T_ERMOLINA, 40, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_ERMOLINA, 40, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_NUROV, 46, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_ELAGINA, 36, "Основы финансовой грамотности", -1, false, false, ANY);
        addHours(g, -1, T_ABRAMCHUK, 56, "Техническая механика", -1, false, false, ANY);
        addHours(g, -1, T_SIVILKAEV, 60, "Технологические процессы в машиностроении", -1, false, false, ANY);
        addHours(g, -1, T_AKHMETOV, 18, "Технология конструкционных материалов", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_PODCHINENNOV, 6, "Компас 3D", sid, false, false, ANY);
        addHours(g, bs, T_PODCHINENNOV, 30, "ЛПЗ Компас 3D", sid, true, false, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 30, "ЛПЗ Компас 3D", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 16, "МДК.01.01 Технология сварочных работ", sid, false, false, ANY);
        addHours(g, bs, T_GORIN, 30, "ЛПЗ МДК.01.01 Технология сварочных работ", sid, true, false, ANY);
        addHours(g, bs + 1, T_GORIN, 30, "ЛПЗ МДК.01.01 Технология сварочных работ", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 20, "МДК.01.02 Основное оборудование для производства сварных конструкций", sid, false, false, ANY);
        addHours(g, bs, T_AKHMETOV, 26, "ЛПЗ МДК.01.02 Основное оборудование для производства сварных конструкций", sid, true, false, ANY);
        addHours(g, bs + 1, T_AKHMETOV, 26, "ЛПЗ МДК.01.02 Основное оборудование для производства сварных конструкций", sid, true, false, ANY);

        addHours(g, bs, T_GORIN, 18, "УП.01", -1, false, true, ANY);
        addHours(g, bs + 1, T_GORIN, 18, "УП.01", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 20, "МДК.03.01 Формы и методы контроля качества металлов и сварных конструкций", sid, false, false, ANY);
        addHours(g, bs, T_AKHMETOV, 26, "ЛПЗ МДК.03.01 Формы и методы контроля качества металлов и сварных конструкций", sid, true, false, ANY);
        addHours(g, bs + 1, T_AKHMETOV, 26, "ЛПЗ МДК.03.01 Формы и методы контроля качества металлов и сварных конструкций", sid, true, false, ANY);

        addHours(g, bs, T_VERHNEV, 36, "УП.03", -1, false, true, ANY);
        addHours(g, bs + 1, T_VERHNEV, 36, "УП.03", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_LIMONOVA, 26, "МДК.05.01 Электросварочные работы на автоматических и полуавтоматических машинах", sid, false, false, ANY);
        addHours(g, bs, T_LIMONOVA, 20, "ЛПЗ МДК.05.01 Электросварочные работы на автоматических и полуавтоматических машинах", sid, true, false, ANY);
        addHours(g, bs + 1, T_LIMONOVA, 20, "ЛПЗ МДК.05.01 Электросварочные работы на автоматических и полуавтоматических машинах", sid, true, false, ANY);

        addHours(g, bs, T_PODCHINENNOV, 18, "УП.05", -1, false, true, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 18, "УП.05", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 40, "МДК.06.01 Технология выполнения газовой сварки и резки", sid, false, false, ANY);
        addHours(g, bs, T_PODCHINENNOV, 52, "ЛПЗ МДК.06.01 Технология выполнения газовой сварки и резки", sid, true, false, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 52, "ЛПЗ МДК.06.01 Технология выполнения газовой сварки и резки", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 20, "МДК.06.02 Технология ручной дуговой сварки", sid, false, false, ANY);
        addHours(g, bs, T_PODCHINENNOV, 60, "ЛПЗ МДК.06.02 Технология ручной дуговой сварки", sid, true, false, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 60, "ЛПЗ МДК.06.02 Технология ручной дуговой сварки", sid, true, false, ANY);

        addHours(g, bs, T_PODCHINENNOV, 36, "УП.06", -1, false, true, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 36, "УП.06", -1, false, true, ANY);
    }

    {
        int g = G_SP_2602P;
        int sid = -1;

        addHours(g, -1, T_KROPOTOVA, 18, "География", -1, false, false, ANY);
        addHours(g, -1, T_SEMENOVA, 18, "Химия", -1, false, false, ANY);
        addHours(g, -1, T_TSIMFER, 36, "История России", -1, false, false, ANY);
        addHours(g, -1, T_ERMOLINA, 40, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_NUROV, 46, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_ELAGINA, 36, "Основы финансовой грамотности", -1, false, false, ANY);
        addHours(g, -1, T_ABRAMCHUK, 56, "Техническая механика", -1, false, false, ANY);
        addHours(g, -1, T_SIVILKAEV, 60, "Технологические процессы в машиностроении", -1, false, false, ANY);
        addHours(g, -1, T_AKHMETOV, 18, "Технология конструкционных материалов", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_PODCHINENNOV, 6, "Компас 3D", sid, false, false, ANY);
        addHours(g, -1, T_PODCHINENNOV, 30, "ЛПЗ Компас 3D", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 16, "МДК.01.01 Технология сварочных работ", sid, false, false, ANY);
        addHours(g, -1, T_GORIN, 30, "ЛПЗ МДК.01.01 Технология сварочных работ", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 20, "МДК.01.02 Основное оборудование для производства сварных конструкций", sid, false, false, ANY);
        addHours(g, -1, T_AKHMETOV, 26, "ЛПЗ МДК.01.02 Основное оборудование для производства сварных конструкций", sid, true, false, ANY);

        addHours(g, -1, T_GORIN, 18, "УП.01", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 20, "МДК.03.01 Формы и методы контроля качества металлов и сварных конструкций", sid, false, false, ANY);
        addHours(g, -1, T_AKHMETOV, 26, "ЛПЗ МДК.03.01 Формы и методы контроля качества металлов и сварных конструкций", sid, true, false, ANY);

        addHours(g, -1, T_AKHMETOV, 36, "УП.03", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_LIMONOVA, 26, "МДК.05.01 Электросварочные работы на автоматических и полуавтоматических машинах", sid, false, false, ANY);
        addHours(g, -1, T_LIMONOVA, 20, "ЛПЗ МДК.05.01 Электросварочные работы на автоматических и полуавтоматических машинах", sid, true, false, ANY);

        addHours(g, -1, T_PODCHINENNOV, 18, "УП.05", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 40, "МДК.06.01 Технология выполнения газовой сварки и резки", sid, false, false, ANY);
        addHours(g, -1, T_PODCHINENNOV, 52, "ЛПЗ МДК.06.01 Технология выполнения газовой сварки и резки", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AKHMETOV, 20, "МДК.06.02 Технология ручной дуговой сварки", sid, false, false, ANY);
        addHours(g, -1, T_PODCHINENNOV, 60, "ЛПЗ МДК.06.02 Технология ручной дуговой сварки", sid, true, false, ANY);

        addHours(g, -1, T_PODCHINENNOV, 36, "УП.06", -1, false, true, ANY);
    }

    {
        int g = G_TORD_2706;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_KROPOTOVA, 14, "География", -1, false, false, ANY);
        addHours(g, -1, T_SOBOLEVA, 20, "Химия", -1, false, false, ANY);
        addHours(g, bs, T_NOVOSELOVA, 46, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_NOVOSELOVA, 46, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_POTAPOVA, 46, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_ABRAMCHUK, 20, "Математика", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_OTRAK, 2, "Информатика теория", sid, false, false, ANY);
        addHours(g, bs, T_OTRAK, 18, "Информатика", sid, true, false, ANY);
        addHours(g, bs + 1, T_OTRAK, 18, "Информатика", sid, true, false, ANY);

        addHours(g, -1, T_SALAMATINA, 56, "Инженерная графика", -1, false, false, ANY);
        addHours(g, -1, T_ABRAMCHUK, 106, "Техническая механика", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 58, "Электротехника и электроника", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_KOSHELEV, 4, "Компас 3D", sid, false, false, ANY);
        addHours(g, bs, T_KOSHELEV, 34, "ЛПЗ Компас 3D", sid, true, false, ANY);
        addHours(g, bs + 1, T_KOSHELEV, 34, "ЛПЗ Компас 3D", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_ABRAMCHUK, 30, "МДК.01.01 Устройство автомобилей", sid, false, false, ANY);
        addHours(g, bs, T_PODCHINENNOV, 16, "ЛПЗ МДК.01.01 Устройство автомобилей", sid, true, false, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 16, "ЛПЗ МДК.01.01 Устройство автомобилей", sid, true, false, ANY);

        addHours(g, bs, T_SAMTSOV, 36, "УП.01", -1, false, true, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 36, "УП.01", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_SAMTSOV, 40, "ВМДК.04.01 Слесарное дело и технические измерения", sid, false, false, ANY);
        addHours(g, bs, T_SAMTSOV, 46, "ЛПЗ ВМДК.04.01 Слесарное дело и технические измерения", sid, true, false, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 46, "ЛПЗ ВМДК.04.01 Слесарное дело и технические измерения", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_TIMEROV, 22, "ВМДК.04.02 Диагностика, техническое обслуживание и ремонт мехатронных систем автомобиля", sid, false, false, ANY);
        addHours(g, bs, T_TIMEROV, 24, "ЛПЗ ВМДК.04.02 Диагностика, техническое обслуживание и ремонт мехатронных систем автомобиля", sid, true, false, ANY);
        addHours(g, bs + 1, T_TIMEROV, 24, "ЛПЗ ВМДК.04.02 Диагностика, техническое обслуживание и ремонт мехатронных систем автомобиля", sid, true, false, ANY);

        addHours(g, bs, T_SAMTSOV, 72, "УП.04", -1, false, true, ANY);
        addHours(g, bs + 1, T_TIMEROV, 72, "УП.04", -1, false, true, ANY);

        addHours(g, -1, T_TIMEROV, 84, "ВМДК.05.01 Основы законодательства в сфере дорожного движения", -1, false, false, ANY);
        addHours(g, -1, T_TIMEROV, 46, "ВМДК.05.02 Основы управления транспортными средствами", -1, false, false, ANY);
    }

    {
        int g = G_TORD_2707P;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_KROPOTOVA, 14, "География", -1, false, false, ANY);
        addHours(g, -1, T_SEMENOVA, 20, "Химия", -1, false, false, ANY);
        addHours(g, bs, T_NOVOSELOVA, 46, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_DAVYDOVA, 46, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_POTAPOVA, 46, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_ABRAMCHUK, 20, "Математика", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_OTRAK, 2, "Информатика теория", sid, false, false, ANY);
        addHours(g, bs, T_OTRAK, 18, "Информатика", sid, true, false, ANY);
        addHours(g, bs + 1, T_OTRAK, 18, "Информатика", sid, true, false, ANY);

        addHours(g, -1, T_SALAMATINA, 56, "Инженерная графика", -1, false, false, ANY);
        addHours(g, -1, T_ABRAMCHUK, 106, "Техническая механика", -1, false, false, ANY);
        addHours(g, -1, T_DINMUKHAMETOV, 58, "Электротехника и электроника", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_KOSHELEV, 4, "Компас 3D", sid, false, false, ANY);
        addHours(g, bs, T_KOSHELEV, 34, "ЛПЗ Компас 3D", sid, true, false, ANY);
        addHours(g, bs + 1, T_KOSHELEV, 34, "ЛПЗ Компас 3D", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_ABRAMCHUK, 30, "МДК.01.01 Устройство автомобилей", sid, false, false, ANY);
        addHours(g, bs, T_PODCHINENNOV, 16, "ЛПЗ МДК.01.01 Устройство автомобилей", sid, true, false, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 16, "ЛПЗ МДК.01.01 Устройство автомобилей", sid, true, false, ANY);

        addHours(g, bs, T_SAMTSOV, 36, "УП.01", -1, false, true, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 36, "УП.01", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_SAMTSOV, 40, "ВМДК.04.01 Слесарное дело и технические измерения", sid, false, false, ANY);
        addHours(g, bs, T_SAMTSOV, 46, "ЛПЗ ВМДК.04.01 Слесарное дело и технические измерения", sid, true, false, ANY);
        addHours(g, bs + 1, T_PODCHINENNOV, 46, "ЛПЗ ВМДК.04.01 Слесарное дело и технические измерения", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_TIMEROV, 22, "ВМДК.04.02 Диагностика, техническое обслуживание и ремонт мехатронных систем автомобиля", sid, false, false, ANY);
        addHours(g, bs, T_TIMEROV, 24, "ЛПЗ ВМДК.04.02 Диагностика, техническое обслуживание и ремонт мехатронных систем автомобиля", sid, true, false, ANY);
        addHours(g, bs + 1, T_TIMEROV, 24, "ЛПЗ ВМДК.04.02 Диагностика, техническое обслуживание и ремонт мехатронных систем автомобиля", sid, true, false, ANY);

        addHours(g, bs, T_SAMTSOV, 72, "УП.04", -1, false, true, ANY);
        addHours(g, bs + 1, T_TIMEROV, 72, "УП.04", -1, false, true, ANY);

        addHours(g, -1, T_TIMEROV, 72, "ВМДК.05.01 Основы законодательства в сфере дорожного движения", -1, false, false, ANY);
        addHours(g, -1, T_TIMEROV, 46, "ВМДК.05.02 Основы управления транспортными средствами", -1, false, false, ANY);
    }

    {
        int g = G_TM_2415;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_KROPOTOVA, 14, "География", -1, false, false, ANY);
        addHours(g, -1, T_SOBOLEVA, 20, "Химия", -1, false, false, ANY);
        addHours(g, -1, T_TSIMFER, 54, "История России", -1, false, false, ANY);
        addHours(g, bs, T_ERMOLINA, 34, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_ERMOLINA, 34, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_NUROV, 46, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_KOSTAREVA, 36, "Основы бережливого производства", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 30, "Техническая механика", -1, false, false, ANY);
        addHours(g, -1, T_SADRIEVA, 54, "Метрология, стандартизация и сертификация", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 42, "Процессы формообразования и инструменты", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 34, "Технология машиностроения", -1, false, false, ANY);
        addHours(g, -1, T_KOSTAREVA, 36, "Охрана труда", -1, false, false, ANY);
        addHours(g, -1, T_NIFONTOVA, 36, "Математика в профессиональной деятельности", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 36, "Технологическая оснастка", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SIMAKOV, 24, "Компас 3D", sid, false, false, ANY);
        addHours(g, bs, T_SIMAKOV, 36, "ЛПЗ Компас 3D", sid, true, false, ANY);
        addHours(g, bs + 1, T_SIMAKOV, 36, "ЛПЗ Компас 3D", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AZARYAN, 14, "Информационные технологии в профессиональной деятельности", sid, false, false, ANY);
        addHours(g, bs, T_AZARYAN, 22, "ЛПЗ Информационные технологии в профессиональной деятельности", sid, true, false, ANY);
        addHours(g, bs + 1, T_AZARYAN, 22, "ЛПЗ Информационные технологии в профессиональной деятельности", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_VALDIYANOV, 22, "МДК.01.01 Разработка технологических процессов изготовления деталей машин с применением систем автоматизированного проектирования", sid, false, false, ANY);
        addHours(g, bs, T_VALDIYANOV, 16, "ЛПЗ МДК.01.01 Разработка технологических процессов изготовления деталей машин с применением систем автоматизированного проектирования", sid, true, false, ANY);
        addHours(g, bs + 1, T_VALDIYANOV, 16, "ЛПЗ МДК.01.01 Разработка технологических процессов изготовления деталей машин с применением систем автоматизированного проектирования", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SALAMATINA, 18, "МДК.01.02 Оформление технологической документации по процессам изготовления деталей машин", sid, false, false, ANY);
        addHours(g, bs, T_SALAMATINA, 12, "ЛПЗ МДК.01.02 Оформление технологической документации по процессам изготовления деталей машин", sid, true, false, ANY);
        addHours(g, bs + 1, T_SALAMATINA, 12, "ЛПЗ МДК.01.02 Оформление технологической документации по процессам изготовления деталей машин", sid, true, false, ANY);

        addHours(g, bs, T_SIVILKAEV, 18, "УП.01", -1, false, true, ANY);
        addHours(g, bs + 1, T_SIVILKAEV, 18, "УП.01", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_VALDIYANOV, 18, "МДК.02.01 Разработка и внедрение управляющих программ изготовления деталей машин", sid, false, false, ANY);
        addHours(g, bs, T_VALDIYANOV, 32, "ЛПЗ МДК.02.01 Разработка и внедрение управляющих программ изготовления деталей машин", sid, true, false, ANY);
        addHours(g, bs + 1, T_VALDIYANOV, 32, "ЛПЗ МДК.02.01 Разработка и внедрение управляющих программ изготовления деталей машин", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SALAMATINA, 38, "ВМДК.07.01 Технология обработки на металлорежущих станках", sid, false, false, ANY);
        addHours(g, bs, T_KLABUKOV, 32, "ЛПЗ ВМДК.07.01 Технология обработки на металлорежущих станках", sid, true, false, ANY);
        addHours(g, bs + 1, T_KLABUKOV, 32, "ЛПЗ ВМДК.07.01 Технология обработки на металлорежущих станках", sid, true, false, ANY);

        addHours(g, bs, T_KLABUKOV, 54, "УП.07", -1, false, true, ANY);
        addHours(g, bs + 1, T_KLABUKOV, 54, "УП.07", -1, false, true, ANY);
    }

    {
        int g = G_TM_2416P;
        int bs = g * PARTS_PER_GROUP;
        int sid = -1;

        addHours(g, -1, T_KROPOTOVA, 14, "География", -1, false, false, ANY);
        addHours(g, -1, T_SOBOLEVA, 20, "Химия", -1, false, false, ANY);
        addHours(g, -1, T_TSIMFER, 54, "История России", -1, false, false, ANY);
        addHours(g, bs, T_ERMOLINA, 34, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, bs + 1, T_ERMOLINA, 34, "Иностранный язык в профессиональной деятельности", -1, false, false, LES);
        addHours(g, -1, T_NUROV, 46, "Физическая культура", -1, false, false, ANY);
        addHours(g, -1, T_KOSTAREVA, 36, "Основы бережливого производства", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 30, "Техническая механика", -1, false, false, ANY);
        addHours(g, -1, T_SADRIEVA, 54, "Метрология, стандартизация и сертификация", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 42, "Процессы формообразования и инструменты", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 34, "Технология машиностроения", -1, false, false, ANY);
        addHours(g, -1, T_KOSTAREVA, 36, "Охрана труда", -1, false, false, ANY);
        addHours(g, -1, T_NIFONTOVA, 36, "Математика в профессиональной деятельности", -1, false, false, ANY);
        addHours(g, -1, T_SALAMATINA, 36, "Технологическая оснастка", -1, false, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SIMAKOV, 24, "Компас 3D", sid, false, false, ANY);
        addHours(g, bs, T_SIMAKOV, 36, "ЛПЗ Компас 3D", sid, true, false, ANY);
        addHours(g, bs + 1, T_SIMAKOV, 36, "ЛПЗ Компас 3D", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_AZARYAN, 14, "Информационные технологии в профессиональной деятельности", sid, false, false, ANY);
        addHours(g, bs, T_AZARYAN, 22, "ЛПЗ Информационные технологии в профессиональной деятельности", sid, true, false, ANY);
        addHours(g, bs + 1, T_AZARYAN, 22, "ЛПЗ Информационные технологии в профессиональной деятельности", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_VALDIYANOV, 22, "МДК.01.01 Разработка технологических процессов изготовления деталей машин с применением систем автоматизированного проектирования", sid, false, false, ANY);
        addHours(g, bs, T_VALDIYANOV, 16, "ЛПЗ МДК.01.01 Разработка технологических процессов изготовления деталей машин с применением систем автоматизированного проектирования", sid, true, false, ANY);
        addHours(g, bs + 1, T_VALDIYANOV, 16, "ЛПЗ МДК.01.01 Разработка технологических процессов изготовления деталей машин с применением систем автоматизированного проектирования", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SALAMATINA, 18, "МДК.01.02 Оформление технологической документации по процессам изготовления деталей машин", sid, false, false, ANY);
        addHours(g, bs, T_SALAMATINA, 12, "ЛПЗ МДК.01.02 Оформление технологической документации по процессам изготовления деталей машин", sid, true, false, ANY);
        addHours(g, bs + 1, T_SALAMATINA, 12, "ЛПЗ МДК.01.02 Оформление технологической документации по процессам изготовления деталей машин", sid, true, false, ANY);

        addHours(g, bs, T_SIVILKAEV, 18, "УП.01", -1, false, true, ANY);
        addHours(g, bs + 1, T_SIVILKAEV, 18, "УП.01", -1, false, true, ANY);

        sid = subj++;
        addHours(g, -1, T_VALDIYANOV, 18, "МДК.02.01 Разработка и внедрение управляющих программ изготовления деталей машин", sid, false, false, ANY);
        addHours(g, bs, T_VALDIYANOV, 32, "ЛПЗ МДК.02.01 Разработка и внедрение управляющих программ изготовления деталей машин", sid, true, false, ANY);
        addHours(g, bs + 1, T_VALDIYANOV, 32, "ЛПЗ МДК.02.01 Разработка и внедрение управляющих программ изготовления деталей машин", sid, true, false, ANY);

        sid = subj++;
        addHours(g, -1, T_SALAMATINA, 38, "ВМДК.07.01 Технология обработки на металлорежущих станках", sid, false, false, ANY);
        addHours(g, bs, T_SIMAKOV, 32, "ЛПЗ ВМДК.07.01 Технология обработки на металлорежущих станках", sid, true, false, ANY);
        addHours(g, bs + 1, T_SIMAKOV, 32, "ЛПЗ ВМДК.07.01 Технология обработки на металлорежущих станках", sid, true, false, ANY);

        addHours(g, bs, T_SIMAKOV, 54, "УП.07", -1, false, true, ANY);
        addHours(g, bs + 1, T_SIMAKOV, 54, "УП.07", -1, false, true, ANY);
    }

    int num_lessons = static_cast<int>(lessons.size());

    if (!ValidateInputLessons(lessons)) {
        std::cerr << "\nВходные данные содержат ошибки. Модель не построена.\n";
        return 1;
    }

    PrintInputDiagnostics(lessons, all_days, unavailable, start_date);

    // ====================== Модель CP-SAT ======================

    CpModelBuilder model;
    LinearExpr objective;

    std::vector<std::vector<BoolVar>> x(
        num_lessons,
        std::vector<BoolVar>(total_slots)
    );

    for (int l = 0; l < num_lessons; l++) {
        for (int t = 0; t < total_slots; t++) {
            x[l][t] = model.NewBoolVar();
        }
    }

    // ====================== Блоки УП ======================

    std::vector<BlockInfo> blocks;

    for (int l = 0; l < num_lessons; l++) {
        if (!lessons[l].is_block) continue;

        BlockInfo bi;
        bi.lesson_id = l;

        for (int d = 0; d < num_days; d++) {
            if (!IsAvailable(all_days[d], lessons[l].group, unavailable)) {
                continue;
            }

            for (int s = 0; s < SLOTS_PER_DAY - 1; s++) {
                if (!IsAllowedUpStartSlot(all_days[d], s)) {
                    continue;
                }

                int t = d * SLOTS_PER_DAY + s;
                bi.possible_starts.push_back(t);
            }
        }

        for (int i = 0; i < static_cast<int>(bi.possible_starts.size()); i++) {
            bi.start_vars.push_back(model.NewBoolVar());
        }

        blocks.push_back(bi);
    }

    std::vector<std::vector<int>> fixed_up_start_indices;
    bool fixed_up_ok = true;

    if (FIX_UP_STARTS_GREEDY) {
        fixed_up_start_indices = SelectGreedyUpStarts(
            lessons,
            blocks,
            all_days,
            num_days,
            fixed_up_ok
        );

        if (!fixed_up_ok) {
            std::cerr << "\nЖадная фиксация УП не смогла расставить все блоки. "
                << "Это уже конкретная проблема по УП/преподавателям, а не лимит решателя.\n";
            return 1;
        }
    }

    // Обычные уроки: точное число слотов
    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].is_block) continue;

        LinearExpr sum;

        for (int t = 0; t < total_slots; t++) {
            sum += x[l][t];
        }

        model.AddEquality(sum, lessons[l].total_slots);
    }

    int total_block_start_vars = 0;

    for (int block_index = 0; block_index < static_cast<int>(blocks.size()); block_index++) {
        auto& blk = blocks[block_index];
        int l = blk.lesson_id;

        int required_starts = lessons[l].total_slots / 2;
        total_block_start_vars += static_cast<int>(blk.start_vars.size());

        if (static_cast<int>(blk.possible_starts.size()) < required_starts) {
            std::cerr << "Недостаточно возможных стартов для блока: "
                << lessons[l].name
                << ", группа " << GROUP_NAME[lessons[l].group]
                << ", доступно стартов " << blk.possible_starts.size()
                << ", требуется " << required_starts
                << "\n";
            return 1;
        }

        LinearExpr start_sum;

        for (const auto& v : blk.start_vars) {
            start_sum += v;
        }

        model.AddEquality(start_sum, required_starts);

        if (FIX_UP_STARTS_GREEDY) {
            std::set<int> fixed_indices(
                fixed_up_start_indices[block_index].begin(),
                fixed_up_start_indices[block_index].end()
            );

            for (int i = 0; i < static_cast<int>(blk.start_vars.size()); i++) {
                model.AddEquality(blk.start_vars[i], fixed_indices.count(i) ? 1 : 0);
            }
        }

        std::vector<std::vector<BoolVar>> covers(total_slots);

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int st = blk.possible_starts[i];

            covers[st].push_back(blk.start_vars[i]);
            covers[st + 1].push_back(blk.start_vars[i]);
        }

        for (int t = 0; t < total_slots; t++) {
            LinearExpr cover_sum;

            for (const auto& v : covers[t]) {
                cover_sum += v;
            }

            model.AddEquality(x[l][t], cover_sum);
        }
    }

    std::cout << "Агрегированных блоковых занятий УП: " << blocks.size() << "\n";
    std::cout << "Переменных старта УП после агрегации: "
        << total_block_start_vars << "\n";

    // ====================== Недоступные дни групп ======================

    for (int d = 0; d < num_days; d++) {
        for (int g = 0; g < GROUPS; g++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                continue;
            }

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;

                for (int l = 0; l < num_lessons; l++) {
                    if (lessons[l].group == g) {
                        model.AddEquality(x[l][t], 0);
                    }
                }
            }
        }
    }

    // ====================== Группы и подгруппы ======================

    std::vector<std::vector<BoolVar>> group_busy(
        GROUPS,
        std::vector<BoolVar>(total_slots)
    );

    std::vector<std::vector<std::vector<BoolVar>>> part_busy(
        GROUPS,
        std::vector<std::vector<BoolVar>>(
            PARTS_PER_GROUP,
            std::vector<BoolVar>(total_slots)
        )
    );

    for (int g = 0; g < GROUPS; g++) {
        int base_subgroup = g * PARTS_PER_GROUP;

        for (int t = 0; t < total_slots; t++) {
            LinearExpr whole_sum;
            LinearExpr sub_sum[PARTS_PER_GROUP];

            for (int l = 0; l < num_lessons; l++) {
                if (lessons[l].group != g) continue;

                if (lessons[l].subgroup == -1) {
                    whole_sum += x[l][t];
                }
                else {
                    int part = lessons[l].subgroup - base_subgroup;

                    if (part >= 0 && part < PARTS_PER_GROUP) {
                        sub_sum[part] += x[l][t];
                    }
                }
            }

            model.AddLessOrEqual(whole_sum, 1);

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                model.AddLessOrEqual(sub_sum[p], 1);

                LinearExpr whole_plus_part;
                whole_plus_part += whole_sum;
                whole_plus_part += sub_sum[p];

                model.AddLessOrEqual(whole_plus_part, 1);
            }

            LinearExpr group_slot_sum;
            group_slot_sum += whole_sum;

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                group_slot_sum += sub_sum[p];
            }

            group_busy[g][t] = MakePositiveIndicator(model, group_slot_sum);

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                LinearExpr part_slot_sum;
                part_slot_sum += whole_sum;
                part_slot_sum += sub_sum[p];

                part_busy[g][p][t] = MakePositiveIndicator(model, part_slot_sum);
            }
        }
    }

    std::vector<std::vector<BoolVar>> student_entities;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            student_entities.push_back(part_busy[g][p]);
        }
    }

    // ====================== УП = отдельный день подгруппы ======================

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                std::vector<BoolVar> up_starts;

                for (const auto& blk : blocks) {
                    const Lesson& lesson = lessons[blk.lesson_id];

                    if (!LessonAffectsPart(lesson, g, p)) {
                        continue;
                    }

                    for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
                        int start_t = blk.possible_starts[i];

                        if (start_t / SLOTS_PER_DAY == d) {
                            up_starts.push_back(blk.start_vars[i]);
                        }
                    }
                }

                if (up_starts.empty()) {
                    continue;
                }

                LinearExpr up_start_sum;

                for (const auto& v : up_starts) {
                    up_start_sum += v;
                }

                model.AddLessOrEqual(up_start_sum, 1);

                LinearExpr part_day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    part_day_sum += part_busy[g][p][t];
                }

                BoolVar has_up = MakePositiveIndicator(model, up_start_sum);

                LinearExpr required_up_slots;
                required_up_slots += up_start_sum;
                required_up_slots += up_start_sum;

                model.AddEquality(part_day_sum, required_up_slots)
                    .OnlyEnforceIf(has_up);
            }
        }
    }

    // ====================== Преподаватели ======================

    std::vector<std::vector<BoolVar>> teacher_busy(
        TEACHERS,
        std::vector<BoolVar>(total_slots)
    );

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int t = 0; t < total_slots; t++) {
            LinearExpr sum;

            for (int l = 0; l < num_lessons; l++) {
                if (lessons[l].teacher == teacher) {
                    sum += x[l][t];
                }
            }

            model.AddLessOrEqual(sum, 1);
            teacher_busy[teacher][t] = MakePositiveIndicator(model, sum);
        }
    }

    // ====================== Блокировка преподавателя на время УП ======================

    for (const auto& blk : blocks) {
        int l = blk.lesson_id;
        int teacher = lessons[l].teacher;

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_t = blk.possible_starts[i];
            std::vector<int> blocked_slots = TeacherBlockedSlotsForUpStart(
                all_days,
                start_t
            );

            for (int blocked_t : blocked_slots) {
                LinearExpr ordinary_teacher_lessons;

                for (int other = 0; other < num_lessons; other++) {
                    if (lessons[other].is_block) {
                        continue;
                    }

                    if (lessons[other].teacher == teacher) {
                        ordinary_teacher_lessons += x[other][blocked_t];
                    }
                }

                model.AddEquality(ordinary_teacher_lessons, 0)
                    .OnlyEnforceIf(blk.start_vars[i]);
            }
        }
    }

    struct UpStartRef {
        int block_index;
        int start_index;
        int teacher;
        int start_t;
        TimeInterval interval;
    };

    std::vector<UpStartRef> up_start_refs;

    for (int b = 0; b < static_cast<int>(blocks.size()); b++) {
        const BlockInfo& blk = blocks[b];
        const Lesson& lesson = lessons[blk.lesson_id];

        for (int i = 0; i < static_cast<int>(blk.possible_starts.size()); i++) {
            int start_t = blk.possible_starts[i];
            int day = start_t / SLOTS_PER_DAY;

            up_start_refs.push_back({
                b,
                i,
                lesson.teacher,
                start_t,
                UpIntervalForStartSlot(all_days[day], start_t % SLOTS_PER_DAY)
                });
        }
    }

    for (int a = 0; a < static_cast<int>(up_start_refs.size()); a++) {
        for (int b = a + 1; b < static_cast<int>(up_start_refs.size()); b++) {
            const UpStartRef& left = up_start_refs[a];
            const UpStartRef& right = up_start_refs[b];

            if (left.teacher != right.teacher) {
                continue;
            }

            if (left.start_t / SLOTS_PER_DAY != right.start_t / SLOTS_PER_DAY) {
                continue;
            }

            if (!IntervalsOverlap(left.interval, right.interval)) {
                continue;
            }

            LinearExpr both_up;
            both_up += blocks[left.block_index].start_vars[left.start_index];
            both_up += blocks[right.block_index].start_vars[right.start_index];

            model.AddLessOrEqual(both_up, 1);
        }
    }

    // ====================== Дневные ограничения студентов ======================

    std::vector<std::vector<std::vector<BoolVar>>> student_day_has(
        GROUPS,
        std::vector<std::vector<BoolVar>>(
            PARTS_PER_GROUP,
            std::vector<BoolVar>(num_days)
        )
    );

    std::vector<BoolVar> student_five_pair_day_vars;

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            LinearExpr visible_group_day_sum;

            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;
                visible_group_day_sum += group_busy[g][t];
            }

            if (LIMIT_VISIBLE_GROUP_PAIRS_PER_DAY) {
                model.AddLessOrEqual(visible_group_day_sum, MAX_STUDENT_PAIRS_PER_DAY);
            }
        }
    }

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            for (int d = 0; d < num_days; d++) {
                LinearExpr day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    day_sum += part_busy[g][p][t];
                }

                BoolVar has = MakePositiveIndicator(model, day_sum);
                student_day_has[g][p][d] = has;

                AddMinIfPositive(
                    model,
                    day_sum,
                    has,
                    MIN_STUDENT_PAIRS_PER_STUDY_DAY
                );

                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY);

                BoolVar is_five_pair_day = model.NewBoolVar();
                model.AddEquality(day_sum, MAX_STUDENT_PAIRS_PER_DAY)
                    .OnlyEnforceIf(is_five_pair_day);
                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY - 1)
                    .OnlyEnforceIf(is_five_pair_day.Not());
                student_five_pair_day_vars.push_back(is_five_pair_day);
            }
        }
    }

    if (FORCE_BOTH_SUBGROUPS_SAME_STUDY_DAYS) {
        for (int g = 0; g < GROUPS; g++) {
            for (int d = 0; d < num_days; d++) {
                model.AddEquality(student_day_has[g][0][d], student_day_has[g][1][d]);
            }
        }
    }

    // ====================== Недельное растягивание расписания ======================

    std::vector<int> week_index(num_days);
    for (int d = 0; d < num_days; d++) {
        week_index[d] = WeekIndexFromStart(start_date, all_days[d]);
    }

    for (int g = 0; g < GROUPS; g++) {
        std::map<int, std::vector<int>> week_days;

        for (int d = 0; d < num_days; d++) {
            if (IsAvailable(all_days[d], g, unavailable)) {
                week_days[week_index[d]].push_back(d);
            }
        }

        for (const auto& item : week_days) {
            const std::vector<int>& days = item.second;
            if (days.empty()) {
                continue;
            }

            int required_days = std::min(
                MIN_STUDENT_STUDY_DAYS_PER_WEEK,
                static_cast<int>(days.size())
            );

            LinearExpr week_study_days;
            for (int d : days) {
                week_study_days += student_day_has[g][0][d];
            }

            if (HARD_MIN_STUDY_DAYS_PER_WEEK) {
                model.AddGreaterOrEqual(week_study_days, required_days);
            }
            else if (USE_QUALITY_OBJECTIVE) {
                IntVar missing = model.NewIntVar(Domain(0, required_days));
                LinearExpr week_with_missing;
                week_with_missing += week_study_days;
                week_with_missing += missing;

                model.AddGreaterOrEqual(week_with_missing, required_days);
                objective += missing * GROUP_WEEK_MISSING_DAY_WEIGHT;
            }
        }
    }

    // ====================== Минимум 2 пары в день у преподавателей ======================

    if (HARD_MIN_2_TEACHER_PAIRS_PER_DAY) {
        for (int teacher = 0; teacher < TEACHERS; teacher++) {
            for (int d = 0; d < num_days; d++) {
                LinearExpr day_sum;

                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;
                    day_sum += teacher_busy[teacher][t];
                }

                AddMin2IfPositive(model, day_sum);
            }
        }
    }

    // ====================== Мягкое растягивание предметов по семестру ======================

    if (USE_QUALITY_OBJECTIVE) {
        AddSubjectSpreadPenalties(
            model,
            lessons,
            x,
            all_days,
            unavailable,
            objective
        );
    }

    // ====================== Убираем окна ======================

    if (HARD_NO_STUDENT_WINDOWS) {
        AddNoWindowsHard(model, student_entities, num_days);
    }

    if (HARD_NO_TEACHER_WINDOWS) {
        AddNoWindowsHard(model, teacher_busy, num_days);
    }

    // ====================== ЛПЗ только после теории ======================

    if (ENFORCE_THEORY_BEFORE_LABS) {
        std::map<std::pair<int, int>, std::vector<int>> theory_of;
        std::map<std::pair<int, int>, std::vector<int>> lab_of;

        for (int l = 0; l < num_lessons; l++) {
            if (lessons[l].subject_id < 0) continue;

            auto key = std::make_pair(lessons[l].group, lessons[l].subject_id);

            if (lessons[l].is_lab) {
                lab_of[key].push_back(l);
            }
            else {
                theory_of[key].push_back(l);
            }
        }

        for (const auto& item : lab_of) {
            const auto& key = item.first;
            const auto& labs = item.second;

            auto it = theory_of.find(key);
            if (it == theory_of.end()) continue;

            const auto& theories = it->second;
            if (theories.empty()) continue;

            if (STRICT_ALL_THEORY_BEFORE_LABS) {
                IntVar last_theory = model.NewIntVar(Domain(0, total_slots - 1));

                for (int l_th : theories) {
                    for (int t = 0; t < total_slots; t++) {
                        model.AddGreaterOrEqual(last_theory, t).OnlyEnforceIf(x[l_th][t]);
                    }
                }

                for (int l_lab : labs) {
                    for (int t = 0; t < total_slots; t++) {
                        model.AddLessThan(last_theory, t).OnlyEnforceIf(x[l_lab][t]);
                    }
                }
            }
            else {
                int group = key.first;
                std::vector<std::vector<int>> buckets =
                    BuildAvailableDayBuckets(group, all_days, unavailable);

                if (buckets.empty()) {
                    continue;
                }

                LinearExpr initial_theory_sum;
                LinearExpr initial_lab_sum;

                for (int l_th : theories) {
                    for (int d : buckets.front()) {
                        for (int s = 0; s < SLOTS_PER_DAY; s++) {
                            int t = d * SLOTS_PER_DAY + s;
                            initial_theory_sum += x[l_th][t];
                        }
                    }
                }

                for (int l_lab : labs) {
                    for (int d : buckets.front()) {
                        for (int s = 0; s < SLOTS_PER_DAY; s++) {
                            int t = d * SLOTS_PER_DAY + s;
                            initial_lab_sum += x[l_lab][t];
                        }
                    }
                }

                model.AddGreaterOrEqual(
                    initial_theory_sum,
                    MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS
                );
                model.AddEquality(initial_lab_sum, 0);
            }
        }

    }

    // ====================== Кампусы ======================

    std::vector<std::vector<IntVar>> group_day_campus(
        GROUPS,
        std::vector<IntVar>(num_days)
    );

    std::vector<std::vector<IntVar>> teacher_day_campus(
        TEACHERS,
        std::vector<IntVar>(num_days)
    );

    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            group_day_campus[g][d] = model.NewIntVar(Domain(0, 1));
        }
    }

    for (int teacher = 0; teacher < TEACHERS; teacher++) {
        for (int d = 0; d < num_days; d++) {
            teacher_day_campus[teacher][d] = model.NewIntVar(Domain(0, 1));
        }
    }

    if (ENFORCE_DAY_CAMPUS_RULES) {
        for (int l = 0; l < num_lessons; l++) {
            int group = lessons[l].group;
            int teacher = lessons[l].teacher;

            for (int d = 0; d < num_days; d++) {
                for (int s = 0; s < SLOTS_PER_DAY; s++) {
                    int t = d * SLOTS_PER_DAY + s;

                    model.AddEquality(group_day_campus[group][d], teacher_day_campus[teacher][d])
                        .OnlyEnforceIf(x[l][t]);

                    if (lessons[l].allowed_campuses.size() == 1) {
                        int campus = static_cast<int>(*lessons[l].allowed_campuses.begin());

                        model.AddEquality(group_day_campus[group][d], campus)
                            .OnlyEnforceIf(x[l][t]);

                        model.AddEquality(teacher_day_campus[teacher][d], campus)
                            .OnlyEnforceIf(x[l][t]);
                    }
                }
            }
        }
    }

    // ====================== Целевая функция качества ======================

    if (USE_QUALITY_OBJECTIVE) {
        for (const auto& v : student_five_pair_day_vars) {
            objective += v * STUDENT_FIVE_PAIR_DAY_WEIGHT;
        }

        if (!HARD_NO_TEACHER_WINDOWS && OPTIMIZE_TEACHER_WINDOWS) {
            std::vector<BoolVar> teacher_gaps =
                CreateWindowPenaltyVars(model, teacher_busy, num_days);

            for (const auto& gap : teacher_gaps) {
                objective += gap * TEACHER_WINDOW_WEIGHT;
            }
        }

        if (STUDENT_LATE_SLOT_WEIGHT > 0) {
            for (const auto& busy : student_entities) {
                for (int d = 0; d < num_days; d++) {
                    int base = d * SLOTS_PER_DAY;

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        objective += busy[base + s] * (s * STUDENT_LATE_SLOT_WEIGHT);
                    }
                }
            }
        }

        if (TEACHER_LATE_SLOT_WEIGHT > 0) {
            for (int teacher = 0; teacher < TEACHERS; teacher++) {
                for (int d = 0; d < num_days; d++) {
                    int base = d * SLOTS_PER_DAY;

                    for (int s = 0; s < SLOTS_PER_DAY; s++) {
                        objective += teacher_busy[teacher][base + s] * (s * TEACHER_LATE_SLOT_WEIGHT);
                    }
                }
            }
        }

        model.Minimize(objective);
    }

    // ====================== Решение ======================

    std::cout << "Запуск решателя...\n";

    CpModelProto model_proto = model.Build();

    std::cout << "\n========== Размер модели ==========" << "\n";
    std::cout << "Переменных: " << model_proto.variables_size() << "\n";
    std::cout << "Ограничений: " << model_proto.constraints_size() << "\n";
    std::cout << "Размер proto: " << (model_proto.ByteSizeLong() / (1024.0 * 1024.0)) << " МБ\n";
    std::cout << "\n========== Режим жёсткости ==========" << "\n";
    std::cout << "USE_QUALITY_OBJECTIVE=" << USE_QUALITY_OBJECTIVE << "\n";
    std::cout << "STOP_AFTER_FIRST_SOLUTION=" << STOP_AFTER_FIRST_SOLUTION << "\n";
    std::cout << "HARD_NO_STUDENT_WINDOWS=" << HARD_NO_STUDENT_WINDOWS << "\n";
    std::cout << "LIMIT_VISIBLE_GROUP_PAIRS_PER_DAY=" << LIMIT_VISIBLE_GROUP_PAIRS_PER_DAY << "\n";
    std::cout << "FORCE_BOTH_SUBGROUPS_SAME_STUDY_DAYS=" << FORCE_BOTH_SUBGROUPS_SAME_STUDY_DAYS << "\n";
    std::cout << "ENFORCE_THEORY_BEFORE_LABS=" << ENFORCE_THEORY_BEFORE_LABS << "\n";
    std::cout << "ENFORCE_DAY_CAMPUS_RULES=" << ENFORCE_DAY_CAMPUS_RULES << "\n";

    SatParameters params;
    params.set_num_search_workers(SOLVER_WORKERS);
    params.set_max_time_in_seconds(SOLVER_TIME_LIMIT_SECONDS);
    params.set_random_seed(1);
    params.set_max_memory_in_mb(SOLVER_MAX_MEMORY_MB);
    params.set_linearization_level(0);
    params.set_symmetry_level(2);

    if (STOP_AFTER_FIRST_SOLUTION) {
        params.set_stop_after_first_solution(true);
    }

    // params.set_log_search_progress(true);

    operations_research::sat::Model sat_model;
    sat_model.Add(NewSatParameters(params));

    CpSolverResponse response = SolveCpModel(model_proto, &sat_model);

    std::cout << "\n========== Результат ==========\n";
    std::cout << "Status: " << CpSolverStatus_Name(response.status()) << "\n";
    std::cout << CpSolverResponseStats(response) << "\n";

    if (response.status() == CpSolverStatus::OPTIMAL ||
        response.status() == CpSolverStatus::FEASIBLE) {

        int student_windows = CountWindows(response, student_entities, num_days);
        int teacher_windows = CountWindows(response, teacher_busy, num_days);
        int max_student_pairs = MaxStudentPairsInDay(response, part_busy, num_days);
        int five_pair_days = CountFivePairStudentDays(response, part_busy, num_days);
        int up_day_violations = CountUpDayRuleViolations(
            response,
            lessons,
            x,
            part_busy,
            num_days
        );
        int up_teacher_lock_violations = CountUpTeacherLockViolations(
            response,
            lessons,
            x,
            blocks,
            all_days
        );

        std::cout << "\nРасписание найдено.\n";
        std::cout << "Окон у студентов: " << student_windows << "\n";
        std::cout << "Окон у преподавателей: " << teacher_windows << "\n";
        std::cout << "Максимум пар у студента за день: " << max_student_pairs << "\n";
        std::cout << "Дней по 5 пар у подгрупп: " << five_pair_days << "\n";
        std::cout << "Нарушений правила УП-день: " << up_day_violations << "\n";
        std::cout << "Нарушений занятости преподавателя во время УП: "
            << up_teacher_lock_violations << "\n";

        if (USE_QUALITY_OBJECTIVE) {
            std::cout << "Objective value: " << response.objective_value() << "\n";
            std::cout << "Best bound: " << response.best_objective_bound() << "\n";
        }

        WriteAllGroupsTxt(
            "raspisanie_all.txt",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus
        );

        for (int g = 0; g < GROUPS; g++) {
            WriteGroupScheduleTxt(
                "raspisanie_" + GROUP_FILE_NAME[g] + ".txt",
                response,
                all_days,
                lessons,
                x,
                group_busy,
                group_day_campus,
                g
            );
        }

        WriteGroupsCsv(
            "raspisanie_groups.csv",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus
        );

        WriteTeachersTxt(
            "raspisanie_teachers.txt",
            response,
            all_days,
            lessons,
            x,
            blocks,
            teacher_busy,
            teacher_day_campus
        );

        std::cout << "\nФайлы созданы:\n";
        std::cout << "  raspisanie_all.txt\n";

        for (int g = 0; g < GROUPS; g++) {
            std::cout << "  raspisanie_" << GROUP_FILE_NAME[g] << ".txt\n";
        }

        std::cout << "  raspisanie_groups.csv\n";
        std::cout << "  raspisanie_teachers.txt\n";

    }
    else if (response.status() == CpSolverStatus::INFEASIBLE) {
        std::cout << "\nМодель противоречива.\n";
        std::cout << "Что можно ослабить первым:\n";
        std::cout << "  1) HARD_NO_STUDENT_WINDOWS = false\n";
        std::cout << "  2) MIN_STUDENT_STUDY_DAYS_PER_WEEK = 1\n";
        std::cout << "  3) увеличить SUBJECT_BUCKET_EXTRA_SLOTS до 3 или 4\n";
        std::cout << "  4) уменьшить MIN_SUBJECT_SPREAD_TOTAL_SLOTS\n";
        std::cout << "  5) HARD_MIN_2_TEACHER_PAIRS_PER_DAY оставить false\n";
        std::cout << "  6) STRICT_ALL_THEORY_BEFORE_LABS оставить false\n";
        std::cout << "  7) если УП-дни слишком жёсткие, проверь правило "
            << "student_day_has[g][0][d] == student_day_has[g][1][d]\n";
    }
    else if (response.status() == CpSolverStatus::UNKNOWN) {
        std::cout << "\nРешатель не успел найти или доказать решение за лимит времени.\n";
        std::cout << "Что можно сделать:\n";
        std::cout << "  1) увеличить SOLVER_TIME_LIMIT_SECONDS\n";
        std::cout << "  2) уменьшить SOLVER_WORKERS до 2, если не хватает ОЗУ\n";
        std::cout << "  3) увеличить SUBJECT_BUCKET_EXTRA_SLOTS до 3 или 4\n";
        std::cout << "  4) временно поставить HARD_NO_STUDENT_WINDOWS = false\n";
    }
    else if (response.status() == CpSolverStatus::MODEL_INVALID) {
        std::cout << "\nМодель некорректна. Проверь CpSolverResponseStats выше.\n";
    }
    else {
        std::cout << "\nРешение не найдено. Статус: "
            << CpSolverStatus_Name(response.status())
            << "\n";
    }

    return 0;
}

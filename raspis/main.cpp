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
// Дополнительно ниже преподаватель блокируется на реальное время УП
// по расписанию звонков: например, во вторник-пятницу дневное УП
// 13:00-17:00 блокирует пары 3, 4 и 5 у этого преподавателя.
constexpr int UP_MORNING_MODEL_START_SLOT = 0;
constexpr int UP_AFTERNOON_MODEL_START_SLOT = 2;

constexpr int GROUPS = 2;
constexpr int PARTS_PER_GROUP = 2;
constexpr int TEACHERS = 8;

constexpr double SOLVER_TIME_LIMIT_SECONDS = 500.0;

// CP-SAT держит отдельные структуры поиска на каждый worker.
// 4 обычно заметно экономнее по ОЗУ, чем 8-10. Если памяти мало, поставь 2.
constexpr int SOLVER_WORKERS = 4;
constexpr double SOLVER_MAX_MEMORY_MB = 8072.0;

// true = остановиться на первом найденном допустимом расписании.
// Полезно, если нужно сначала просто получить решение.
constexpr bool STOP_AFTER_FIRST_SOLUTION = false;

// У студентов: минимум 2 пары в учебный день, максимум 5 пар в день.
// Ограничение считается отдельно для каждой подгруппы.
constexpr int MIN_STUDENT_PAIRS_PER_STUDY_DAY = 2;
constexpr int MAX_STUDENT_PAIRS_PER_DAY = 5;

// каждая группа должна учиться минимум N дней в каждую доступную учебную неделю.
// Если в неделе доступен только 1 день, требуется 1 день.
// По умолчанию это мягкая цель, а не причина INFEASIBLE.
constexpr int MIN_STUDENT_STUDY_DAYS_PER_WEEK = 2;
constexpr bool HARD_MIN_STUDY_DAYS_PER_WEEK = false;
constexpr int GROUP_WEEK_MISSING_DAY_WEIGHT = 2000;

// Растягивание предметов без жёсткого требования "каждый предмет в каждой
// трети семестра". Вместо этого доступные дни группы режутся на корзины
// примерно по две учебные недели. Предмет должен появляться во многих корзинах,
// но лимит на часы в корзине адаптируется к общей нагрузке предмета.
constexpr int SUBJECT_SPREAD_BUCKET_AVAILABLE_DAYS = 12;
constexpr int MIN_SUBJECT_SPREAD_TOTAL_SLOTS = 4;
constexpr int SUBJECT_BUCKET_EXTRA_SLOTS = 2;
constexpr int SUBJECT_BUCKET_MIN_CAPACITY = 2;
constexpr int SUBJECT_MISSING_BUCKET_WEIGHT = 1200;
constexpr int SUBJECT_BUCKET_OVERLOAD_WEIGHT = 120;
constexpr int SUBJECT_MISSING_SEGMENT_WEIGHT = 1500;

// Для УП и других блоковых занятий одно появление = минимум 2 пары подряд.
// Для обычных предметов одно появление может быть 1 парой.
constexpr int NORMAL_SUBJECT_ACTIVE_BUCKET_UNIT = 2;
constexpr int BLOCK_SUBJECT_ACTIVE_BUCKET_UNIT = 4;

// Жёсткое запрещение окон у студентов часто делает модель противоречивой.
// По умолчанию выключено: сначала получаем расписание, потом при необходимости усиливаем качество.
constexpr bool HARD_NO_STUDENT_WINDOWS = true;

// Жёстко запрещать окна у преподавателей не советую:
// модель может стать слишком жёсткой.
constexpr bool HARD_NO_TEACHER_WINDOWS = false;

// Мягкая оптимизация качества расписания.
// Включена, потому что растягивание по неделям и предметам теперь мягкое.
constexpr bool USE_QUALITY_OBJECTIVE = true;

// Минимум 2 пары в день для преподавателей — НЕ требование задачи.
// Если включить, модель легко становится противоречивой на малых нагрузках.
constexpr bool HARD_MIN_2_TEACHER_PAIRS_PER_DAY = false;

// Требование "все лабораторные только после всех теорий" часто делает
// расписание слишком жёстким и мешает растягивать предметы.
// false означает более лёгкий вариант: в первой двухнедельной корзине
// предмета ставится теория и запрещаются ЛПЗ; дальше теория и ЛПЗ могут
// чередоваться, чтобы предмет не исчезал сразу после января.
constexpr bool STRICT_ALL_THEORY_BEFORE_LABS = false;
constexpr int MIN_INITIAL_THEORY_SLOTS_BEFORE_LABS = 1;

// Чтобы экономить память, окна преподавателей по умолчанию не оптимизируются.
constexpr bool OPTIMIZE_TEACHER_WINDOWS = false;

// Чем больше вес, тем сильнее решатель старается убрать окна преподавателей.
constexpr int TEACHER_WINDOW_WEIGHT = 1;

// Штраф за каждый день, где у конкретной подгруппы ровно 5 пар.
// Это не запрещает 5 пар, но делает такие дни нежелательными.
constexpr int STUDENT_FIVE_PAIR_DAY_WEIGHT = 100;

// Сдвиг занятий студентов к началу дня.
constexpr int STUDENT_LATE_SLOT_WEIGHT = 1;

// Сдвиг занятий преподавателей к началу дня.
constexpr int TEACHER_LATE_SLOT_WEIGHT = 0;

// Индексы преподавателей
constexpr int T_NOVOSELOVA = 0;
constexpr int T_DAVYDOVA = 1;
constexpr int T_NUROV = 2;
constexpr int T_POTAPOVA = 3;
constexpr int T_SERYANINA = 4;
constexpr int T_GOBOV = 5;
constexpr int T_SAMTSOVA = 6;
constexpr int T_GARBUZOV = 7;

const std::array<std::string, GROUPS> GROUP_NAME = {
    "ИСП-3304",
    "ИСП-3305п"
};

const std::array<std::string, TEACHERS> TEACHER_NAME = {
    "Новосёлова",
    "Давыдова",
    "Нуров",
    "Потапова",
    "Серянина",
    "Гобов",
    "Самцова",
    "Гарбузов"
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
    int subgroup;       // -1 = вся группа, иначе 0/1 или 2/3
    int teacher;
    int total_slots;
    std::string name;
    int subject_id;     // для связки теория-лабы
    bool is_lab;
    bool is_block;      // true = УП, одно появление = 2 пары нагрузки, время берётся из фиксированных смен УП
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

// Возвращает: 1=ПН, 2=ВТ, ..., 6=СБ, 7=ВС
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

    // Zeller:
    // h=0 СБ, h=1 ВС, h=2 ПН, ..., h=6 ПТ
    // нужно:
    // 1 ПН, ..., 6 СБ, 7 ВС
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
    // Интервалы именно для пар 1-7. Поднятие флага и 0 урок не моделируются.
    if (slot < 0 || slot >= SLOTS_PER_DAY) {
        return { 0, 0 };
    }

    if (day_of_week == 1) {
        // Понедельник
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
        // Вторник-пятница
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
        // Суббота
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

// Жёсткое устранение окон:
// если есть занятие слева и справа, то все слоты между ними тоже должны быть заняты.
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

// Мягкие переменные окон для минимизации.
// gap=1, если в этот слот есть окно: до него есть занятия, после него есть занятия,
// а сам слот пустой.
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

                // gap => before
                model.AddImplication(gap, before);

                // gap => after
                model.AddImplication(gap, after);

                // gap => текущий слот пустой
                model.AddImplication(gap, busy[base + s].Not());

                // before && after && !busy => gap
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
    // Мягче и устойчивее, чем жёсткое деление на 3 части семестра:
    // 1) доступные дни группы режутся на корзины примерно по 2 учебные недели;
    // 2) предмет штрафуется, если встречается в слишком малом числе корзин;
    // 3) избыток часов предмета в одной корзине штрафуется, но не запрещается.
    // Если часов мало, предмет естественно получается примерно "раз в пару недель".
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

                // Чисто общегрупповые предметы дали бы полностью одинаковые
                // штрафы для обеих подгрупп. Оставляем только один набор.
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

                    // overload >= bucket_sum - soft_max_per_bucket
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

                // Дополнительная мягкая страховка от "закрыли предмет в январе".
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

                // Если УП есть, то день этой подгруппы должен состоять
                // ровно из одного двухпарного УП-блока и больше ни из чего.
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

    // Обычные пары выводятся по x[l][t].
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

    // УП в преподавательском расписании показывается во всех парах,
    // которые пересекаются с реальным временем УП. Так преподаватель не выглядит
    // свободным, например, на 5 паре, если дневное УП идёт до 17:00/17:30.
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

// ====================== Основная программа ======================

int main() {
    std::setlocale(LC_ALL, "ru_RU.UTF-8");

    Date start_date = { 2026, 1, 12 };
    Date end_date = { 2026, 6, 19 };

    std::map<int, std::vector<std::pair<Date, Date>>> unavailable;
    unavailable[0] = { {{2026, 4, 30}, {2026, 6, 19}} }; // ИСП-3304 ПП
    unavailable[1] = { {{2026, 4, 30}, {2026, 6, 19}} }; // ИСП-3304 ПП
    //unavailable[1] = { {{2026, 3, 20}, {2026, 3, 30}} }; // ИСП-3305п сборы

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

    // Иностранный язык — только Лесная
    int engId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, bs, T_NOVOSELOVA, 13, "Ин. язык", engId, false, false, { LESNAYA });
        add(g, bs + 1, T_DAVYDOVA, 13, "Ин. язык", engId, false, false, { LESNAYA });
    }

    // Физкультура
    for (int g = 0; g < GROUPS; g++) {
        add(g, -1, T_NUROV, 14, "Физическая культура", -1, false, false, { LESNAYA, KRIVOUSOVA });
    }

    // БЖД
    for (int g = 0; g < GROUPS; g++) {
        add(g, -1, T_POTAPOVA, 17, "БЖД", -1, false, false, { LESNAYA, KRIVOUSOVA });
    }

    // Экономика
    add(0, -1, T_SERYANINA, 12, "Экономика", -1, false, false, { LESNAYA, KRIVOUSOVA });
    add(1, -1, T_GARBUZOV, 12, "Экономика", -1, false, false, { LESNAYA, KRIVOUSOVA });

    // МДК.01.01 Разработка программных модулей
    int pmId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_GARBUZOV, 8, "МДК.01.01 теория", pmId, false, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, -1, T_GARBUZOV, 15, "МДК.01.01 КП", -1, false, false, { LESNAYA, KRIVOUSOVA });
    }

    // МДК.04.01 Технология разработки и защиты БД
    int dbId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_SAMTSOVA, 36, "МДК.04.01 теория", dbId, false, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, { LESNAYA, KRIVOUSOVA });

        // Один агрегированный предмет на 36 пар, который ниже раскладывается
        // на 18 стартов по 2 соседние пары.
        add(g, bs, T_SAMTSOVA, 36, "УП.04", -1, false, true, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_SAMTSOVA, 36, "УП.04", -1, false, true, { LESNAYA, KRIVOUSOVA });
    }

    // ВМДК.05.01 Управление и автоматизация БД
    int autoId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_GARBUZOV, 16, "ВМДК.05.01 теория", autoId, false, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, { LESNAYA, KRIVOUSOVA });

        // 9 двойных блоков УП.05 = 18 пар на подгруппу.
        add(g, bs, T_GOBOV, 18, "УП.05", -1, false, true, { LESNAYA, KRIVOUSOVA });
        add(g, bs + 1, T_GOBOV, 18, "УП.05", -1, false, true, { LESNAYA, KRIVOUSOVA });
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

    // x[l][t] = 1, если занятие l стоит в глобальном слоте t
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

    // Обычные уроки: точное число слотов
    for (int l = 0; l < num_lessons; l++) {
        if (lessons[l].is_block) continue;

        LinearExpr sum;

        for (int t = 0; t < total_slots; t++) {
            sum += x[l][t];
        }

        model.AddEquality(sum, lessons[l].total_slots);
    }

    // Блоки УП: агрегированный предмет раскладывается на нужное число
    // стартов, каждый старт занимает 2 соседние пары. Например 36 пар УП.04
    // превращаются в 18 двойных блоков, но без 18 одинаковых Lesson-строк.
    int total_block_start_vars = 0;

    for (auto& blk : blocks) {
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

            // В одном слоте не может быть двух занятий всей группы.
            model.AddLessOrEqual(whole_sum, 1);

            // Каждая подгруппа не может иметь два занятия одновременно.
            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                model.AddLessOrEqual(sub_sum[p], 1);

                // Занятие всей группы конфликтует с занятием любой подгруппы.
                LinearExpr whole_plus_part;
                whole_plus_part += whole_sum;
                whole_plus_part += sub_sum[p];

                model.AddLessOrEqual(whole_plus_part, 1);
            }

            // group_busy = есть хотя бы одно занятие у группы в этом слоте.
            LinearExpr group_slot_sum;
            group_slot_sum += whole_sum;

            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                group_slot_sum += sub_sum[p];
            }

            group_busy[g][t] = MakePositiveIndicator(model, group_slot_sum);

            // part_busy = есть занятие у конкретной подгруппы.
            // Занятие всей группы считается занятием для обеих подгрупп.
            for (int p = 0; p < PARTS_PER_GROUP; p++) {
                LinearExpr part_slot_sum;
                part_slot_sum += whole_sum;
                part_slot_sum += sub_sum[p];

                part_busy[g][p][t] = MakePositiveIndicator(model, part_slot_sum);
            }
        }
    }

    // Список студенческих сущностей: каждая подгруппа отдельно.
    std::vector<std::vector<BoolVar>> student_entities;

    for (int g = 0; g < GROUPS; g++) {
        for (int p = 0; p < PARTS_PER_GROUP; p++) {
            student_entities.push_back(part_busy[g][p]);
        }
    }

    // ====================== УП = отдельный день подгруппы ======================
    //
    // Если у подгруппы в день есть УП, то:
    // 1) в этот день у этой подгруппы может быть только один УП-блок;
    // 2) день этой подгруппы состоит ровно из 2 пар этого УП;
    // 3) никаких других УП, ЛПЗ, теорий, физкультуры, БЖД и т.п.
    //    в этот день у этой подгруппы быть не может.
    //
    // part_busy уже учитывает и занятия подгруппы, и занятия всей группы.
    // Поэтому общегрупповая пара тоже запрещается в УП-день этой подгруппы.
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

                // Запрещает два УП-блока в один день одной подгруппе:
                // УП.04 + УП.05, два блока УП.04, два блока УП.05 и т.п.
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

                // Если УП есть, день подгруппы должен состоять ровно из 2 пар:
                // эти 2 пары — сам УП-блок, больше ничего.
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

            // Преподаватель не может вести больше одной пары одновременно.
            model.AddLessOrEqual(sum, 1);

            teacher_busy[teacher][t] = MakePositiveIndicator(model, sum);
        }
    }

    // ====================== Блокировка преподавателя на время УП ======================
    //
    // УП в учебной нагрузке учитывается как 2 пары, но фактически занимает
    // отдельный временной интервал:
    //   ПН:    09:00-13:00 или 13:30-17:30
    //   ВТ-ПТ: 08:30-12:30 или 13:00-17:00
    //   СБ:    08:30-12:00 или 12:30-16:30
    //
    // Поэтому преподавателю, который ведёт УП, запрещены обычные пары,
    // пересекающиеся с реальным временем УП. Другие УП этого же преподавателя
    // запрещаются только если их реальные интервалы пересекаются. Так утреннее
    // и дневное УП в один день не конфликтуют друг с другом, если по времени
    // они действительно не пересекаются.
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

            // В расписании группы за день должно быть не больше 5 занятых номеров пар.
            // Это дополнительно защищает вывод от дней, где подгруппы разнесены на 6-7 пар.
            model.AddLessOrEqual(visible_group_day_sum, MAX_STUDENT_PAIRS_PER_DAY);
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

                // Если подгруппа учится в этот день, то минимум 2 пары.
                AddMinIfPositive(
                    model,
                    day_sum,
                    has,
                    MIN_STUDENT_PAIRS_PER_STUDY_DAY
                );

                // У конкретного студента / подгруппы не может быть больше 5 пар в день.
                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY);

                // Индикатор дня с 5 парами. Используется в целевой функции качества.
                BoolVar is_five_pair_day = model.NewBoolVar();
                model.AddEquality(day_sum, MAX_STUDENT_PAIRS_PER_DAY)
                    .OnlyEnforceIf(is_five_pair_day);
                model.AddLessOrEqual(day_sum, MAX_STUDENT_PAIRS_PER_DAY - 1)
                    .OnlyEnforceIf(is_five_pair_day.Not());
                student_five_pair_day_vars.push_back(is_five_pair_day);
            }
        }
    }

    // Если в конкретный день учится одна подгруппа, обязана учиться и другая.
    // То есть не будет дней, когда часть студентов группы вообще не учится.
    for (int g = 0; g < GROUPS; g++) {
        for (int d = 0; d < num_days; d++) {
            model.AddEquality(student_day_has[g][0][d], student_day_has[g][1][d]);
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
            // Строгий режим: все теоретические занятия предмета раньше всех ЛПЗ.
            // Оставлен как переключатель, но по умолчанию выключен.
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
            // Быстрый режим: в первой доступной двухнедельной корзине предмета
            // должна быть теория, а ЛПЗ в этой корзине запрещены. Это сохраняет
            // смысл "сначала теория", но не заставляет закрывать всю теорию
            // перед первой лабораторной.
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

    // ====================== Кампусы ======================

    // Кампус теперь задаётся не на каждый слот, а на весь день.
    // Это проще и корректнее: у группы и преподавателя один кампус в течение дня.
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

    for (int l = 0; l < num_lessons; l++) {
        int group = lessons[l].group;
        int teacher = lessons[l].teacher;

        for (int d = 0; d < num_days; d++) {
            for (int s = 0; s < SLOTS_PER_DAY; s++) {
                int t = d * SLOTS_PER_DAY + s;

                // Если занятие стоит, группа и преподаватель в этот день
                // находятся в одном кампусе.
                model.AddEquality(group_day_campus[group][d], teacher_day_campus[teacher][d])
                    .OnlyEnforceIf(x[l][t]);

                // Если занятие разрешено только в одном кампусе, фиксируем кампус дня.
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

    // ====================== Целевая функция качества ======================

    if (USE_QUALITY_OBJECTIVE) {
        // Дни по 5 пар разрешены, но штрафуются.
        for (const auto& v : student_five_pair_day_vars) {
            objective += v * STUDENT_FIVE_PAIR_DAY_WEIGHT;
        }

        // Мягко убираем окна у преподавателей только если явно включено.
        if (!HARD_NO_TEACHER_WINDOWS && OPTIMIZE_TEACHER_WINDOWS) {
            std::vector<BoolVar> teacher_gaps =
                CreateWindowPenaltyVars(model, teacher_busy, num_days);

            for (const auto& gap : teacher_gaps) {
                objective += gap * TEACHER_WINDOW_WEIGHT;
            }
        }

        // Сдвигаем занятия студентов к началу дня.
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

        // Сдвигаем занятия преподавателей к началу дня.
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

    // Для отладки можно включить подробный лог:
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

        WriteGroupScheduleTxt(
            "raspisanie_ISP-3304.txt",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            0
        );

        WriteGroupScheduleTxt(
            "raspisanie_ISP-3305p.txt",
            response,
            all_days,
            lessons,
            x,
            group_busy,
            group_day_campus,
            1
        );

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
        std::cout << "  raspisanie_ISP-3304.txt\n";
        std::cout << "  raspisanie_ISP-3305p.txt\n";
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
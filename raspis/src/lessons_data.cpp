#include "lessons_data.h"

#include "config.h"

namespace timetable {

std::vector<Lesson> CreateLessons() {
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

    int engId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, bs, T_NOVOSELOVA, 13, "Ин. язык", engId, false, false, {LESNAYA});
        add(g, bs + 1, T_DAVYDOVA, 13, "Ин. язык", engId, false, false, {LESNAYA});
    }

    for (int g = 0; g < GROUPS; g++) {
        add(g, -1, T_NUROV, 14, "Физическая культура", -1, false, false, {LESNAYA, KRIVOUSOVA});
    }

    for (int g = 0; g < GROUPS; g++) {
        add(g, -1, T_POTAPOVA, 17, "БЖД", -1, false, false, {LESNAYA, KRIVOUSOVA});
    }

    add(0, -1, T_SERYANINA, 12, "Экономика", -1, false, false, {LESNAYA, KRIVOUSOVA});
    add(1, -1, T_GARBUZOV, 12, "Экономика", -1, false, false, {LESNAYA, KRIVOUSOVA});

    int pmId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_GARBUZOV, 8, "МДК.01.01 теория", pmId, false, false, {LESNAYA, KRIVOUSOVA});
        add(g, bs, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, {LESNAYA, KRIVOUSOVA});
        add(g, bs + 1, T_GARBUZOV, 23, "МДК.01.01 ЛПЗ", pmId, true, false, {LESNAYA, KRIVOUSOVA});
        add(g, -1, T_GARBUZOV, 15, "МДК.01.01 КП", -1, false, false, {LESNAYA, KRIVOUSOVA});
    }

    int dbId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_SAMTSOVA, 36, "МДК.04.01 теория", dbId, false, false, {LESNAYA, KRIVOUSOVA});
        add(g, bs, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, {LESNAYA, KRIVOUSOVA});
        add(g, bs + 1, T_GOBOV, 35, "МДК.04.01 ЛПЗ", dbId, true, false, {LESNAYA, KRIVOUSOVA});

        add(g, bs, T_SAMTSOVA, 36, "УП.04", -1, false, true, {LESNAYA, KRIVOUSOVA});
        add(g, bs + 1, T_SAMTSOVA, 36, "УП.04", -1, false, true, {LESNAYA, KRIVOUSOVA});
    }

    int autoId = subj++;

    for (int g = 0; g < GROUPS; g++) {
        int bs = g * PARTS_PER_GROUP;

        add(g, -1, T_GARBUZOV, 16, "ВМДК.05.01 теория", autoId, false, false, {LESNAYA, KRIVOUSOVA});
        add(g, bs, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, {LESNAYA, KRIVOUSOVA});
        add(g, bs + 1, T_GOBOV, 19, "ВМДК.05.01 ЛПЗ", autoId, true, false, {LESNAYA, KRIVOUSOVA});

        add(g, bs, T_GOBOV, 18, "УП.05", -1, false, true, {LESNAYA, KRIVOUSOVA});
        add(g, bs + 1, T_GOBOV, 18, "УП.05", -1, false, true, {LESNAYA, KRIVOUSOVA});
    }

    return lessons;
}

}  // namespace timetable

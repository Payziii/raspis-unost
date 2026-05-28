// Скрипт импорта schedule.json -> data/timetable_data.json
// Запуск: node import_schedule.js
//
// Логика:
// - 1 пара = 2 часа (УП: 1 блок = 6 часов); округление вверх (Math.ceil)
// - Предметы с "1 подгруппа" / "2 подгруппа" в названии → subgroup 0 или 1 относительно группы
// - Предметы без подгрупп → subgroup = -1
// - "ЛПЗ ..." → is_lab = true
// - "УП." в начале → is_block = true
// - СБОРЫ (преподаватель == "СБОРЫ") → пропускаем (нет преподавателя в системе)
// - Нечётные total_slots у is_block → округляем до чётного (блоки должны быть кратны 2)
// - Преподаватели и группы нумеруются автоматически; одинаковые имена → один id
// - Группы 4 курса (цифра "4" после разделителя в числовой части) → unavailable с 2026-02-10 по конец

const fs = require('fs');
const path = require('path');

const LESNAYA = 0;
const KRIVOUSOVA = 1;
const END_DATE = '2026-06-19';
const FOURTH_YEAR_PRACTICE_FROM = '2026-02-25'; 
const PRACTICE_TEXT = 'Производственная практика';

const raw = JSON.parse(fs.readFileSync('schedule.json', 'utf8'));

// ----- helpers -----

function hoursToSlots(h, block = false) {
  return Math.ceil(h / (block ? 6 : 2));
}

function is4thYear(groupName) {
  // Примеры: ТЭО-4510, ТМ-4412, ИСП-4303п, МЦМ-308 (3й курс!), ТАКХС-Пф-2201
  // Берём числовой суффикс после последнего '-', первая цифра = курс
  // Для "Пф-XYYY" тоже: МЦМ-Пф-201 → 2й, ТАКХС-Пф-2201 → 2й, ТЭО-Пф-2501 → 2й
  const parts = groupName.split('-');
  const lastPart = parts[parts.length - 1].replace(/п$/, '');
  const firstDigit = lastPart.match(/^(\d)/);
  return firstDigit && firstDigit[1] === '4';
}

function cleanSubgroupName(name) {
  return name
    .replace(/\s*1\s*подгруппа\s*/gi, '')
    .replace(/\s*2\s*подгруппа\s*/gi, '')
    .trim();
}

function detectSubgroup(name) {
  if (/1\s*подгруппа/i.test(name)) return 1;
  if (/2\s*подгруппа/i.test(name)) return 2;
  return 0; // нет подгруппы
}

function isLab(name) {
  return /^ЛПЗ\s/i.test(name);
}

function isBlock(cleanedName) {
  return /^УП\./i.test(cleanedName);
}

// ----- собираем уникальные группы и преподавателей -----

const groupNameToId = {};
const teacherNameToId = {};
const groups = [];
const teachers = [];

// Сначала добавляем "СБОРЫ" как специального преподавателя (пропускаем его в уроках)
const SKIP_TEACHER = 'СБОРЫ';

for (const r of raw) {
  const gName = r['Группа'];
  if (!(gName in groupNameToId)) {
    const id = groups.length;
    groupNameToId[gName] = id;
    groups.push({ id, name: gName, parts: 2 });
  }

  const tName = r['Преподаватель'];
  if (tName === SKIP_TEACHER) continue;
  if (!(tName in teacherNameToId)) {
    const id = teachers.length;
    teacherNameToId[tName] = id;
    teachers.push({ id, name: tName });
  }
}

// ----- собираем уроки -----

const lessons = [];
let lessonId = 0;

// subject_id: группируем так, чтобы теория и ЛПЗ одного предмета имели один id.
// Стратегия:
// - Если Индекс непустой → ключ = groupName + ':' + index (теория)
// - Если Индекс пустой и имя начинается с "ЛПЗ " →
//     baseName = имя без "ЛПЗ " → ищем совпадение по prefix среди уже созданных ключей
//     (т.е. key где index является prefix baseName)
// - КП, УП → -1
// Реализуем двухпроходно: сначала собираем index→id, потом матчим ЛПЗ по baseName.

const subjectFamilyToId = {};   // key → id
const indexToId = {};           // groupName + ':' + index → id
const baseNameToId = {};        // groupName + ':' + baseName → id (регистронезависимо для поиска)
let nextSubjectId = 0;

function subjectId(groupName, index, cleanedName) {
  if (/^КП\s/i.test(cleanedName)) return -1;
  if (/^УП\./i.test(cleanedName)) return -1;
  if (/^СБОРЫ/i.test(cleanedName)) return -1;

  const baseName = cleanedName.replace(/^ЛПЗ\s+/i, '').trim();
  const hasIndex = index && index.trim();

  if (hasIndex) {
    const key = groupName + ':idx:' + index.trim();
    if (!(key in subjectFamilyToId)) {
      const id = nextSubjectId++;
      subjectFamilyToId[key] = id;
      // Регистрируем baseName → тот же id, чтобы ЛПЗ нашёл его
      const bnKey = groupName + ':bn:' + baseName.toLowerCase();
      if (!(bnKey in baseNameToId)) baseNameToId[bnKey] = id;
    }
    return subjectFamilyToId[key];
  } else {
    // Пустой индекс: пробуем найти по baseName совпадение с теорией
    const bnKey = groupName + ':bn:' + baseName.toLowerCase();
    if (bnKey in baseNameToId) {
      return baseNameToId[bnKey];
    }
    // Не нашли — создаём новый id
    if (!(bnKey in subjectFamilyToId)) {
      const id = nextSubjectId++;
      subjectFamilyToId[bnKey] = id;
      baseNameToId[bnKey] = id;
    }
    return subjectFamilyToId[bnKey];
  }
}

for (const r of raw) {
  const tName = r['Преподаватель'];
  if (tName === SKIP_TEACHER) continue; // пропускаем СБОРЫ

  const gName = r['Группа'];
  const groupId = groupNameToId[gName];
  const teacherId = teacherNameToId[tName];
  const rawName = r['Предмет'];
  const index = r['Индекс'] || '';
  const hours = r['Часов'];

  const sgKind = detectSubgroup(rawName);
  const cleanedName = cleanSubgroupName(rawName);
  const lab = isLab(cleanedName);
  const block = isBlock(cleanedName);

  let subgroup;
  if (sgKind === 0) {
    subgroup = -1;
  } else {
    // подгруппа 1 → id подгруппы = groupId * 2
    // подгруппа 2 → id подгруппы = groupId * 2 + 1
    subgroup = groupId * 2 + (sgKind - 1);
  }

  let totalSlots = hoursToSlots(hours, block);

  // Минимум 1 слот
  if (totalSlots < 1) totalSlots = 1;

  const sid = subjectId(gName, index, cleanedName);

  lessons.push({
    id: lessonId++,
    group: groupId,
    subgroup,
    teacher: teacherId,
    total_slots: totalSlots,
    name: cleanedName,
    subject_id: sid,
    is_lab: lab,
    is_block: block,
    allowed_campuses: [LESNAYA, KRIVOUSOVA],
  });
}

// ----- unavailable: Производственная практика для 4-го курса -----

const unavailable = [];
let unavailId = 0;

for (const group of groups) {
  if (is4thYear(group.name)) {
    unavailable.push({
      id: unavailId++,
      group: group.id,
      from: FOURTH_YEAR_PRACTICE_FROM,
      to: END_DATE,
      text: PRACTICE_TEXT,
    });
  }
}

// ----- итоговый JSON -----

const output = {
  settings: {
    start_date: '2026-01-12',
    end_date: END_DATE,
  },
  groups,
  teachers,
  unavailable,
  lessons,
};

const outPath = path.join('data', 'timetable_data.json');
fs.mkdirSync('data', { recursive: true });
fs.writeFileSync(outPath, JSON.stringify(output, null, 2), 'utf8');

console.log('Записано:', outPath);
console.log('  Групп:', groups.length);
console.log('  Преподавателей:', teachers.length);
console.log('  Уроков:', lessons.length);
console.log('  Unavailable (Произв. практика 4-й курс):', unavailable.length);
const fourthGroups = groups.filter(g => is4thYear(g.name));
console.log('  Группы 4-го курса:', fourthGroups.map(g => g.name).join(', '));

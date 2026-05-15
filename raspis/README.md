# Timetable Solver

Код из одного большого `main.cpp` разнесён по модулям.

## Структура

```text
include/
  config.h          # константы, имена групп/преподавателей
  types.h           # Date, Lesson, BlockInfo, Campus
  date_utils.h      # даты, недели, интервалы пар и УП
  format_utils.h    # строки, CSV, UTF-8 BOM, чтение значений CP-SAT
  model_utils.h     # вспомогательные ограничения и метрики
  diagnostics.h     # проверка входных данных и диагностика
  output_writers.h  # запись txt/csv расписаний
  lessons_data.h    # создание списка занятий
  scheduler.h       # запуск построения модели и решателя

src/
  *.cpp             # реализации модулей
```

## Сборка

Пример:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/or-tools
cmake --build build -j
./build/timetable_solver
```

Если OR-Tools подключён иначе, оставь свои текущие настройки сборки и просто добавь все `src/*.cpp` в проект, а `include/` — в include directories.

# Запуск в Visual Studio

Вариант 1, рекомендуется: открой `timetable_solver.sln`.

Вариант 2: `File -> Open -> Folder` и выбери папку с `CMakeLists.txt`.

Если открываешь `.sln/.vcxproj`, проект ожидает переменную окружения `ORTOOLS_ROOT`, например:

```bat
setx ORTOOLS_ROOT C:\ortools
```

Внутри `C:\ortools` должны быть папки `include` и `lib`.

Если OR-Tools уже был настроен в старом проекте, можно просто перенести его настройки:

- C/C++ -> General -> Additional Include Directories
- Linker -> General -> Additional Library Directories
- Linker -> Input -> Additional Dependencies

Главная причина сообщения `Сборка: успешно выполнено — 0, со сбоем — 0, в актуальном состоянии — 1` обычно такая: Visual Studio открыла не тот проект/target или исходники не добавлены в проект. В `.vcxproj` из этого архива все `.cpp` уже явно подключены.

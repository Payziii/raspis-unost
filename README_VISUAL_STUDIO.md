# Visual Studio

Открывай `timetable_solver.sln`.

Проект настроен под:

- Platform Toolset: `v145`
- Platform: `x64`
- C++ standard: C++20
- OR-Tools include: `C:\or-tools\include`
- OR-Tools lib: `C:\or-tools\lib`

Debug|x64 линкует:

- `ortools.lib`

Release|x64 линкует:

- `ortools_full.lib`
- `utf8_validity.lib`

Если у тебя в `C:\or-tools\lib` другие имена библиотек, поменяй их в:

`Project Properties -> Linker -> Input -> Additional Dependencies`.

Собирать нужно именно `x64`, не `Win32`.

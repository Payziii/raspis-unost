# Timetable Solver API

При запуске `.exe` теперь стартует локальное API:

```text
http://127.0.0.1:8080
```

Генерация расписания при запуске НЕ выполняется. Она запускается только POST-запросом.

## Куда сохраняются файлы

После успешной генерации файлы складываются сюда:

```text
output/latest/
  schedule_all.json
  raspisanie_all.txt
  raspisanie_groups.csv
  raspisanie_teachers.txt
  groups/
    group_0.json
    group_1.json
    raspisanie_ISP-3304.txt
    raspisanie_ISP-3305p.txt
```

`.gitignore` не добавлен и не изменялся.

## Endpoints

### Перегенерировать расписание

```http
POST /api/schedule/regenerate
```

Пример PowerShell:

```powershell
Invoke-RestMethod -Method Post http://127.0.0.1:8080/api/schedule/regenerate
```

### Получить всё расписание

```http
GET /api/schedule
```

Пример:

```powershell
Invoke-RestMethod http://127.0.0.1:8080/api/schedule
```

### Получить расписание одной группы

```http
GET /api/schedule/group/0
GET /api/schedule/group/1
GET /api/schedule/group/ISP-3304
GET /api/schedule/group/ISP-3305p
```

Пример:

```powershell
Invoke-RestMethod http://127.0.0.1:8080/api/schedule/group/0
```

Если расписание ещё не сгенерировано, GET вернёт 404 с сообщением вызвать `POST /api/schedule/regenerate`.

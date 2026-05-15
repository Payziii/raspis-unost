# Timetable API

При запуске `.exe` поднимается локальный API:

```text
http://127.0.0.1:8080
```

Генерация расписания при запуске **не выполняется**. Она запускается только запросом:

```http
POST /api/schedule/regenerate
```

Данные для генератора лежат в отдельном файле:

```text
data/timetable_data.json
```

Результаты генерации лежат отдельно:

```text
output/latest/
```

`.gitignore` я не добавлял и не менял.

## Расписание

```http
POST /api/schedule/regenerate
GET  /api/schedule
GET  /api/schedule/group/0
GET  /api/schedule/group/ИСП-3304
```

## Полный файл данных

```http
GET /api/data
PUT /api/data
```

`PUT /api/data` полностью заменяет `data/timetable_data.json` телом запроса.

## Настройки дат семестра

```http
GET   /api/settings
PATCH /api/settings
PUT   /api/settings
```

Пример:

```powershell
Invoke-RestMethod -Method Patch `
  -Uri http://127.0.0.1:8080/api/settings `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"start_date":"2026-01-12","end_date":"2026-06-19"}'
```

## Группы

```http
GET    /api/groups
POST   /api/groups
GET    /api/groups/{id}
PUT    /api/groups/{id}
PATCH  /api/groups/{id}
DELETE /api/groups/{id}
```

Пример добавления группы:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/groups `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"name":"ИСП-3306","parts":2}'
```

## Преподаватели

```http
GET    /api/teachers
POST   /api/teachers
GET    /api/teachers/{id}
PUT    /api/teachers/{id}
PATCH  /api/teachers/{id}
DELETE /api/teachers/{id}
```

Пример добавления преподавателя:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/teachers `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"name":"Иванов"}'
```

## Пары / занятия

```http
GET    /api/lessons
POST   /api/lessons
GET    /api/lessons/{id}
PUT    /api/lessons/{id}
PATCH  /api/lessons/{id}
DELETE /api/lessons/{id}
```

Формат занятия:

```json
{
  "group": 0,
  "subgroup": -1,
  "teacher": 7,
  "total_slots": 12,
  "name": "Экономика",
  "subject_id": -1,
  "is_lab": false,
  "is_block": false,
  "allowed_campuses": [0, 1]
}
```

Пояснения:

```text
subgroup = -1  вся группа
subgroup = group_id * 2      первая подгруппа
subgroup = group_id * 2 + 1  вторая подгруппа
campus 0 = Лесная
campus 1 = Кривоусова
is_block = true для УП-блоков
```

Пример добавления пары:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/lessons `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"group":0,"subgroup":-1,"teacher":7,"total_slots":10,"name":"Новый предмет","subject_id":-1,"is_lab":false,"is_block":false,"allowed_campuses":[0,1]}'
```

## Недоступность групп

```http
GET    /api/unavailable
POST   /api/unavailable
GET    /api/unavailable/{id}
PUT    /api/unavailable/{id}
PATCH  /api/unavailable/{id}
DELETE /api/unavailable/{id}
```

Пример:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8080/api/unavailable `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"group":1,"from":"2026-03-20","to":"2026-03-30"}'
```

После любых изменений данных нужно вызвать:

```powershell
Invoke-RestMethod -Method Post http://127.0.0.1:8080/api/schedule/regenerate
```

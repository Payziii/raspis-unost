<template>
  <div class="page">
    <div class="page-header">
      <h1 class="page-title">🛠 Конструктор расписания</h1>
      <div class="header-actions">
        <button class="btn btn-secondary btn-sm" :disabled="cstore.saving" @click="onCopyFromAuto">
          <span v-if="cstore.saving" class="spinner spinner-sm"/>📥 Скопировать из автогенерации
        </button>
        <button class="btn btn-ghost btn-sm" :disabled="cstore.saving" @click="onClear">
          🗑 Очистить
        </button>
        <button class="btn btn-primary btn-sm" :disabled="cstore.saving || !cstore.dirty" @click="onSave">
          <span v-if="cstore.saving" class="spinner spinner-sm"/>💾 Сохранить
          <span v-if="cstore.dirty" class="badge badge-warning" style="margin-left:6px">●</span>
        </button>
      </div>
    </div>

    <div v-if="cstore.loading" class="center-block">
      <span class="spinner spinner-lg" style="color: var(--accent)" />
      <p style="margin-top:16px; color: var(--text-muted)">Загрузка ручного расписания…</p>
    </div>

    <div v-else-if="cstore.error" class="empty-state">
      <span class="icon">⚠️</span>
      <h3>{{ cstore.error }}</h3>
    </div>

    <div v-else-if="groups.length === 0" class="empty-state">
      <span class="icon">📋</span>
      <h3>Ручное расписание пусто</h3>
      <p>Скопируй из автогенерации, чтобы начать редактирование, или построй с нуля кликами по ячейкам.</p>
      <div style="margin-top:20px; display:flex; gap:10px; justify-content:center">
        <button class="btn btn-primary" @click="onCopyFromAuto">📥 Скопировать из автогенерации</button>
        <button class="btn btn-secondary" @click="initScratch">✏️ Начать с нуля</button>
      </div>
    </div>

    <template v-else>
      <!-- Year tabs -->
      <div class="year-tabs">
        <button
          v-for="tab in yearTabs"
          :key="tab.value"
          class="year-tab"
          :class="{ active: selectedYear === tab.value }"
          @click="selectedYear = tab.value"
        >
          {{ tab.label }}
          <span v-if="tab.count > 0" class="tab-count">{{ tab.count }}</span>
        </button>
      </div>

      <!-- Week nav -->
      <div class="week-nav">
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex <= 0" @click="weekIndex--">‹ Пред.</button>
        <span class="week-label">
          <strong>Неделя {{ weekIndex + 1 }}</strong>
          <span class="week-dates">{{ weekLabel }}</span>
        </span>
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex >= sortedWeeks.length - 1" @click="weekIndex++">След. ›</button>
      </div>

      <div v-if="filteredGroups.length === 0" class="empty-state">
        <span class="icon">🎓</span>
        <h3>Нет групп для выбранного курса</h3>
      </div>

      <div v-else-if="weekDates.length === 0" class="empty-state">
        <span class="icon">📅</span>
        <h3>Нет учебных дней на этой неделе</h3>
      </div>

      <div v-else class="sched-scroll">
        <table class="sched-table">
          <thead>
            <tr>
              <th rowspan="2" class="th-slot sticky-col">🕐<br/>Пара</th>
              <th
                v-for="dateStr in weekDates"
                :key="dateStr"
                :colspan="filteredGroups.length"
                class="th-day"
              >
                <span class="day-name">{{ getDayWeekday(dateStr).toUpperCase() }}</span>
                <span class="day-date-sub">{{ formatDateShort(dateStr) }}</span>
              </th>
            </tr>
            <tr>
              <template v-for="dateStr in weekDates" :key="dateStr">
                <th
                  v-for="(g, gi) in filteredGroups"
                  :key="`${dateStr}-${g.group_index}`"
                  class="th-group"
                  :class="{ 'day-separator': gi === 0 }"
                >
                  {{ g.group_name }}
                </th>
              </template>
            </tr>
          </thead>
          <tbody>
            <tr v-for="slotNum in 7" :key="slotNum" class="slot-row">
              <td class="slot-label sticky-col">
                <span class="slot-num">{{ slotNum }}</span>
                <span class="slot-time">{{ getSlotTimeForSlot(slotNum) }}</span>
              </td>
              <template v-for="(dateStr, di) in weekDates" :key="dateStr">
                <td
                  v-for="(g, gi) in filteredGroups"
                  :key="`${dateStr}-${g.group_index}`"
                  class="slot-cell editable"
                  :class="{
                    'day-separator': gi === 0,
                    'cell-lab': cellIsLab(g.group_index, dateStr, slotNum),
                    'cell-practice': cellIsBlock(g.group_index, dateStr, slotNum),
                  }"
                  @click="openPicker(g, dateStr, slotNum)"
                >
                  <div class="cell-inner" v-if="cellText(g.group_index, dateStr, slotNum)">
                    <span class="cell-subject">{{ cellText(g.group_index, dateStr, slotNum) }}</span>
                  </div>
                  <span v-else class="cell-empty">+</span>
                </td>
              </template>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- Прогресс уроков -->
      <section class="progress-card">
        <div class="card-title">📊 Прогресс расстановки уроков</div>
        <div class="progress-grid">
          <div v-for="lp in lessonProgress" :key="lp.id" class="progress-item" :class="{
            'progress-complete': lp.placed >= lp.total_slots,
            'progress-over': lp.placed > lp.total_slots,
          }">
            <div class="progress-name">
              <span class="progress-group">{{ groupNameById(lp.group) }}</span>
              {{ lp.name }}
            </div>
            <div class="progress-counts">{{ lp.placed }} / {{ lp.total_slots }}</div>
          </div>
        </div>
      </section>
    </template>

    <!-- Lesson picker modal -->
    <Modal v-model="pickerOpen" :title="pickerTitle">
      <div v-if="pickerCell" class="picker-body">
        <div v-if="pickerCell.lessons.length" class="picker-current">
          <div style="font-weight:600;margin-bottom:8px">Сейчас в ячейке:</div>
          <div v-for="L in pickerCell.lessons" :key="L.id" class="picker-current-item">
            <div>{{ L.name }} <span class="muted">— {{ subgroupLabel(L.subgroup) }}, препод #{{ L.teacher_id }}</span></div>
            <button class="btn btn-ghost btn-sm" style="color:var(--error)" @click="removeLessonFromCell(L.id)">Удалить</button>
          </div>
          <hr style="margin:14px 0;border-color:var(--border)"/>
        </div>

        <div style="font-weight:600;margin-bottom:8px">Доступные уроки группы {{ pickerCell.groupName }}:</div>
        <div v-if="pickerOptions.length === 0" style="color:var(--text-muted);padding:14px 0">
          Все уроки этой группы уже расставлены целиком.
        </div>
        <div v-else class="picker-list">
          <div
            v-for="opt in pickerOptions" :key="opt.id"
            class="picker-option"
            :class="{ 'picker-warn': opt.conflict }"
            @click="addLessonToCell(opt)"
          >
            <div class="picker-option-main">
              <span class="picker-option-name">{{ opt.name }}</span>
              <span class="picker-option-meta">
                {{ subgroupLabel(opt.subgroup) }} ·
                препод #{{ opt.teacher }} ·
                {{ opt.placed }}/{{ opt.total_slots }}
                <span v-if="opt.is_block"> · УП</span>
                <span v-if="opt.is_lab"> · ЛПЗ</span>
              </span>
            </div>
            <div v-if="opt.conflict" class="picker-option-warn">⚠ {{ opt.conflict }}</div>
          </div>
        </div>
      </div>
      <template #footer>
        <button class="btn btn-ghost" @click="pickerOpen = false">Закрыть</button>
      </template>
    </Modal>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import Modal from '../components/Modal.vue'
import { useConstructorStore } from '../stores/constructor.js'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'

const cstore = useConstructorStore()
const data = useDataStore()
const toast = useToast()

const selectedYear = ref(0)
const weekIndex = ref(0)

onMounted(async () => {
  await Promise.all([
    cstore.load(),
    data.loadGroups(),
    data.loadLessons(),
    data.loadTeachers(),
  ])
})

// ── Computed shape ──
const groups = computed(() => cstore.manualData?.groups || [])

function parseDMY(str) {
  const [d, m, y] = str.split('.')
  return new Date(+y, +m - 1, +d)
}
function getMonday(date) {
  const d = new Date(date)
  const day = d.getDay()
  d.setDate(d.getDate() - (day === 0 ? 6 : day - 1))
  d.setHours(0, 0, 0, 0)
  return d
}
function fmtDate(date) {
  return `${String(date.getDate()).padStart(2, '0')}.${String(date.getMonth() + 1).padStart(2, '0')}.${date.getFullYear()}`
}
function formatDateShort(dateStr) {
  const [d, m] = dateStr.split('.')
  return `${d}.${m}`
}
function compareDMY(a, b) { return parseDMY(a) - parseDMY(b) }
function detectCourseYear(name) {
  const m4 = name.match(/[^\d]2(\d)\d{2}/)
  if (m4) {
    const d = parseInt(m4[1])
    const enrollYear = 2020 + d
    const y = 2025 - enrollYear + 1
    if (y >= 1 && y <= 4) return y
  }
  const m1 = name.match(/-(\d)/)
  if (m1) {
    const d = parseInt(m1[1])
    if (d >= 1 && d <= 4) return d
  }
  return null
}

const slotLookup = computed(() => {
  const map = {}
  for (const g of groups.value) {
    map[g.group_index] = {}
    for (const day of g.days || []) {
      map[g.group_index][day.date] = { weekday: day.weekday, date_iso: day.date_iso, slots: {} }
      for (const s of day.slots || []) {
        map[g.group_index][day.date].slots[s.slot] = { time: s.time, text: s.text, lessons: s.lessons || [] }
      }
    }
  }
  return map
})

const sortedWeeks = computed(() => {
  const set = new Set()
  for (const g of groups.value) {
    for (const d of g.days || []) {
      const mon = getMonday(parseDMY(d.date))
      set.add(mon.toISOString())
    }
  }
  return [...set].sort().map(iso => {
    const mon = new Date(iso)
    const sun = new Date(mon); sun.setDate(sun.getDate() + 6)
    return { iso, monStr: fmtDate(mon), sunStr: fmtDate(sun) }
  })
})

const weekLabel = computed(() => {
  const w = sortedWeeks.value[weekIndex.value]
  return w ? `${w.monStr} — ${w.sunStr}` : ''
})

const weekDates = computed(() => {
  if (!sortedWeeks.value.length) return []
  const w = sortedWeeks.value[weekIndex.value]
  if (!w) return []
  const set = new Set()
  for (const g of groups.value) {
    for (const d of g.days || []) {
      if (getMonday(parseDMY(d.date)).toISOString() === w.iso) set.add(d.date)
    }
  }
  return [...set].sort(compareDMY)
})

function getDayWeekday(dateStr) {
  for (const g of groups.value) {
    const lk = slotLookup.value[g.group_index]?.[dateStr]
    if (lk) return lk.weekday
  }
  return ''
}

const yearTabs = computed(() => {
  const counts = [0, 0, 0, 0]
  for (const g of groups.value) {
    const y = detectCourseYear(g.group_name)
    if (y >= 1 && y <= 4) counts[y - 1]++
  }
  return [
    { value: 0, label: 'Все группы', count: groups.value.length },
    { value: 1, label: '1 курс', count: counts[0] },
    { value: 2, label: '2 курс', count: counts[1] },
    { value: 3, label: '3 курс', count: counts[2] },
    { value: 4, label: '4 курс', count: counts[3] },
  ]
})

const filteredGroups = computed(() => {
  if (selectedYear.value === 0) return groups.value
  return groups.value.filter(g => detectCourseYear(g.group_name) === selectedYear.value)
})

function getSlotTimeForSlot(slotNum) {
  for (const dateStr of weekDates.value) {
    for (const g of groups.value) {
      const s = slotLookup.value[g.group_index]?.[dateStr]?.slots[slotNum]
      if (s?.time) return s.time
    }
  }
  return ''
}

function cellLessons(gi, dateStr, slotNum) {
  return slotLookup.value[gi]?.[dateStr]?.slots[slotNum]?.lessons || []
}
function cellText(gi, dateStr, slotNum) {
  const arr = cellLessons(gi, dateStr, slotNum)
  if (!arr.length) return ''
  return arr.map(L => L.name).join(' | ')
}
function cellIsLab(gi, dateStr, slotNum) {
  return cellLessons(gi, dateStr, slotNum).some(L => L.is_lab)
}
function cellIsBlock(gi, dateStr, slotNum) {
  return cellLessons(gi, dateStr, slotNum).some(L => L.is_block)
}

function subgroupLabel(sub) {
  if (sub === -1 || sub == null) return 'вся группа'
  return `подгруппа ${(sub % 2) + 1}`
}

function groupNameById(gi) {
  return groups.value.find(g => g.group_index === gi)?.group_name || `Группа ${gi}`
}

// ── Lesson progress ──
const placementByLesson = computed(() => {
  const acc = {}
  for (const g of groups.value) {
    for (const d of g.days || []) {
      for (const s of d.slots || []) {
        for (const L of (s.lessons || [])) {
          acc[L.id] = (acc[L.id] || 0) + 1
        }
      }
    }
  }
  return acc
})

const lessonProgress = computed(() => {
  const items = []
  for (const lesson of data.lessons) {
    items.push({
      id: lesson.id,
      group: lesson.group,
      name: lesson.name,
      total_slots: lesson.total_slots,
      placed: placementByLesson.value[lesson.id] || 0,
      subgroup: lesson.subgroup,
    })
  }
  items.sort((a, b) => a.group - b.group || a.id - b.id)
  return items
})

// ── Picker modal ──
const pickerOpen = ref(false)
const pickerCell = ref(null) // { groupName, group_index, dateStr, dateIso, slotNum, time, weekday, lessons }
const pickerTitle = computed(() =>
  pickerCell.value
    ? `${pickerCell.value.groupName} · ${pickerCell.value.dateStr} · Пара ${pickerCell.value.slotNum}`
    : 'Выбор урока'
)

function openPicker(group, dateStr, slotNum) {
  const lk = slotLookup.value[group.group_index]?.[dateStr]
  const sl = lk?.slots[slotNum]
  pickerCell.value = {
    groupName: group.group_name,
    group_index: group.group_index,
    dateStr,
    dateIso: lk?.date_iso || isoFromDmy(dateStr),
    slotNum,
    time: sl?.time || '',
    weekday: lk?.weekday || '',
    lessons: sl?.lessons ? sl.lessons.slice() : [],
  }
  pickerOpen.value = true
}

function isoFromDmy(dateStr) {
  const [d, m, y] = dateStr.split('.')
  return `${y}-${m}-${d}`
}

const pickerOptions = computed(() => {
  if (!pickerCell.value) return []
  const sameGroup = data.lessons.filter(l => l.group === pickerCell.value.group_index)
  return sameGroup.map(l => {
    const placed = placementByLesson.value[l.id] || 0
    let conflict = ''
    // Конфликт: тот же урок уже в этом слоте
    if (pickerCell.value.lessons.some(x => x.id === l.id)) conflict = 'уже в этой ячейке'
    // Конфликт: эта же подгруппа уже занята
    else if (l.subgroup !== -1) {
      const occupiedSub = pickerCell.value.lessons.some(x => x.subgroup === l.subgroup || x.subgroup === -1)
      if (occupiedSub) conflict = 'подгруппа уже занята'
    } else {
      // -1 (вся группа) не должна сосуществовать ни с чем
      if (pickerCell.value.lessons.length) conflict = 'ячейка занята подгруппой'
    }
    // Конфликт: преподаватель занят в этот слот у другой группы
    if (!conflict) {
      for (const g of groups.value) {
        if (g.group_index === pickerCell.value.group_index) continue
        const arr = slotLookup.value[g.group_index]?.[pickerCell.value.dateStr]?.slots[pickerCell.value.slotNum]?.lessons || []
        if (arr.some(x => x.teacher_id === l.teacher)) {
          conflict = `препод #${l.teacher} занят в ${g.group_name}`
          break
        }
      }
    }
    if (!conflict && placed >= l.total_slots) {
      conflict = 'нагрузка уже выполнена'
    }
    return {
      id: l.id, name: l.name, subgroup: l.subgroup, teacher: l.teacher,
      is_lab: l.is_lab, is_block: l.is_block, total_slots: l.total_slots,
      placed,
      conflict,
    }
  })
})

function addLessonToCell(opt) {
  if (opt.conflict) {
    toast.warning(opt.conflict)
    return
  }
  const cell = pickerCell.value
  const group = cstore.ensureGroup(cell.group_index, cell.groupName)
  const day = cstore.findOrCreateDay(group, cell.dateIso, cell.dateStr, cell.weekday)
  const slot = cstore.findOrCreateSlot(day, cell.slotNum, cell.time)
  const newLessons = [...(slot.lessons || []), {
    id: opt.id, name: opt.name, teacher_id: opt.teacher,
    subgroup: opt.subgroup, is_lab: opt.is_lab, is_block: opt.is_block,
  }]
  cstore.setSlotLessons(slot, newLessons)
  // Sync picker view
  pickerCell.value.lessons = newLessons.slice()
  toast.success('Добавлено')
}

function removeLessonFromCell(lessonId) {
  const cell = pickerCell.value
  if (!cell) return
  const group = cstore.findGroup(cell.group_index)
  if (!group) return
  const day = group.days.find(d => d.date_iso === cell.dateIso)
  if (!day) return
  const slot = day.slots.find(s => s.slot === cell.slotNum)
  if (!slot) return
  const newLessons = (slot.lessons || []).filter(L => L.id !== lessonId)
  cstore.setSlotLessons(slot, newLessons)
  pickerCell.value.lessons = newLessons.slice()
  toast.success('Удалено')
}

// ── Header actions ──

async function onCopyFromAuto() {
  const r = await cstore.copyFromAuto()
  if (r.ok) toast.success('Скопировано из автогенерации')
  else toast.error(r.data?.message || 'Ошибка копирования')
}

async function onClear() {
  if (!confirm('Очистить ручное расписание полностью? Действие необратимо.')) return
  const r = await cstore.clear()
  if (r.ok) toast.success('Очищено')
  else toast.error(r.data?.message || 'Ошибка')
}

async function onSave() {
  const r = await cstore.save()
  if (r.ok) toast.success('Ручное расписание сохранено')
  else toast.error(r.data?.message || 'Ошибка сохранения')
}

function initScratch() {
  cstore.manualData = { groups: [] }
  cstore.dirty = true
  toast.info('Начни кликать по ячейкам, чтобы добавить занятия')
}
</script>

<style scoped>
.header-actions { display: flex; gap: 8px; flex-wrap: wrap; }

.year-tabs { display: flex; gap: 6px; margin-bottom: 16px; flex-wrap: wrap; }
.year-tab {
  display: flex; align-items: center; gap: 6px;
  padding: 7px 14px; border-radius: var(--radius-sm);
  border: 1px solid var(--border); background: var(--bg-secondary);
  color: var(--text-secondary); font-size: 13px; font-weight: 500;
  cursor: pointer; transition: all var(--transition);
}
.year-tab:hover { border-color: var(--accent); color: var(--text-primary); }
.year-tab.active { background: var(--accent-light); border-color: var(--accent); color: var(--accent); }
.tab-count { background: var(--bg-tertiary); border-radius: 10px; padding: 0 6px; font-size: 11px; }

.week-nav {
  display: flex; align-items: center; justify-content: space-between; gap: 12px;
  margin-bottom: 16px; background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); padding: 12px 16px;
}
.week-label { display: flex; flex-direction: column; align-items: center; gap: 2px; }
.week-dates { font-size: 12px; color: var(--text-muted); }

.sched-scroll {
  overflow-x: auto; border-radius: var(--radius);
  border: 1px solid var(--border-strong); box-shadow: 0 4px 24px rgba(0, 0, 0, 0.35);
  margin-bottom: 20px;
}
.sched-table { border-collapse: collapse; font-size: 13px; width: 100%; }
.sticky-col { position: sticky; left: 0; z-index: 2; }
.th-slot {
  background: var(--bg-secondary); border-right: 2px solid var(--border-strong);
  border-bottom: 2px solid var(--border-strong); z-index: 4 !important;
  text-align: center; width: 84px; padding: 10px 8px;
}
.th-day {
  background: linear-gradient(135deg, #4f46e5 0%, #6366f1 60%, #818cf8 100%);
  color: #fff; text-align: center; padding: 10px 14px;
  border-left: 2px solid rgba(255,255,255,0.18);
  border-bottom: 1px solid rgba(255,255,255,0.18); white-space: nowrap;
}
.day-name { display: block; font-size: 12px; font-weight: 800; }
.day-date-sub { display: block; font-size: 10px; font-weight: 400; opacity: 0.72; }
.th-group {
  background: #1a2540; color: var(--text-secondary); text-align: center;
  font-size: 10px; font-weight: 700; text-transform: uppercase;
  padding: 6px 10px; white-space: nowrap;
  border-bottom: 2px solid var(--border-strong); min-width: 140px;
}
.th-group.day-separator { border-left: 2px solid var(--border-strong); }
.slot-label {
  background: var(--bg-secondary); padding: 10px 8px;
  display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px;
  border-right: 2px solid var(--border-strong);
  border-top: 1px solid var(--border); min-height: 56px;
}
.slot-num { font-size: 20px; font-weight: 800; color: var(--text-primary); }
.slot-time { font-size: 9px; color: var(--text-muted); }

.slot-cell {
  padding: 8px 10px; vertical-align: top;
  border-top: 1px solid var(--border); transition: background var(--transition);
}
.slot-cell.editable { cursor: pointer; }
.slot-cell.editable:hover { background: rgba(99, 102, 241, 0.12); }
.slot-cell.day-separator { border-left: 2px solid var(--border-strong); }

.cell-lab { background: rgba(16, 185, 129, 0.07); }
.cell-practice { background: rgba(245, 158, 11, 0.07); }

.cell-inner { display: flex; flex-direction: column; gap: 4px; }
.cell-subject { font-size: 12px; font-weight: 600; color: var(--text-primary); line-height: 1.35; }
.cell-empty {
  color: var(--text-muted); font-size: 18px; display: flex;
  align-items: center; justify-content: center; min-height: 32px; opacity: 0.25;
}

.progress-card {
  background: var(--bg-secondary); border: 1px solid var(--border);
  border-radius: var(--radius); padding: 16px; margin-bottom: 20px;
}
.card-title { font-size: 14px; font-weight: 700; margin-bottom: 10px; color: var(--text-primary); }
.progress-grid {
  display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 6px;
}
.progress-item {
  display: flex; justify-content: space-between; gap: 8px; padding: 6px 10px;
  background: var(--bg-tertiary); border-radius: var(--radius-sm); font-size: 12px;
}
.progress-name { color: var(--text-secondary); }
.progress-group { color: var(--accent); font-weight: 600; margin-right: 6px; }
.progress-counts { color: var(--text-primary); font-weight: 600; font-variant-numeric: tabular-nums; }
.progress-complete .progress-counts { color: #10b981; }
.progress-over .progress-counts { color: #ef4444; }

.picker-body { max-height: 60vh; overflow-y: auto; }
.picker-current-item {
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  padding: 8px 10px; background: var(--bg-tertiary); border-radius: var(--radius-sm);
  margin-bottom: 6px;
}
.picker-list { display: flex; flex-direction: column; gap: 4px; }
.picker-option {
  padding: 8px 10px; background: var(--bg-tertiary); border-radius: var(--radius-sm);
  cursor: pointer; transition: background var(--transition);
  border: 1px solid transparent;
}
.picker-option:hover { background: var(--accent-light); border-color: var(--accent); }
.picker-option.picker-warn { opacity: 0.7; }
.picker-option.picker-warn:hover { background: rgba(239, 68, 68, 0.1); border-color: var(--error); }
.picker-option-main { display: flex; justify-content: space-between; gap: 10px; align-items: center; }
.picker-option-name { font-weight: 600; font-size: 13px; }
.picker-option-meta { font-size: 11px; color: var(--text-muted); }
.picker-option-warn { font-size: 11px; color: var(--error); margin-top: 4px; }
.muted { color: var(--text-muted); font-size: 11px; }

.center-block { display: flex; flex-direction: column; align-items: center; padding: 80px 20px; }
</style>

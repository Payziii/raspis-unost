<template>
  <div class="page">
    <!-- Header -->
    <div class="page-header">
      <h1 class="page-title">📅 Расписание</h1>
      <button class="btn btn-primary" :disabled="store.generating" @click="onRegenerate">
        <span v-if="store.generating" class="spinner spinner-sm" />
        <span>{{ store.generating ? 'Генерируется…' : 'Сгенерировать' }}</span>
      </button>
    </div>

    <!-- Course year tabs -->
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

    <!-- Loading -->
    <div v-if="store.loading" class="center-block">
      <span class="spinner spinner-lg" style="color: var(--accent)" />
      <p style="margin-top:16px; color: var(--text-muted)">Загрузка расписания…</p>
    </div>

    <!-- Error -->
    <div v-else-if="store.error" class="empty-state">
      <span class="icon">⚠️</span>
      <h3>{{ store.error }}</h3>
      <p>Убедитесь, что сервер запущен на http://127.0.0.1:8080</p>
      <div style="margin-top:20px; display:flex; gap:10px; justify-content:center">
        <button class="btn btn-primary" @click="store.fetchSchedule()">Повторить</button>
        <button class="btn btn-secondary" :disabled="store.generating" @click="onRegenerate">
          {{ store.generating ? 'Генерируется…' : 'Сгенерировать расписание' }}
        </button>
      </div>
    </div>

    <!-- No schedule yet -->
    <div v-else-if="!store.scheduleData || store.groups.length === 0" class="empty-state">
      <span class="icon">📋</span>
      <h3>Расписание не сгенерировано</h3>
      <p>Нажмите кнопку «Сгенерировать», чтобы создать расписание</p>
      <button class="btn btn-primary btn-lg" style="margin-top:20px" :disabled="store.generating" @click="onRegenerate">
        <span v-if="store.generating" class="spinner spinner-sm" />
        {{ store.generating ? 'Генерируется…' : '🚀 Сгенерировать расписание' }}
      </button>
    </div>

    <!-- Schedule table -->
    <template v-else>
      <!-- Week navigation -->
      <div class="week-nav">
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex <= 0" @click="weekIndex--">‹ Пред.</button>
        <span class="week-label">
          <strong>Неделя {{ weekIndex + 1 }}</strong>
          <span class="week-dates">{{ weekLabel }}</span>
        </span>
        <button class="btn btn-ghost btn-sm" :disabled="weekIndex >= sortedWeeks.length - 1" @click="weekIndex++">След. ›</button>
      </div>

      <!-- No groups for selected year -->
      <div v-if="filteredGroups.length === 0" class="empty-state">
        <span class="icon">🎓</span>
        <h3>Нет групп для {{ yearTabs.find(t => t.value === selectedYear)?.label }}</h3>
        <p>Попробуйте другой курс или выберите «Все группы»</p>
      </div>

      <!-- No data for week -->
      <div v-else-if="weekDates.length === 0" class="empty-state">
        <span class="icon">📅</span>
        <h3>Нет занятий на этой неделе</h3>
      </div>

      <div v-else class="sched-scroll">
        <table class="sched-table">
          <thead>
            <!-- Row 1: day names -->
            <tr>
              <th rowspan="2" class="th-slot sticky-col">
                <span class="slot-header-icon">🕐</span>
                <span class="slot-header-text">Пара</span>
              </th>
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
            <!-- Row 2: group names -->
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
            <tr v-for="slotNum in maxSlots" :key="slotNum" class="slot-row">
              <td class="slot-label sticky-col">
                <span class="slot-num">{{ slotNum }}</span>
                <span class="slot-time">{{ getSlotTimeForSlot(slotNum) }}</span>
              </td>
              <template v-for="(dateStr, di) in weekDates" :key="dateStr">
                <td
                  v-for="(g, gi) in filteredGroups"
                  :key="`${dateStr}-${g.group_index}`"
                  class="slot-cell"
                  :class="[
                    getCellClass(g.group_index, dateStr, slotNum),
                    { 'day-separator': gi === 0 }
                  ]"
                >
                  <div class="cell-inner" v-if="getCellText(g.group_index, dateStr, slotNum)">
                    <span class="cell-subject">{{ parseSubject(getCellText(g.group_index, dateStr, slotNum)) }}</span>
                    <span
                      v-for="(detail, i) in parseDetails(getCellText(g.group_index, dateStr, slotNum))"
                      :key="i"
                      class="cell-detail"
                    >{{ detail }}</span>
                  </div>
                  <span v-else class="cell-empty">—</span>
                </td>
              </template>
            </tr>
          </tbody>
        </table>
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useScheduleStore } from '../stores/schedule.js'
import { useToast } from '../composables/useToast.js'

const store = useScheduleStore()
const toast = useToast()

const selectedYear = ref(0)
const weekIndex = ref(0)

onMounted(async () => {
  await store.fetchSchedule()
  jumpToCurrentWeek()
})

// ── Helpers ──────────────────────────────────────────────────────────────

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

function compareDMY(a, b) {
  return parseDMY(a) - parseDMY(b)
}

function detectCourseYear(name) {
  const m4 = name.match(/[^\d]2(\d)\d{2}/)
  if (m4) {
    const d = parseInt(m4[1])
    const enrollYear = 2020 + d
    const year = 2025 - enrollYear + 1
    if (year >= 1 && year <= 4) return year
  }
  const m1 = name.match(/-(\d)/)
  if (m1) {
    const d = parseInt(m1[1])
    if (d >= 1 && d <= 4) return d
  }
  return null
}

// ── Slot lookup ──────────────────────────────────────────────────────────

const slotLookup = computed(() => {
  const map = {}
  for (const g of store.groups) {
    map[g.group_index] = {}
    for (const day of g.days) {
      map[g.group_index][day.date] = { weekday: day.weekday, slots: {} }
      for (const s of day.slots) {
        map[g.group_index][day.date].slots[s.slot] = { time: s.time, text: s.text }
      }
    }
  }
  return map
})

// ── Weeks ────────────────────────────────────────────────────────────────

const sortedWeeks = computed(() => {
  if (!store.scheduleData) return []
  const weeksSet = new Set()
  for (const g of store.groups) {
    for (const d of g.days) {
      const mon = getMonday(parseDMY(d.date))
      weeksSet.add(mon.toISOString())
    }
  }
  return [...weeksSet].sort().map(iso => {
    const mon = new Date(iso)
    const sun = new Date(mon); sun.setDate(sun.getDate() + 6)
    return { iso, mon, sun, monStr: fmtDate(mon), sunStr: fmtDate(sun) }
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
  for (const g of store.groups) {
    for (const d of g.days) {
      const parsed = parseDMY(d.date)
      const mon = getMonday(parsed)
      if (mon.toISOString() === w.iso) set.add(d.date)
    }
  }
  return [...set].sort(compareDMY)
})

function getDayWeekday(dateStr) {
  for (const g of store.groups) {
    const lookup = slotLookup.value[g.group_index]?.[dateStr]
    if (lookup) return lookup.weekday
  }
  return ''
}

function jumpToCurrentWeek() {
  if (!sortedWeeks.value.length) return
  const today = new Date()
  const mon = getMonday(today).toISOString()
  const idx = sortedWeeks.value.findIndex(w => w.iso === mon)
  weekIndex.value = idx >= 0 ? idx : 0
}

watch(() => store.scheduleData, jumpToCurrentWeek)

// ── Max slots ────────────────────────────────────────────────────────────

const maxSlots = computed(() => {
  let max = 0
  for (const g of store.groups) {
    for (const d of g.days) max = Math.max(max, d.slots.length)
  }
  return max || 7
})

// ── Year tabs ────────────────────────────────────────────────────────────

const yearTabs = computed(() => {
  const counts = [0, 0, 0, 0]
  for (const g of store.groups) {
    const y = detectCourseYear(g.group_name)
    if (y && y >= 1 && y <= 4) counts[y - 1]++
  }
  const total = store.groups.length
  return [
    { value: 0, label: 'Все группы', count: total },
    { value: 1, label: '1 курс', count: counts[0] },
    { value: 2, label: '2 курс', count: counts[1] },
    { value: 3, label: '3 курс', count: counts[2] },
    { value: 4, label: '4 курс', count: counts[3] },
  ]
})

const filteredGroups = computed(() => {
  if (selectedYear.value === 0) return store.groups
  return store.groups.filter(g => detectCourseYear(g.group_name) === selectedYear.value)
})

// ── Cell helpers ─────────────────────────────────────────────────────────

function getSlotTimeForSlot(slotNum) {
  for (const dateStr of weekDates.value) {
    for (const g of store.groups) {
      const s = slotLookup.value[g.group_index]?.[dateStr]?.slots[slotNum]
      if (s?.time) return s.time
    }
  }
  return ''
}

function getCellText(groupIndex, dateStr, slotNum) {
  const s = slotLookup.value[groupIndex]?.[dateStr]?.slots[slotNum]
  if (!s || s.text === '-') return null
  return s.text
}

function getCellClass(groupIndex, dateStr, slotNum) {
  const text = getCellText(groupIndex, dateStr, slotNum)
  if (!text) return 'cell-empty-type'
  const lo = text.toLowerCase()
  if (lo.includes('лпз') || lo.includes('лаб')) return 'cell-lab'
  if (lo.includes('уп-') || lo.includes('уп ') || lo.includes('практик')) return 'cell-practice'
  return 'cell-normal'
}

function parseSubject(text) {
  if (!text) return ''
  const idx = text.indexOf(' — ')
  return idx >= 0 ? text.slice(0, idx) : text
}

function parseDetails(text) {
  if (!text) return []
  const idx = text.indexOf(' — ')
  if (idx < 0) return []
  return text.slice(idx + 3).split(', ').filter(Boolean)
}

// ── Regenerate ───────────────────────────────────────────────────────────

async function onRegenerate() {
  const r = await store.regenerate()
  if (r.ok) {
    toast.success('Расписание успешно сгенерировано!')
    jumpToCurrentWeek()
  } else {
    toast.error(r.message || 'Ошибка генерации расписания')
  }
}
</script>

<style scoped>
/* ── Year tabs ─────────────────────────────────────────────────────────── */
.year-tabs {
  display: flex;
  gap: 6px;
  margin-bottom: 20px;
  flex-wrap: wrap;
}
.year-tab {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 7px 14px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: var(--bg-secondary);
  color: var(--text-secondary);
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--transition);
}
.year-tab:hover { border-color: var(--accent); color: var(--text-primary); }
.year-tab.active {
  background: var(--accent-light);
  border-color: var(--accent);
  color: var(--accent);
}
.tab-count {
  background: var(--bg-tertiary);
  border-radius: 10px;
  padding: 0 6px;
  font-size: 11px;
}

/* ── Week nav ──────────────────────────────────────────────────────────── */
.week-nav {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 20px;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 12px 16px;
}
.week-label {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}
.week-label strong { font-size: 14px; }
.week-dates { font-size: 12px; color: var(--text-muted); }

/* ── Table scroll wrapper ──────────────────────────────────────────────── */
.sched-scroll {
  overflow-x: auto;
  -webkit-overflow-scrolling: touch;
  border-radius: var(--radius);
  border: 1px solid var(--border-strong);
  box-shadow: 0 4px 24px rgba(0, 0, 0, 0.35);
}

/* ── Table base ─────────────────────────────────────────────────────────  */
.sched-table {
  border-collapse: collapse;
  font-size: 13px;
  width: 100%;
  table-layout: auto;
}

/* ── Sticky first column ─────────────────────────────────────────────── */
.sticky-col {
  position: sticky;
  left: 0;
  z-index: 2;
}

/* ── Header: slot cell (top-left, rowspan=2) ────────────────────────── */
.th-slot {
  background: var(--bg-secondary);
  border-right: 2px solid var(--border-strong);
  border-bottom: 2px solid var(--border-strong);
  z-index: 4 !important;
  text-align: center;
  width: 84px;
  min-width: 76px;
  padding: 10px 8px;
}
.slot-header-icon {
  display: block;
  font-size: 18px;
  line-height: 1;
  margin-bottom: 4px;
}
.slot-header-text {
  display: block;
  font-size: 10px;
  font-weight: 700;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

/* ── Header: day columns ─────────────────────────────────────────────── */
.th-day {
  background: linear-gradient(135deg, #4f46e5 0%, #6366f1 60%, #818cf8 100%);
  color: #fff;
  text-align: center;
  padding: 10px 14px;
  border-left: 2px solid rgba(255, 255, 255, 0.18);
  border-bottom: 1px solid rgba(255, 255, 255, 0.18);
  white-space: nowrap;
}
.th-day:first-child { border-left: none; }
.day-name {
  display: block;
  font-size: 12px;
  font-weight: 800;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}
.day-date-sub {
  display: block;
  font-size: 10px;
  font-weight: 400;
  opacity: 0.72;
  margin-top: 3px;
  letter-spacing: 0.04em;
}

/* ── Header: group sub-row ───────────────────────────────────────────── */
.th-group {
  background: #1a2540;
  color: var(--text-secondary);
  text-align: center;
  font-size: 10px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  padding: 6px 10px;
  white-space: nowrap;
  border-bottom: 2px solid var(--border-strong);
  min-width: 140px;
}
.th-group.day-separator {
  border-left: 2px solid var(--border-strong);
}

/* ── Slot label (first column in body) ──────────────────────────────── */
.slot-label {
  background: var(--bg-secondary);
  padding: 10px 8px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  border-right: 2px solid var(--border-strong);
  border-top: 1px solid var(--border);
  min-height: 56px;
  transition: background var(--transition);
}
.slot-num {
  font-size: 20px;
  font-weight: 800;
  color: var(--text-primary);
  line-height: 1;
}
.slot-time {
  font-size: 9px;
  color: var(--text-muted);
  white-space: nowrap;
  letter-spacing: 0.03em;
  text-align: center;
}

/* ── Data cells ──────────────────────────────────────────────────────── */
.slot-cell {
  padding: 8px 10px;
  vertical-align: top;
  border-top: 1px solid var(--border);
  transition: background var(--transition);
}

.slot-cell.day-separator {
  border-left: 2px solid var(--border-strong);
}

.slot-row:hover .slot-cell {
  background: rgba(255, 255, 255, 0.025);
}
.slot-row:hover .slot-label {
  background: #253047;
}

/* Lesson type backgrounds */
.cell-lab {
  background: rgba(16, 185, 129, 0.07);
}
.slot-row:hover .cell-lab {
  background: rgba(16, 185, 129, 0.13) !important;
}
.cell-practice {
  background: rgba(245, 158, 11, 0.07);
}
.slot-row:hover .cell-practice {
  background: rgba(245, 158, 11, 0.13) !important;
}

/* Cell content */
.cell-inner {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.cell-subject {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-primary);
  line-height: 1.35;
}
.cell-detail {
  font-size: 10px;
  color: var(--text-secondary);
  background: rgba(51, 65, 85, 0.7);
  border-radius: 3px;
  padding: 1px 5px;
  display: inline-block;
  width: fit-content;
  white-space: nowrap;
}
.cell-empty {
  color: var(--text-muted);
  font-size: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 40px;
  opacity: 0.3;
}

/* Loading center */
.center-block {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 80px 20px;
}

@media (max-width: 640px) {
  .week-nav { flex-wrap: wrap; }
  .week-label { order: -1; width: 100%; align-items: center; }
  .th-slot { min-width: 68px; }
  .th-group { min-width: 110px; }
}
</style>

<template>
  <div class="page">
    <h1 class="page-title" style="margin-bottom:24px">⚙️ Настройки</h1>

    <!-- Semester dates -->
    <section class="card settings-card">
      <div class="card-title">📆 Даты семестра</div>
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">Начало семестра</label>
          <input v-model="sForm.start_date" type="date" class="form-input" />
        </div>
        <div class="form-group">
          <label class="form-label">Конец семестра</label>
          <input v-model="sForm.end_date" type="date" class="form-input" />
        </div>
      </div>
      <div style="margin-top:14px;display:flex;gap:10px;align-items:center">
        <button class="btn btn-primary" :disabled="savingSettings" @click="saveSettings">
          <span v-if="savingSettings" class="spinner spinner-sm"/>
          Сохранить
        </button>
        <span v-if="settingsSaved" class="badge badge-success">✓ Сохранено</span>
      </div>
    </section>

    <!-- Regenerate -->
    <section class="card settings-card regen-card">
      <div class="card-title">🚀 Генерация расписания</div>
      <p style="color:var(--text-secondary);font-size:14px;margin-bottom:16px">
        После изменения данных необходимо пересгенерировать расписание.
      </p>
      <div class="form-group" style="max-width:420px">
        <label class="form-label">Режим генерации</label>
        <select v-model="lockMode" class="form-select" :disabled="generating">
          <option value="none">С нуля</option>
          <option value="manual">Зафиксировать Конструктор</option>
          <option value="auto">Зафиксировать прошлую автогенерацию</option>
        </select>
      </div>
      <button class="btn btn-primary btn-lg" :disabled="generating" @click="onRegenerate" style="margin-top:8px">
        <span v-if="generating" class="spinner spinner-sm"/>
        {{ generating ? 'Генерируется…' : '🚀 Сгенерировать расписание' }}
      </button>
    </section>

    <!-- Unavailable days -->
    <section class="card settings-card">
      <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:14px">
        <div class="card-title" style="margin:0">🚫 Недоступные дни</div>
        <button class="btn btn-primary btn-sm" @click="unavailModal = true">+ Добавить</button>
      </div>

      <div v-if="store.unavailable.length === 0" style="color:var(--text-muted);font-size:14px;padding:16px 0">
        Нет записей
      </div>

      <div v-else class="unavail-list">
        <div v-for="u in store.unavailable" :key="u.id" class="unavail-item">
          <div class="unavail-info">
            <span class="unavail-who">
              {{ u.all_groups ? '🌐 Все группы' : groupName(u.group) }}
            </span>
            <span class="unavail-when">
              <template v-if="u.dates && u.dates.length">
                {{ u.dates.join(', ') }}
              </template>
              <template v-else>
                {{ u.from }} — {{ u.to }}
              </template>
            </span>
            <span v-if="u.text" class="unavail-text">{{ u.text }}</span>
          </div>
          <button class="btn btn-ghost btn-sm btn-icon" style="color:var(--error)" @click="deleteUnavail(u.id)">🗑</button>
        </div>
      </div>
    </section>

    <!-- Root solver config modal (открывается по набору 'root!' — без видимого поля) -->
    <Modal v-model="rootMenuOpen" title="🔧 Параметры решателя (root)">
      <div v-if="!rootCfg" style="padding:20px;text-align:center;color:var(--text-muted)">
        Загрузка…
      </div>
      <template v-else>
        <p style="color:var(--text-secondary);font-size:13px;margin-bottom:14px">
          Параметры применяются при следующей регенерации.
        </p>
        <div v-for="section in rootSections" :key="section.id" class="root-section">
          <h3 class="root-section-title">{{ section.title }}</h3>
          <div v-for="field in section.fields" :key="field.key" class="root-field">
            <div class="root-field-row">
              <label class="root-field-label" :title="field.description">{{ field.label }}</label>
              <div class="root-field-control">
                <input
                  v-if="field.type === 'bool'"
                  type="checkbox"
                  v-model="rootCfg[field.key]"
                />
                <input
                  v-else
                  type="number"
                  :step="field.type === 'double' ? '0.01' : '1'"
                  v-model.number="rootCfg[field.key]"
                  class="form-input root-input-num"
                />
              </div>
            </div>
            <div class="root-field-desc">{{ field.description }}</div>
          </div>
        </div>
      </template>
      <template #footer>
        <button class="btn btn-ghost" @click="rootMenuOpen = false">Закрыть</button>
        <button class="btn btn-secondary" :disabled="savingRoot || !rootCfg" @click="resetRootCfg">
          Сбросить к дефолтам
        </button>
        <button class="btn btn-primary" :disabled="savingRoot || !rootCfg" @click="saveRootCfg">
          <span v-if="savingRoot" class="spinner spinner-sm"/>Сохранить
        </button>
      </template>
    </Modal>

    <!-- Add unavailable modal -->
    <Modal v-model="unavailModal" title="Добавить недоступный день">
      <label class="form-checkbox" style="margin-bottom:4px">
        <input type="checkbox" v-model="uForm.all_groups" />
        Применить ко всем группам
      </label>
      <div v-if="!uForm.all_groups" class="form-group">
        <label class="form-label">Группа</label>
        <select v-model.number="uForm.group" class="form-select">
          <option v-for="g in store.groups" :key="g.id" :value="g.id">{{ g.name }}</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Тип периода</label>
        <select v-model="uForm.mode" class="form-select">
          <option value="range">Диапазон дат (от–до)</option>
          <option value="dates">Конкретные даты</option>
        </select>
      </div>
      <template v-if="uForm.mode === 'range'">
        <div class="form-row">
          <div class="form-group">
            <label class="form-label">От</label>
            <input v-model="uForm.from" type="date" class="form-input" />
          </div>
          <div class="form-group">
            <label class="form-label">До</label>
            <input v-model="uForm.to" type="date" class="form-input" />
          </div>
        </div>
      </template>
      <template v-else>
        <div class="form-group">
          <label class="form-label">Даты (через запятую, YYYY-MM-DD)</label>
          <input v-model="uForm.datesStr" class="form-input" placeholder="2026-02-23, 2026-03-08" />
        </div>
      </template>
      <div class="form-group">
        <label class="form-label">Текст (необязательно)</label>
        <input v-model="uForm.text" class="form-input" placeholder="Например: Праздник" />
      </div>
      <template #footer>
        <button class="btn btn-ghost" @click="unavailModal = false">Отмена</button>
        <button class="btn btn-primary" :disabled="savingUnavail" @click="addUnavail">
          <span v-if="savingUnavail" class="spinner spinner-sm"/>Добавить
        </button>
      </template>
    </Modal>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onBeforeUnmount } from 'vue'
import Modal from '../components/Modal.vue'
import { useDataStore } from '../stores/data.js'
import { useScheduleStore } from '../stores/schedule.js'
import { useToast } from '../composables/useToast.js'
import { api } from '../api/index.js'

const store = useDataStore()
const schedStore = useScheduleStore()
const toast = useToast()

const sForm = reactive({ start_date: '', end_date: '' })
const savingSettings = ref(false)
const settingsSaved = ref(false)
const generating = ref(false)
const unavailModal = ref(false)
const savingUnavail = ref(false)
const lockMode = ref('none')

const uForm = reactive({
  all_groups: false, group: 0, mode: 'range',
  from: '', to: '', datesStr: '', text: ''
})

// ── Root menu state ──
const ROOT_TRIGGER = 'root!'
const rootMenuOpen = ref(false)
const rootCfg = ref(null)
const rootSchema = ref([])
const savingRoot = ref(false)
let rootBuffer = ''

const SECTION_TITLES = {
  solver: '⚙️ Solver',
  hard_soft: '🔒 Жёсткие/мягкие ограничения',
  weights: '⚖️ Веса штрафов',
  shape: '📐 Размерности',
}

const rootSections = computed(() => {
  const groups = {}
  for (const f of rootSchema.value) {
    const cat = f.category || 'other'
    if (!groups[cat]) {
      groups[cat] = { id: cat, title: SECTION_TITLES[cat] || cat, fields: [] }
    }
    groups[cat].fields.push(f)
  }
  return Object.values(groups)
})

function onRootKey(e) {
  const t = e.target
  if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable)) return
  if (!e.key) return
  if (e.key.length === 1) {
    rootBuffer = (rootBuffer + e.key).slice(-ROOT_TRIGGER.length)
    if (rootBuffer === ROOT_TRIGGER) {
      rootBuffer = ''
      openRootMenu()
    }
  } else if (e.key === 'Escape' || e.key === 'Backspace') {
    rootBuffer = ''
  }
}

async function openRootMenu() {
  const r = await api.settings.getSolverConfig()
  if (!r.ok) {
    toast.error(r.data?.message || 'Не удалось загрузить параметры')
    return
  }
  rootCfg.value = { ...(r.data.values || {}) }
  rootSchema.value = r.data.schema || []
  rootMenuOpen.value = true
}

async function saveRootCfg() {
  if (!rootCfg.value) return
  savingRoot.value = true
  const r = await api.settings.updateSolverConfig(rootCfg.value)
  savingRoot.value = false
  if (r.ok) toast.success('Параметры сохранены. Нужна регенерация.')
  else toast.error(r.data?.message || 'Ошибка сохранения')
}

async function resetRootCfg() {
  savingRoot.value = true
  const r = await api.settings.resetSolverConfig()
  savingRoot.value = false
  if (r.ok) {
    const fresh = await api.settings.getSolverConfig()
    if (fresh.ok) rootCfg.value = { ...(fresh.data.values || {}) }
    toast.success('Сброшено к дефолтам')
  } else toast.error(r.data?.message || 'Ошибка сброса')
}

onMounted(async () => {
  document.addEventListener('keydown', onRootKey)
  await Promise.all([store.loadGroups(), store.loadUnavailable(), loadSettings()])
  if (store.groups.length) uForm.group = store.groups[0].id
})

onBeforeUnmount(() => {
  document.removeEventListener('keydown', onRootKey)
})

async function loadSettings() {
  const res = await api.settings.get()
  if (res.ok) {
    sForm.start_date = res.data.start_date || ''
    sForm.end_date = res.data.end_date || ''
  }
}

async function saveSettings() {
  savingSettings.value = true
  const r = await store.saveSettings({ start_date: sForm.start_date, end_date: sForm.end_date })
  savingSettings.value = false
  if (r.ok) {
    settingsSaved.value = true
    toast.success('Настройки сохранены')
    setTimeout(() => { settingsSaved.value = false }, 3000)
  } else toast.error(r.data?.message || 'Ошибка сохранения')
}

async function onRegenerate() {
  generating.value = true
  const opts = lockMode.value && lockMode.value !== 'none' ? { lock_existing: lockMode.value } : {}
  const r = await schedStore.regenerate(opts)
  generating.value = false
  if (r.ok) toast.success(r.message || 'Расписание успешно сгенерировано!')
  else toast.error(r.message || 'Ошибка генерации')
}

function groupName(id) { return store.groups.find(g => g.id === id)?.name ?? `Группа ${id}` }

async function addUnavail() {
  savingUnavail.value = true
  const body = {}
  if (uForm.all_groups) body.all_groups = true
  else body.group = uForm.group
  if (uForm.text.trim()) body.text = uForm.text.trim()
  if (uForm.mode === 'range') {
    body.from = uForm.from; body.to = uForm.to
  } else {
    body.dates = uForm.datesStr.split(',').map(s => s.trim()).filter(Boolean)
  }
  const r = await store.createUnavailable(body)
  savingUnavail.value = false
  if (r.ok) {
    toast.success('Добавлено')
    unavailModal.value = false
    Object.assign(uForm, { all_groups: false, from: '', to: '', datesStr: '', text: '', mode: 'range' })
  } else toast.error(r.data?.message || 'Ошибка')
}

async function deleteUnavail(id) {
  const r = await store.deleteUnavailable(id)
  if (r.ok) toast.success('Удалено')
  else toast.error(r.data?.message || 'Ошибка')
}
</script>

<style scoped>
.settings-card { margin-bottom: 20px; }
.regen-card { border-color: var(--accent-light2); background: var(--accent-light); }
.form-row { display: flex; gap: 12px; }
.form-row > * { flex: 1; }
.unavail-list { display: flex; flex-direction: column; gap: 8px; }
.unavail-item {
  display: flex; align-items: center; justify-content: space-between;
  background: var(--bg-tertiary); border-radius: var(--radius-sm);
  padding: 10px 12px; gap: 12px;
}
.unavail-info { display: flex; flex-direction: column; gap: 3px; flex: 1; }
.unavail-who { font-weight: 600; font-size: 14px; }
.unavail-when { font-size: 12px; color: var(--text-muted); }
.unavail-text { font-size: 12px; color: var(--text-secondary); }
@media (max-width: 500px) { .form-row { flex-direction: column; } }

/* ── Root menu ── */
.root-section { margin-bottom: 20px; }
.root-section-title {
  font-size: 13px; font-weight: 700; text-transform: uppercase;
  color: var(--accent); margin: 0 0 10px 0; letter-spacing: 0.05em;
}
.root-field { padding: 8px 0; border-bottom: 1px solid var(--border); }
.root-field:last-child { border-bottom: none; }
.root-field-row {
  display: flex; align-items: center; justify-content: space-between; gap: 12px;
}
.root-field-label {
  font-size: 13px; color: var(--text-primary); flex: 1; cursor: help;
}
.root-field-control { display: flex; align-items: center; min-width: 120px; justify-content: flex-end; }
.root-input-num { width: 110px; padding: 4px 8px; font-size: 12px; }
.root-field-desc {
  font-size: 11px; color: var(--text-muted); margin-top: 3px; line-height: 1.35;
}
</style>

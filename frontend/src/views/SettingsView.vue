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
      <button class="btn btn-primary btn-lg" :disabled="generating" @click="onRegenerate">
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
import { ref, reactive, onMounted } from 'vue'
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

const uForm = reactive({
  all_groups: false, group: 0, mode: 'range',
  from: '', to: '', datesStr: '', text: ''
})

onMounted(async () => {
  await Promise.all([store.loadGroups(), store.loadUnavailable(), loadSettings()])
  if (store.groups.length) uForm.group = store.groups[0].id
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
  const r = await schedStore.regenerate()
  generating.value = false
  if (r.ok) toast.success('Расписание успешно сгенерировано!')
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
</style>

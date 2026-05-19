<template>
  <div class="page">
    <div class="page-header">
      <h1 class="page-title">🎓 Группы</h1>
      <button class="btn btn-primary" @click="openAdd">+ Добавить</button>
    </div>

    <div v-if="loading" class="center-load"><span class="spinner spinner-lg" style="color:var(--accent)"/></div>

    <div v-else-if="store.groups.length === 0" class="empty-state">
      <span class="icon">🎓</span>
      <h3>Нет групп</h3>
      <p>Добавьте первую группу</p>
    </div>

    <div v-else class="cards-grid">
      <div v-for="g in store.groups" :key="g.id" class="group-card card">
        <div class="gc-top">
          <span class="gc-name">{{ g.name }}</span>
          <span class="badge badge-accent">ID {{ g.id }}</span>
        </div>
        <div class="gc-meta">
          <span class="chip">{{ g.parts === 1 ? '1 подгруппа' : '2 подгруппы' }}</span>
        </div>
        <div class="gc-actions">
          <button class="btn btn-ghost btn-sm" @click="openEdit(g)">✏️ Изменить</button>
          <button class="btn btn-ghost btn-sm" style="color:var(--error)" @click="confirmDelete(g)">🗑 Удалить</button>
        </div>
      </div>
    </div>

    <Modal v-model="modalOpen" :title="editItem ? 'Редактировать группу' : 'Добавить группу'">
      <div class="form-group">
        <label class="form-label">Название группы</label>
        <input v-model="form.name" class="form-input" placeholder="Например: ИСП-3306" />
      </div>
      <div class="form-group">
        <label class="form-label">Количество подгрупп</label>
        <select v-model.number="form.parts" class="form-select">
          <option :value="1">1 подгруппа (без деления)</option>
          <option :value="2">2 подгруппы</option>
        </select>
      </div>
      <template #footer>
        <button class="btn btn-ghost" @click="modalOpen = false">Отмена</button>
        <button class="btn btn-primary" :disabled="saving || !form.name.trim()" @click="save">
          <span v-if="saving" class="spinner spinner-sm"/>
          {{ editItem ? 'Сохранить' : 'Добавить' }}
        </button>
      </template>
    </Modal>

    <Modal v-model="deleteModal" title="Удалить группу?">
      <p style="color:var(--text-secondary)">Группа <strong style="color:var(--text-primary)">{{ deleteTarget?.name }}</strong> будет удалена.</p>
      <template #footer>
        <button class="btn btn-ghost" @click="deleteModal = false">Отмена</button>
        <button class="btn btn-danger" :disabled="saving" @click="doDelete">
          <span v-if="saving" class="spinner spinner-sm"/>Удалить
        </button>
      </template>
    </Modal>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import Modal from '../components/Modal.vue'
import { useDataStore } from '../stores/data.js'
import { useToast } from '../composables/useToast.js'

const store = useDataStore()
const toast = useToast()
const loading = ref(false)
const saving = ref(false)
const modalOpen = ref(false)
const deleteModal = ref(false)
const editItem = ref(null)
const deleteTarget = ref(null)
const form = ref({ name: '', parts: 2 })

onMounted(async () => { loading.value = true; await store.loadGroups(); loading.value = false })

function openAdd() { editItem.value = null; form.value = { name: '', parts: 2 }; modalOpen.value = true }
function openEdit(g) { editItem.value = g; form.value = { name: g.name, parts: g.parts }; modalOpen.value = true }
function confirmDelete(g) { deleteTarget.value = g; deleteModal.value = true }

async function save() {
  if (!form.value.name.trim()) return
  saving.value = true
  const d = { name: form.value.name.trim(), parts: form.value.parts }
  const r = editItem.value ? await store.updateGroup(editItem.value.id, d) : await store.createGroup(d)
  saving.value = false
  if (r.ok) { toast.success(editItem.value ? 'Группа обновлена' : 'Группа добавлена'); modalOpen.value = false }
  else toast.error(r.data?.message || 'Ошибка')
}

async function doDelete() {
  saving.value = true
  const r = await store.deleteGroup(deleteTarget.value.id)
  saving.value = false
  if (r.ok) { toast.success('Группа удалена'); deleteModal.value = false }
  else toast.error(r.data?.message || 'Ошибка')
}
</script>

<style scoped>
.center-load { display:flex; justify-content:center; padding:60px; }
.cards-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 14px; }
.group-card { display: flex; flex-direction: column; gap: 10px; }
.gc-top { display: flex; align-items: center; justify-content: space-between; }
.gc-name { font-size: 18px; font-weight: 700; }
.gc-meta { }
.gc-actions { display: flex; gap: 8px; border-top: 1px solid var(--border); padding-top: 10px; }
</style>

<template>
  <div class="page">
    <div class="page-header">
      <h1 class="page-title">👤 Преподаватели</h1>
      <button class="btn btn-primary" @click="openAdd">+ Добавить</button>
    </div>

    <div v-if="loading" class="center-load"><span class="spinner spinner-lg" style="color:var(--accent)"/></div>

    <div v-else-if="store.teachers.length === 0" class="empty-state">
      <span class="icon">👤</span>
      <h3>Нет преподавателей</h3>
      <p>Добавьте первого преподавателя</p>
    </div>

    <div v-else class="cards-grid">
      <div v-for="t in store.teachers" :key="t.id" class="teacher-card card">
        <div class="tc-body">
          <div class="tc-avatar">{{ initials(t.name) }}</div>
          <div class="tc-info">
            <div class="tc-name">{{ t.name }}</div>
            <div class="tc-id">ID: {{ t.id }}</div>
          </div>
        </div>
        <div class="tc-actions">
          <button class="btn btn-ghost btn-sm" @click="openEdit(t)">✏️ Изменить</button>
          <button class="btn btn-ghost btn-sm" style="color:var(--error)" @click="confirmDelete(t)">🗑 Удалить</button>
        </div>
      </div>
    </div>

    <!-- Add/Edit modal -->
    <Modal v-model="modalOpen" :title="editItem ? 'Редактировать преподавателя' : 'Добавить преподавателя'">
      <div class="form-group">
        <label class="form-label">ФИО преподавателя</label>
        <input v-model="form.name" class="form-input" placeholder="Например: Иванов И.И." @keyup.enter="save" />
      </div>
      <template #footer>
        <button class="btn btn-ghost" @click="modalOpen = false">Отмена</button>
        <button class="btn btn-primary" :disabled="saving || !form.name.trim()" @click="save">
          <span v-if="saving" class="spinner spinner-sm"/>
          {{ editItem ? 'Сохранить' : 'Добавить' }}
        </button>
      </template>
    </Modal>

    <!-- Delete confirm -->
    <Modal v-model="deleteModal" title="Удалить преподавателя?">
      <p style="color:var(--text-secondary)">Преподаватель <strong style="color:var(--text-primary)">{{ deleteTarget?.name }}</strong> будет удалён. Это действие нельзя отменить.</p>
      <template #footer>
        <button class="btn btn-ghost" @click="deleteModal = false">Отмена</button>
        <button class="btn btn-danger" :disabled="saving" @click="doDelete">
          <span v-if="saving" class="spinner spinner-sm"/>
          Удалить
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
const form = ref({ name: '' })

onMounted(async () => {
  loading.value = true
  await store.loadTeachers()
  loading.value = false
})

function initials(name) {
  return name.split(/\s+/).map(w => w[0]).join('').slice(0, 2).toUpperCase()
}

function openAdd() {
  editItem.value = null
  form.value = { name: '' }
  modalOpen.value = true
}

function openEdit(t) {
  editItem.value = t
  form.value = { name: t.name }
  modalOpen.value = true
}

function confirmDelete(t) {
  deleteTarget.value = t
  deleteModal.value = true
}

async function save() {
  if (!form.value.name.trim()) return
  saving.value = true
  let r
  if (editItem.value) {
    r = await store.updateTeacher(editItem.value.id, { name: form.value.name.trim() })
  } else {
    r = await store.createTeacher({ name: form.value.name.trim() })
  }
  saving.value = false
  if (r.ok) {
    toast.success(editItem.value ? 'Преподаватель обновлён' : 'Преподаватель добавлен')
    modalOpen.value = false
  } else {
    toast.error(r.data?.message || 'Ошибка сохранения')
  }
}

async function doDelete() {
  saving.value = true
  const r = await store.deleteTeacher(deleteTarget.value.id)
  saving.value = false
  if (r.ok) {
    toast.success('Преподаватель удалён')
    deleteModal.value = false
  } else {
    toast.error(r.data?.message || 'Ошибка удаления')
  }
}
</script>

<style scoped>
.center-load { display:flex; justify-content:center; padding:60px; }
.cards-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 14px; }
.teacher-card { display: flex; flex-direction: column; gap: 12px; }
.tc-body { display: flex; align-items: center; gap: 12px; }
.tc-avatar {
  width: 44px; height: 44px; border-radius: 50%;
  background: var(--accent-light); color: var(--accent);
  display: flex; align-items: center; justify-content: center;
  font-size: 15px; font-weight: 700; flex-shrink: 0;
}
.tc-name { font-weight: 600; font-size: 15px; }
.tc-id { font-size: 12px; color: var(--text-muted); }
.tc-actions { display: flex; gap: 8px; border-top: 1px solid var(--border); padding-top: 10px; }
</style>

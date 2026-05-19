import { defineStore } from 'pinia'
import { ref } from 'vue'
import { api } from '../api/index.js'

export const useDataStore = defineStore('data', () => {
  const teachers = ref([])
  const groups = ref([])
  const lessons = ref([])
  const unavailable = ref([])
  const settings = ref({ start_date: '', end_date: '' })
  const loading = ref(false)

  async function loadAll() {
    loading.value = true
    const [t, g, l, u, s] = await Promise.all([
      api.teachers.list(),
      api.groups.list(),
      api.lessons.list(),
      api.unavailable.list(),
      api.settings.get(),
    ])
    if (t.ok) teachers.value = t.data
    if (g.ok) groups.value = g.data
    if (l.ok) lessons.value = l.data
    if (u.ok) unavailable.value = u.data
    if (s.ok) settings.value = s.data
    loading.value = false
  }

  async function loadTeachers() {
    const r = await api.teachers.list()
    if (r.ok) teachers.value = r.data
    return r
  }
  async function createTeacher(d) {
    const r = await api.teachers.create(d)
    if (r.ok) await loadTeachers()
    return r
  }
  async function updateTeacher(id, d) {
    const r = await api.teachers.update(id, d)
    if (r.ok) await loadTeachers()
    return r
  }
  async function deleteTeacher(id) {
    const r = await api.teachers.remove(id)
    if (r.ok) await loadTeachers()
    return r
  }

  async function loadGroups() {
    const r = await api.groups.list()
    if (r.ok) groups.value = r.data
    return r
  }
  async function createGroup(d) {
    const r = await api.groups.create(d)
    if (r.ok) await loadGroups()
    return r
  }
  async function updateGroup(id, d) {
    const r = await api.groups.update(id, d)
    if (r.ok) await loadGroups()
    return r
  }
  async function deleteGroup(id) {
    const r = await api.groups.remove(id)
    if (r.ok) await loadGroups()
    return r
  }

  async function loadLessons() {
    const r = await api.lessons.list()
    if (r.ok) lessons.value = r.data
    return r
  }
  async function createLesson(d) {
    const r = await api.lessons.create(d)
    if (r.ok) await loadLessons()
    return r
  }
  async function updateLesson(id, d) {
    const r = await api.lessons.update(id, d)
    if (r.ok) await loadLessons()
    return r
  }
  async function deleteLesson(id) {
    const r = await api.lessons.remove(id)
    if (r.ok) await loadLessons()
    return r
  }

  async function loadUnavailable() {
    const r = await api.unavailable.list()
    if (r.ok) unavailable.value = r.data
    return r
  }
  async function createUnavailable(d) {
    const r = await api.unavailable.create(d)
    if (r.ok) await loadUnavailable()
    return r
  }
  async function deleteUnavailable(id) {
    const r = await api.unavailable.remove(id)
    if (r.ok) await loadUnavailable()
    return r
  }

  async function saveSettings(d) {
    const r = await api.settings.update(d)
    if (r.ok) settings.value = { ...settings.value, ...d }
    return r
  }

  return {
    teachers, groups, lessons, unavailable, settings, loading,
    loadAll,
    loadTeachers, createTeacher, updateTeacher, deleteTeacher,
    loadGroups, createGroup, updateGroup, deleteGroup,
    loadLessons, createLesson, updateLesson, deleteLesson,
    loadUnavailable, createUnavailable, deleteUnavailable,
    saveSettings,
  }
})

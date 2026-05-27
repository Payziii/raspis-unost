import { defineStore } from 'pinia'
import { ref } from 'vue'
import { api } from '../api/index.js'

export const useConstructorStore = defineStore('constructor', () => {
  const manualData = ref(null)   // { groups: [{group_index, group_name, days: [{date, date_iso, day_index, weekday, slots: [{slot, time, text, lessons}]}]}] }
  const loading = ref(false)
  const saving = ref(false)
  const error = ref(null)
  const dirty = ref(false)

  function emptyData() {
    return { groups: [] }
  }

  function markDirty() { dirty.value = true }

  async function load() {
    loading.value = true
    error.value = null
    const r = await api.constructor.load()
    loading.value = false
    if (r.ok) {
      manualData.value = r.data
      dirty.value = false
    } else if (r.status === 404) {
      manualData.value = emptyData()
      dirty.value = false
    } else {
      error.value = r.data?.message || 'Ошибка загрузки'
    }
    return r
  }

  async function copyFromAuto() {
    saving.value = true
    const r = await api.constructor.copyFromAuto()
    if (r.ok) await load()
    saving.value = false
    return r
  }

  async function clear() {
    saving.value = true
    const r = await api.constructor.clear()
    if (r.ok) {
      manualData.value = emptyData()
      dirty.value = false
    }
    saving.value = false
    return r
  }

  async function save() {
    if (!manualData.value) return { ok: false }
    saving.value = true
    const r = await api.constructor.save(manualData.value)
    if (r.ok) dirty.value = false
    saving.value = false
    return r
  }

  // ── Cell mutations ──

  function findGroup(gi) {
    if (!manualData.value) return null
    return manualData.value.groups.find(g => g.group_index === gi) || null
  }

  function ensureGroup(gi, gname) {
    if (!manualData.value) manualData.value = emptyData()
    let g = findGroup(gi)
    if (!g) {
      g = { group_index: gi, group_name: gname, days: [] }
      manualData.value.groups.push(g)
    }
    return g
  }

  function findOrCreateDay(group, dateIso, displayDate, weekday) {
    let day = group.days.find(d => d.date_iso === dateIso)
    if (!day) {
      day = {
        date_iso: dateIso,
        date: displayDate,
        weekday: weekday || '',
        day_index: -1,
        slots: [],
      }
      group.days.push(day)
      group.days.sort((a, b) => (a.date_iso || '').localeCompare(b.date_iso || ''))
    }
    return day
  }

  function findOrCreateSlot(day, slotNum, time) {
    let s = day.slots.find(x => x.slot === slotNum)
    if (!s) {
      s = { slot: slotNum, time: time || '', text: '-', lessons: [] }
      day.slots.push(s)
      day.slots.sort((a, b) => a.slot - b.slot)
    }
    return s
  }

  function setSlotLessons(slot, lessons, textFallback) {
    slot.lessons = lessons.slice()
    if (lessons.length === 0) {
      slot.text = '-'
    } else if (lessons.length === 1) {
      const L = lessons[0]
      slot.text = `${L.name} — ${L.subgroupName || ''}`
    } else {
      slot.text = lessons.map(L => `${L.name}`).join(' | ')
    }
    if (textFallback) slot.text = textFallback
    markDirty()
  }

  return {
    manualData, loading, saving, error, dirty,
    load, copyFromAuto, clear, save,
    findGroup, ensureGroup, findOrCreateDay, findOrCreateSlot,
    setSlotLessons, markDirty,
  }
})

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { api } from '../api/index.js'

export const useScheduleStore = defineStore('schedule', () => {
  const scheduleData = ref(null)
  const loading = ref(false)
  const error = ref(null)
  const generating = ref(false)

  async function fetchSchedule() {
    loading.value = true
    error.value = null
    const res = await api.schedule.get()
    loading.value = false
    if (res.ok) {
      scheduleData.value = res.data
    } else {
      error.value = res.data?.message || 'Ошибка загрузки расписания'
    }
  }

  async function regenerate() {
    generating.value = true
    const res = await api.schedule.regenerate()
    generating.value = false
    if (res.ok) {
      await fetchSchedule()
      return { ok: true, message: res.data?.message || 'Расписание сгенерировано' }
    }
    return { ok: false, message: res.data?.message || 'Ошибка генерации' }
  }

  const groups = computed(() => scheduleData.value?.groups || [])

  return { scheduleData, loading, error, generating, groups, fetchSchedule, regenerate }
})

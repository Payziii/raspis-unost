import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { api } from '../api/index.js'

export const useScheduleStore = defineStore('schedule', () => {
  const scheduleData = ref(null)
  const loading = ref(false)
  const error = ref(null)
  const generating = ref(false)

  // Прогресс недельной генерации
  const progress = ref(null)   // null | { state, total_weeks, current_week, solved_weeks, weeks, message, total_elapsed }
  let _pollTimer = null

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

  function _stopPolling() {
    if (_pollTimer) { clearInterval(_pollTimer); _pollTimer = null }
  }

  async function _pollProgress() {
    const res = await api.schedule.progress()
    if (!res.ok) return
    progress.value = res.data

    const st = res.data?.state
    if (st === 'done' || st === 'failed' || st === 'cancelled' || st === 'idle') {
      _stopPolling()
      generating.value = false
      // Обновляем расписание если успешно завершено
      if (st === 'done') await fetchSchedule()
    }
  }

  async function regenerate(opts = {}) {
    generating.value = true
    progress.value = null

    const res = await api.schedule.regenerate(opts)

    if (!res.ok) {
      generating.value = false
      return { ok: false, message: res.data?.message || 'Ошибка генерации' }
    }

    if (res.data?.async) {
      // Async (weekly) — запускаем polling
      _stopPolling()
      _pollTimer = setInterval(_pollProgress, 1000)
      return { ok: true, async: true, message: 'Генерация запущена' }
    }

    // Sync (monolithic) — старое поведение
    generating.value = false
    if (res.ok) {
      await fetchSchedule()
      const extra = res.data?.lock_source && res.data.lock_source !== 'none'
        ? ` (закреплено ${res.data.locked_count} слотов из «${res.data.lock_source}»)` : ''
      return { ok: true, message: (res.data?.message || 'Расписание сгенерировано') + extra }
    }
    return { ok: false, message: res.data?.message || 'Ошибка генерации' }
  }

  async function cancelGeneration() {
    const res = await api.schedule.cancel()
    return res.ok
  }

  const groups = computed(() => scheduleData.value?.groups || [])

  return {
    scheduleData, loading, error, generating,
    progress,
    groups,
    fetchSchedule, regenerate, cancelGeneration,
  }
})

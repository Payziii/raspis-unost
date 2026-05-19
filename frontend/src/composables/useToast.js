import { ref } from 'vue'

const toasts = ref([])
let nextId = 0

export function useToast() {
  function show(message, type = 'info', duration = 4000) {
    const id = ++nextId
    toasts.value.push({ id, message, type })
    setTimeout(() => {
      toasts.value = toasts.value.filter(t => t.id !== id)
    }, duration)
  }
  return {
    toasts,
    show,
    success: (m) => show(m, 'success'),
    error: (m) => show(m, 'error'),
    info: (m) => show(m, 'info'),
    warning: (m) => show(m, 'warning'),
  }
}

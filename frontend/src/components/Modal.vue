<template>
  <Teleport to="body">
    <Transition name="modal-fade">
      <div v-if="modelValue" class="modal-backdrop" @click.self="close">
        <div class="modal-box" role="dialog" :aria-label="title">
          <div class="modal-header">
            <span class="modal-title">{{ title }}</span>
            <button class="btn btn-icon btn-ghost" @click="close" aria-label="Закрыть">✕</button>
          </div>
          <div class="modal-body">
            <slot />
          </div>
          <div v-if="$slots.footer" class="modal-footer">
            <slot name="footer" />
          </div>
        </div>
      </div>
    </Transition>
  </Teleport>
</template>

<script setup>
import { onMounted, onUnmounted } from 'vue'

const props = defineProps({ modelValue: Boolean, title: String })
const emit = defineEmits(['update:modelValue'])

function close() { emit('update:modelValue', false) }

function onKey(e) { if (e.key === 'Escape') close() }
onMounted(() => document.addEventListener('keydown', onKey))
onUnmounted(() => document.removeEventListener('keydown', onKey))
</script>

<style scoped>
.modal-backdrop {
  position: fixed; inset: 0; z-index: 1000;
  background: rgba(0,0,0,0.6); backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center; padding: 16px;
}
.modal-box {
  background: var(--bg-secondary); border: 1px solid var(--border-strong);
  border-radius: var(--radius-lg); width: 100%; max-width: 480px;
  max-height: 90vh; display: flex; flex-direction: column;
  box-shadow: 0 20px 60px rgba(0,0,0,0.5);
}
.modal-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 20px; border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}
.modal-title { font-size: 16px; font-weight: 600; }
.modal-body { padding: 20px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 16px; }
.modal-footer { padding: 16px 20px; border-top: 1px solid var(--border); display: flex; gap: 10px; justify-content: flex-end; flex-shrink: 0; }

.modal-fade-enter-active, .modal-fade-leave-active { transition: opacity 0.2s; }
.modal-fade-enter-from, .modal-fade-leave-to { opacity: 0; }
.modal-fade-enter-active .modal-box, .modal-fade-leave-active .modal-box { transition: transform 0.2s; }
.modal-fade-enter-from .modal-box { transform: scale(0.95) translateY(8px); }
.modal-fade-leave-to .modal-box { transform: scale(0.97); }
</style>

<template>
  <Teleport to="body">
    <div class="toast-container">
      <TransitionGroup name="toast">
        <div
          v-for="t in toasts"
          :key="t.id"
          class="toast"
          :class="`toast-${t.type}`"
        >
          <span class="toast-icon">{{ icons[t.type] }}</span>
          <span class="toast-msg">{{ t.message }}</span>
        </div>
      </TransitionGroup>
    </div>
  </Teleport>
</template>

<script setup>
import { useToast } from '../composables/useToast.js'
const { toasts } = useToast()
const icons = { success: '✓', error: '✕', warning: '⚠', info: 'ℹ' }
</script>

<style scoped>
.toast-container {
  position: fixed; bottom: 24px; right: 24px; z-index: 2000;
  display: flex; flex-direction: column; gap: 8px; pointer-events: none;
}
.toast {
  display: flex; align-items: center; gap: 10px;
  padding: 12px 16px; border-radius: var(--radius);
  font-size: 14px; font-weight: 500; min-width: 260px; max-width: 360px;
  pointer-events: all; box-shadow: 0 8px 24px rgba(0,0,0,0.4);
  border: 1px solid;
}
.toast-success { background: #052e16; border-color: #166534; color: #86efac; }
.toast-error   { background: #1c0a0a; border-color: #7f1d1d; color: #fca5a5; }
.toast-warning { background: #1c1200; border-color: #78350f; color: #fcd34d; }
.toast-info    { background: #0c1a2e; border-color: #164e63; color: #7dd3fc; }
.toast-icon { font-size: 16px; flex-shrink: 0; }

.toast-enter-active, .toast-leave-active { transition: all 0.25s ease; }
.toast-enter-from { opacity: 0; transform: translateX(24px); }
.toast-leave-to   { opacity: 0; transform: translateX(24px); }

@media (max-width: 480px) {
  .toast-container { left: 12px; right: 12px; bottom: 12px; }
  .toast { min-width: 0; }
}
</style>

<template>
  <nav class="navbar">
    <div class="nav-inner">
      <RouterLink to="/schedule" class="nav-logo">
        <img src="/icons/icon.svg" alt="" class="nav-logo-img" />
        <span>Расписание</span>
      </RouterLink>

      <div class="nav-links desktop-only">
        <RouterLink v-for="l in links" :key="l.to" :to="l.to" class="nav-link">
          {{ l.label }}
        </RouterLink>
      </div>

      <button class="btn btn-icon btn-ghost mobile-only" @click="drawerOpen = !drawerOpen" aria-label="Меню">
        <span class="burger" :class="{ open: drawerOpen }">
          <span /><span /><span />
        </span>
      </button>
    </div>
  </nav>

  <Transition name="drawer">
    <div v-if="drawerOpen" class="drawer-backdrop" @click="drawerOpen = false">
      <div class="drawer" @click.stop>
        <div class="drawer-header">
          <span class="nav-logo-text">Расписание</span>
          <button class="btn btn-icon btn-ghost" @click="drawerOpen = false">✕</button>
        </div>
        <div class="drawer-links">
          <RouterLink
            v-for="l in links" :key="l.to" :to="l.to"
            class="drawer-link" @click="drawerOpen = false"
          >
            <span class="drawer-link-icon">{{ l.icon }}</span>
            {{ l.label }}
          </RouterLink>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { ref, watch } from 'vue'
import { useRoute } from 'vue-router'

const drawerOpen = ref(false)
const route = useRoute()
watch(route, () => { drawerOpen.value = false })

const links = [
  { to: '/schedule',    label: 'Расписание',    icon: '📅' },
  { to: '/constructor', label: 'Конструктор',   icon: '🛠' },
  { to: '/teachers',    label: 'Преподаватели', icon: '👤' },
  { to: '/groups',      label: 'Группы',        icon: '🎓' },
  { to: '/lessons',     label: 'Пары',          icon: '📖' },
  { to: '/settings',    label: 'Настройки',     icon: '⚙️' },
]
</script>

<style scoped>
.navbar {
  position: sticky; top: 0; z-index: 100;
  background: rgba(15, 23, 42, 0.85);
  backdrop-filter: blur(12px);
  border-bottom: 1px solid var(--border);
  height: var(--nav-height);
}
.nav-inner {
  max-width: 1400px; margin: 0 auto;
  height: 100%; padding: 0 20px;
  display: flex; align-items: center; justify-content: space-between;
}
.nav-logo {
  display: flex; align-items: center; gap: 8px;
  font-size: 16px; font-weight: 700; color: var(--text-primary);
  text-decoration: none;
}
.nav-logo-img { width: 28px; height: 28px; border-radius: 6px; }
.nav-logo-text { font-size: 16px; font-weight: 700; }

.nav-links { display: flex; align-items: center; gap: 4px; }
.nav-link {
  padding: 6px 14px; border-radius: var(--radius-sm);
  font-size: 14px; font-weight: 500;
  color: var(--text-secondary); transition: all var(--transition);
  text-decoration: none;
}
.nav-link:hover { color: var(--text-primary); background: var(--accent-light); }
.nav-link.router-link-active { color: var(--accent); background: var(--accent-light); }

.burger { display: flex; flex-direction: column; gap: 4px; width: 18px; }
.burger span { display: block; height: 2px; background: var(--text-secondary); border-radius: 2px; transition: all 0.2s; }
.burger.open span:nth-child(1) { transform: rotate(45deg) translate(4px, 4px); }
.burger.open span:nth-child(2) { opacity: 0; }
.burger.open span:nth-child(3) { transform: rotate(-45deg) translate(4px, -4px); }

.drawer-backdrop {
  position: fixed; inset: 0; z-index: 200;
  background: rgba(0,0,0,0.5); backdrop-filter: blur(2px);
}
.drawer {
  position: absolute; top: 0; right: 0; bottom: 0; width: 260px;
  background: var(--bg-secondary); border-left: 1px solid var(--border);
  display: flex; flex-direction: column;
}
.drawer-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 20px; border-bottom: 1px solid var(--border);
}
.drawer-links { padding: 12px; display: flex; flex-direction: column; gap: 4px; }
.drawer-link {
  display: flex; align-items: center; gap: 12px;
  padding: 12px 14px; border-radius: var(--radius-sm);
  font-size: 15px; font-weight: 500; color: var(--text-secondary);
  text-decoration: none; transition: all var(--transition);
}
.drawer-link:hover { color: var(--text-primary); background: var(--accent-light); }
.drawer-link.router-link-active { color: var(--accent); background: var(--accent-light); }
.drawer-link-icon { font-size: 18px; width: 24px; text-align: center; }

.desktop-only { display: flex; }
.mobile-only  { display: none; }

@media (max-width: 768px) {
  .desktop-only { display: none; }
  .mobile-only  { display: flex; }
}

.drawer-enter-active, .drawer-leave-active { transition: opacity 0.2s; }
.drawer-enter-from, .drawer-leave-to { opacity: 0; }
.drawer-enter-active .drawer, .drawer-leave-active .drawer { transition: transform 0.2s; }
.drawer-enter-from .drawer { transform: translateX(100%); }
.drawer-leave-to .drawer { transform: translateX(100%); }
</style>

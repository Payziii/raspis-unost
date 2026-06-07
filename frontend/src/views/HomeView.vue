<template>
  <div class="home">
    <div class="home-card">
      <div class="home-logo">
        <img src="/icons/icon.svg" alt="" class="home-logo-img" />
        <span class="home-logo-text">Расписание</span>
      </div>
      <p class="home-subtitle">Выберите режим просмотра</p>
      <div class="home-actions">
        <RouterLink to="/student" class="entry-btn entry-student">
          <span class="entry-icon">🎓</span>
          <span class="entry-label">Студентам</span>
          <span class="entry-desc">Посмотреть расписание своей группы</span>
        </RouterLink>
        <RouterLink to="/schedule" class="entry-btn entry-admin">
          <span class="entry-icon">🛠</span>
          <span class="entry-label">Диспетчеру</span>
          <span class="entry-desc">Управление расписанием</span>
        </RouterLink>
      </div>
    </div>
  </div>
</template>

<script setup>
</script>

<style scoped>
.home {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
}

.home-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 24px;
  max-width: 480px;
  width: 100%;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg, 16px);
  padding: 48px 40px;
  box-shadow: 0 8px 40px rgba(0, 0, 0, 0.4);
}

.home-logo {
  display: flex;
  align-items: center;
  gap: 12px;
}
.home-logo-img {
  width: 40px;
  height: 40px;
  border-radius: 10px;
}
.home-logo-text {
  font-size: 22px;
  font-weight: 800;
  color: var(--text-primary);
  letter-spacing: -0.02em;
}

.home-subtitle {
  margin: 0;
  font-size: 14px;
  color: var(--text-muted);
}

.home-actions {
  display: flex;
  flex-direction: column;
  gap: 12px;
  width: 100%;
}

.entry-btn {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 4px;
  padding: 20px 24px;
  border-radius: var(--radius);
  border: 1px solid var(--border);
  text-decoration: none;
  transition: all var(--transition);
  background: var(--bg-tertiary);
}
.entry-btn:hover {
  border-color: var(--accent);
  background: var(--accent-light);
  transform: translateY(-1px);
  box-shadow: 0 4px 16px rgba(99, 102, 241, 0.2);
}

.entry-icon { font-size: 28px; line-height: 1; margin-bottom: 4px; }
.entry-label { font-size: 16px; font-weight: 700; color: var(--text-primary); }
.entry-desc { font-size: 12px; color: var(--text-muted); }

.entry-student:hover .entry-label { color: var(--accent); }
.entry-admin:hover .entry-label { color: var(--accent); }

@media (max-width: 480px) {
  .home-card { padding: 32px 24px; }
}
</style>

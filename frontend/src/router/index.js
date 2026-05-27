import { createRouter, createWebHistory } from 'vue-router'
import ScheduleView from '../views/ScheduleView.vue'
import TeachersView from '../views/TeachersView.vue'
import GroupsView from '../views/GroupsView.vue'
import LessonsView from '../views/LessonsView.vue'
import SettingsView from '../views/SettingsView.vue'
import ConstructorView from '../views/ConstructorView.vue'

const routes = [
  { path: '/', redirect: '/schedule' },
  { path: '/schedule', component: ScheduleView, meta: { title: 'Расписание' } },
  { path: '/constructor', component: ConstructorView, meta: { title: 'Конструктор' } },
  { path: '/teachers', component: TeachersView, meta: { title: 'Преподаватели' } },
  { path: '/groups', component: GroupsView, meta: { title: 'Группы' } },
  { path: '/lessons', component: LessonsView, meta: { title: 'Пары' } },
  { path: '/settings', component: SettingsView, meta: { title: 'Настройки' } },
]

const router = createRouter({ history: createWebHistory(), routes })
router.afterEach((to) => {
  document.title = to.meta.title ? `${to.meta.title} — Расписание` : 'Расписание'
})
export default router

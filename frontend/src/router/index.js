import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import StudentView from '../views/StudentView.vue'
import ScheduleView from '../views/ScheduleView.vue'
import TeachersView from '../views/TeachersView.vue'
import GroupsView from '../views/GroupsView.vue'
import LessonsView from '../views/LessonsView.vue'
import SettingsView from '../views/SettingsView.vue'
import ConstructorView from '../views/ConstructorView.vue'

const routes = [
  { path: '/', component: HomeView, meta: { title: 'Выбор режима', hideNav: true } },
  { path: '/student', component: StudentView, meta: { title: 'Расписание', hideNav: true } },
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

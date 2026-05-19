<template>
  <div class="app-root">
    <Navbar />
    <main class="main-content">
      <RouterView />
    </main>
    <Toast />
  </div>
</template>

<script setup>
import Navbar from './components/Navbar.vue'
import Toast from './components/Toast.vue'
</script>

<style scoped>
.app-root { min-height: 100vh; display: flex; flex-direction: column; }
.main-content { flex: 1; display: flex; flex-direction: column; }
</style>

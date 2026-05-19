const BASE = '/api'

async function request(method, path, body = null) {
  const opts = { method, headers: { 'Content-Type': 'application/json; charset=utf-8' } }
  if (body !== null) opts.body = JSON.stringify(body)
  try {
    const res = await fetch(BASE + path, opts)
    const text = await res.text()
    let data
    try { data = JSON.parse(text) } catch { data = { message: text } }
    return { ok: res.ok, status: res.status, data }
  } catch {
    return { ok: false, status: 0, data: { message: 'Нет связи с сервером' } }
  }
}

export const api = {
  schedule: {
    get: () => request('GET', '/schedule'),
    getGroup: (id) => request('GET', `/schedule/group/${encodeURIComponent(id)}`),
    regenerate: () => request('POST', '/schedule/regenerate'),
  },
  groups: {
    list: () => request('GET', '/groups'),
    create: (d) => request('POST', '/groups', d),
    update: (id, d) => request('PUT', `/groups/${id}`, d),
    remove: (id) => request('DELETE', `/groups/${id}`),
  },
  teachers: {
    list: () => request('GET', '/teachers'),
    create: (d) => request('POST', '/teachers', d),
    update: (id, d) => request('PUT', `/teachers/${id}`, d),
    remove: (id) => request('DELETE', `/teachers/${id}`),
  },
  lessons: {
    list: () => request('GET', '/lessons'),
    create: (d) => request('POST', '/lessons', d),
    update: (id, d) => request('PUT', `/lessons/${id}`, d),
    remove: (id) => request('DELETE', `/lessons/${id}`),
  },
  unavailable: {
    list: () => request('GET', '/unavailable'),
    create: (d) => request('POST', '/unavailable', d),
    update: (id, d) => request('PUT', `/unavailable/${id}`, d),
    remove: (id) => request('DELETE', `/unavailable/${id}`),
  },
  settings: {
    get: () => request('GET', '/settings'),
    update: (d) => request('PATCH', '/settings', d),
  },
}

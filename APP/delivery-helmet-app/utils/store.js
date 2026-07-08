import { reactive } from 'vue'


const store = reactive({
  // 订单列表
  // status说明：
  //   pending_pickup = 待取餐（去餐馆取）
  //   delivering     = 配送中（已取餐，去客户地址）
  //   arriving       = 即将到达（距目的地50米内，LED闪烁）
  //   completed      = 已送达
  // 订单列表：以管理员网页为数据源，APP启动后通过MQTT自动同步
  // 网页发布时带 retain=true，APP连上MQTT立刻收到最新数据，无需硬编码
  orders: [],

  // 硬件数据
  device: {
    // FL=前左, FR=前右, BL=后左, BR=后右（箱体四角温度传感器）
    // 初始为0，显示"--"；收到STM32数据后更新，不做高低评价
    temperatures: { 'FL': 0, 'FR': 0, 'BL': 0, 'BR': 0 },
    antiTheft: 'normal',
    fallDetect: 'normal',
    ledBindings: { '01': null, '02': null, '03': null, '04': null },
    speed: 0,           // 骑手当前速度（km/h），来自STM32的GPS模块
    connected: false,
    mockMode: false,
    lastUpdate: null
  },

  // 顶部通知 Toast 队列（替代原来的 uni.showModal 弹窗）
  toasts: [],

  // 跌倒专属全屏警报（与普通 toast 区分，优先级最高）
  fallAlert: { visible: false },

  // 保留 alarm.show 供 device.vue resetAll() 兼容使用（不显示UI，仅内部状态）
  alarm: { show: false, title: '', message: '', level: 'danger' },

  // 骑手位置（长江大学新风学苑）
  riderLocation: { lat: 30.335483, lng: 112.215560 },

  // 导航状态（map.vue 规划路线后写入，App.vue 的 GPS 回调实时推进）
  // navSteps：当前路段每一步的 {instruction, distance, endLat, endLng}
  // navStepIndex：当前走到第几步（到达步骤终点时 +1）
  // navTarget：当前导航目标信息，{place, type} type='pickup'|'deliver'
  navSteps: [],
  navStepIndex: 0,
  navTarget: null
})

// 触发通知：自动判断当前页面，地图页用系统原生弹窗（可覆盖原生地图组件），其他页用自定义Toast
store.triggerAlarm = function(title, message, level) {
  level = level || 'danger'

  // 震动反馈
  if (level === 'danger') { try { uni.vibrateLong() } catch(e) {} }
  else                    { try { uni.vibrateShort() } catch(e) {} }

  // 从标题中提取 emoji（标题格式均为 "emoji 文字"）
  const spaceIdx  = title.indexOf(' ')
  const emoji     = spaceIdx > 0 ? title.slice(0, spaceIdx) : '🔔'
  const titleText = spaceIdx > 0 ? title.slice(spaceIdx + 1) : title

  // 检测是否在地图页（<map> 是原生组件，WebView层的固定弹窗会被压在下面）
  let onMapPage = false
  try {
    const pages = getCurrentPages()
    if (pages && pages.length > 0) {
      const cur = pages[pages.length - 1]
      onMapPage = !!(cur && cur.route && cur.route.indexOf('map/map') !== -1)
    }
  } catch(e) {}

  if (onMapPage) {
    // ===== 地图页：系统原生弹窗，凌驾于所有原生组件之上 =====
    if (level === 'danger') {
      // 危险级：showModal 强制骑手确认
      uni.showModal({
        title:        emoji + ' ' + titleText,
        content:      message || '',
        showCancel:   false,
        confirmText:  '知道了',
        confirmColor: '#f44336'
      })
    } else {
      // 普通提示：showToast 轻量非阻塞
      uni.showToast({
        title:    emoji + ' ' + titleText,
        icon:     'none',
        duration: level === 'warning' ? 4000 : 3000
      })
    }
  } else {
    // ===== 其他页：自定义顶部Toast动画卡片 =====
    const duration = level === 'danger' ? 8000 : level === 'warning' ? 5000 : 4000
    const id = Date.now() + '_' + Math.random().toString(36).slice(2, 7)

    store.toasts.push({ id, emoji, titleText, message, level, duration, hiding: false })

    // 最多同时显示 3 条，超出移除最旧的
    if (store.toasts.length > 3) store.toasts.splice(0, 1)

    // 到时间后触发滑出动画，再 350ms 后从数组移除
    setTimeout(() => {
      const t = store.toasts.find(t => t.id === id)
      if (t && !t.hiding) {
        t.hiding = true
        setTimeout(() => {
          const idx = store.toasts.findIndex(t => t.id === id)
          if (idx !== -1) store.toasts.splice(idx, 1)
        }, 350)
      }
    }, duration)
  }

  // 兼容旧代码（device.vue resetAll 依赖 alarm.show）
  store.alarm.show    = true
  store.alarm.title   = title
  store.alarm.message = message
  store.alarm.level   = level
}

// ===== 跌倒专属全屏警报 =====
store.triggerFallAlert = function() {
  store.fallAlert.visible = true
  // 每2秒长震动一次，直到手动解除
  try { uni.vibrateLong() } catch(e) {}
  store._fallVibTimer = setInterval(() => {
    if (!store.fallAlert.visible) { clearInterval(store._fallVibTimer); return }
    try { uni.vibrateLong() } catch(e) {}
  }, 2000)
}
store.closeFallAlert = function() {
  store.fallAlert.visible = false
  if (store._fallVibTimer) { clearInterval(store._fallVibTimer); store._fallVibTimer = null }
  store.device.fallDetect = 'normal'
  store.alarm.show = false
}

// 关闭所有通知（设备重置时调用）
store.closeAlarm = function() {
  store.alarm.show = false
  store.toasts.forEach(t => { t.hiding = true })
  setTimeout(() => { store.toasts.splice(0, store.toasts.length) }, 350)
}

// 更新硬件数据
store.updateDeviceData = function(data) {
  if (data.temperatures) {
    for (const k in data.temperatures) {
      store.device.temperatures[k] = data.temperatures[k]
    }
  }
  if (data.antiTheft !== undefined) {
    store.device.antiTheft = data.antiTheft
    if (data.antiTheft === 'alarm') store.triggerAlarm('🚨 防盗报警', '外卖箱被异常打开！')
  }
  if (data.fallDetect !== undefined) {
    store.device.fallDetect = data.fallDetect
    if (data.fallDetect === 'alarm') store.triggerFallAlert()  // 跌倒：全屏专属警报
  }
  store.device.lastUpdate = new Date().toLocaleTimeString()
}

// 按下单时间排序，同时计算骑手到取餐点的距离
store.sortOrders = function() {
  store.orders.sort((a, b) => a.createdAt - b.createdAt)
  store.orders.forEach((o, i) => { o.sortOrder = i + 1 })
  // 计算骑手到取餐点的距离
  const { lat, lng } = store.riderLocation
  store.orders.forEach(o => {
    const targetLat = o.status === 'pending_pickup' ? o.shopLat : o.lat
    const targetLng = o.status === 'pending_pickup' ? o.shopLng : o.lng
    const dLat = (targetLat - lat) * Math.PI / 180
    const dLng = (targetLng - lng) * Math.PI / 180
    const a = Math.sin(dLat/2)**2 + Math.cos(lat*Math.PI/180)*Math.cos(targetLat*Math.PI/180)*Math.sin(dLng/2)**2
    o.distance = Math.round(6371000 * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a)))
  })
}

// 距离计算工具（Haversine公式，单位：米）
store._calcDist = function(lat1, lng1, lat2, lng2) {
  const R = 6371000
  const dLat = (lat2 - lat1) * Math.PI / 180
  const dLng = (lng2 - lng1) * Math.PI / 180
  const a = Math.sin(dLat/2)**2 + Math.cos(lat1*Math.PI/180)*Math.cos(lat2*Math.PI/180)*Math.sin(dLng/2)**2
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a))
}

// NFC绑定：STM32刷到哪个卡，就匹配预分配了该clipId的待取餐订单
// 订单创建时已固定分配 clipId（如鸡块=01号），刷01号卡就取鸡块
store.bindNfc = function(clipId) {
  // 直接找 clipId 匹配的待取餐订单（不再用距离判断）
  const order = store.orders.find(o => o.clipId === clipId && o.status === 'pending_pickup')
  if (!order) return null

  order.clipId_bound = clipId
  order.status = 'delivering'
  store.device.ledBindings[clipId] = order.id
  store.triggerAlarm('📦 取餐成功', order.foodName + ' 已取餐，送往：' + order.address, 'info')
  return order
}

// NFC解绑：STM32已确认位置，APP直接解绑并完成订单
store.unbindNfc = function(clipId) {
  const order = store.orders.find(o => o.clipId_bound === clipId &&
    (o.status === 'delivering' || o.status === 'arriving'))
  if (!order) return null

  order.status = 'completed'
  order.clipId_bound = null
  store.device.ledBindings[clipId] = null
  store.triggerAlarm('✅ 送达成功', order.foodName + ' 已送达 ' + order.address + '！', 'info')
  // 3秒后从列表移除（与网页行为一致）
  setTimeout(() => {
    const idx = store.orders.indexOf(order)
    if (idx !== -1) store.orders.splice(idx, 1)
  }, 3000)
  return order
}

export default store

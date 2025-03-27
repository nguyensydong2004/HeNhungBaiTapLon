// Firebase configuration
const firebaseConfig = {
  apiKey: "AIzaSyAbzh3_KMKoOOBTODMNuPDzWvqJAz2MNfI",
  authDomain: "henhung-3c03a.firebaseapp.com",
  databaseURL: "https://henhung-3c03a-default-rtdb.firebaseio.com",
  projectId: "henhung-3c03a",
  storageBucket: "henhung-3c03a.firebasestorage.app",
  messagingSenderId: "571696059482",
  appId: "1:571696059482:web:3aed9a78d194764540b463"
};

// Initialize Firebase
firebase.initializeApp(firebaseConfig);
const db = firebase.database();

// Global variables
let editingScheduleId = null;
let lastCheckedMinute = -1;
let currentPage = '';

// Main initialization
document.addEventListener('DOMContentLoaded', () => {
  currentPage = window.location.pathname.split('/').pop();
  
  initFirebaseListeners();
  setupEventHandlers();
  updateDateTime();
  
  // Set up intervals
  setInterval(updateDateTime, 1000);
  setInterval(checkSchedules, 10000); // Check schedules every 10 seconds
  
  // Page specific initialization
  switch(currentPage) {
    case 'schedule.html':
      loadSchedules();
      setupScheduleForm();
      addDaysSelectionToForm();
      break;
    case 'history.html':
      loadHistory();
      setupHistoryFilters();
      break;
    case 'settings.html':
      loadSettings();
      setupSettingsForms();
      break;
    default: // index.html
      break;
  }
});

// ==================== FIREBASE LISTENERS ====================
function initFirebaseListeners() {
  // Food level listener
  db.ref('/sensors/food_level').on('value', snap => {
    const level = snap.val() || 0;
    updateFoodLevel(level);
    
    const threshold = parseInt(localStorage.getItem('lowFoodThreshold')) || 20;
    if (level < threshold) {
      showAlert('lowFoodAlert');
      showToast('CẢNH BÁO: Thức ăn sắp hết! Vui lòng bổ sung.', 'error');
    } else {
      hideAlert('lowFoodAlert');
    }
  });

  // Last fed time listener
  db.ref('/sensors/last_fed').on('value', snap => {
    const time = snap.val() || '--:--';
    const timeElement = document.getElementById('lastFedTime');
    if (timeElement) {
      timeElement.textContent = `Lần cuối: ${time.split(' ')[1] || '--:--'}`;
    }
  });

  // Connection status listener
  db.ref('/sensors/connection_status').on('value', snap => {
    const status = snap.val() === true ? 'Online' : 'Offline';
    updateConnectionStatus(status);
  });

  // LCD display listener
  db.ref('/display').on('value', snap => {
    const display = snap.val() || {};
    const line1Element = document.getElementById('lcdLine1');
    const line2Element = document.getElementById('lcdLine2');
    
    if (line1Element) line1Element.textContent = display.lcd_line1 || '';
    if (line2Element) line2Element.textContent = display.lcd_line2 || '';
  });

  // Settings listener
  db.ref('/settings').on('value', snap => {
    const settings = snap.val() || {};
    const feedDurationElement = document.getElementById('feedDuration');
    const lowFoodThresholdElement = document.getElementById('lowFoodThreshold');
    
    if (feedDurationElement && settings.feed_duration) {
      feedDurationElement.value = settings.feed_duration;
    }
    if (lowFoodThresholdElement && settings.low_food_threshold) {
      lowFoodThresholdElement.value = settings.low_food_threshold;
      localStorage.setItem('lowFoodThreshold', settings.low_food_threshold);
    }
  });
}

// ==================== EVENT HANDLERS ====================
function setupEventHandlers() {
  // Feed now button
  const feedNowBtn = document.getElementById('feedNow');
  if (feedNowBtn) {
    feedNowBtn.addEventListener('click', handleFeedNow);
  }

  // Update LCD button
  const updateLcdBtn = document.getElementById('updateLCD');
  if (updateLcdBtn) {
    updateLcdBtn.addEventListener('click', handleUpdateLCD);
  }

  // Refresh button
  const refreshBtn = document.getElementById('refreshBtn');
  if (refreshBtn) {
    refreshBtn.addEventListener('click', handleRefresh);
  }

  // Clear history button
  const clearHistoryBtn = document.getElementById('clearHistoryBtn');
  if (clearHistoryBtn) {
    clearHistoryBtn.addEventListener('click', handleClearHistory);
  }

  // Alert close buttons
  document.querySelectorAll('.alert-close').forEach(btn => {
    btn.addEventListener('click', function() {
      this.closest('.alert').style.display = 'none';
    });
  });
}

async function handleFeedNow() {
  const feedNowBtn = document.getElementById('feedNow');
  try {
    feedNowBtn.disabled = true;
    feedNowBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Đang cho ăn...';
    
    const feedTimestamp = new Date().toISOString();
    
    // Send feed command
    await db.ref('/controls').update({
      manual_feed: true,
      feed_timestamp: feedTimestamp,
      request_time: firebase.database.ServerValue.TIMESTAMP
    });
    
    // Add to history
    await db.ref('/history').push().set({
      timestamp: feedTimestamp,
      type: 'manual',
      portion: 1,
      status: 'completed'
    });
    
    // Update last fed time
    await db.ref('/sensors').update({
      last_fed: new Date().toLocaleString('vi-VN')
    });
    
    showToast('Đã gửi lệnh cho ăn thành công', 'success');
  } catch (error) {
    console.error('Lỗi khi cho ăn:', error);
    showToast(`Lỗi: ${error.message}`, 'error');
  } finally {
    setTimeout(() => {
      if (feedNowBtn) {
        feedNowBtn.disabled = false;
        feedNowBtn.innerHTML = '<i class="fas fa-utensil-spoon"></i> Cho ăn ngay';
      }
    }, 3000);
  }
}

async function handleUpdateLCD() {
  try {
    const line1 = document.getElementById('lcdInput1')?.value.trim() || ' ';
    const line2 = document.getElementById('lcdInput2')?.value.trim() || ' ';
    
    if (line1.length > 16 || line2.length > 16) {
      throw new Error('Mỗi dòng tối đa 16 ký tự');
    }
    
    await db.ref('/display').update({
      lcd_line1: line1,
      lcd_line2: line2,
      last_updated: firebase.database.ServerValue.TIMESTAMP
    });
    
    showToast('Cập nhật LCD thành công!', 'success');
    
    document.getElementById('lcdInput1').value = '';
    document.getElementById('lcdInput2').value = '';
  } catch (error) {
    console.error('Lỗi cập nhật LCD:', error);
    showToast(`Lỗi LCD: ${error.message}`, 'error');
  }
}

function handleRefresh() {
  const refreshBtn = document.getElementById('refreshBtn');
  if (refreshBtn) {
    refreshBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Đang tải...';
    setTimeout(() => {
      window.location.reload();
    }, 500);
  }
}

async function handleClearHistory() {
  if (confirm('Bạn có chắc muốn xóa toàn bộ lịch sử?')) {
    try {
      await db.ref('/history').remove();
      showToast('Đã xóa lịch sử thành công', 'success');
    } catch (error) {
      showToast(`Lỗi khi xóa lịch sử: ${error.message}`, 'error');
    }
  }
}

// ==================== SCHEDULE FUNCTIONS ====================
function loadSchedules() {
  db.ref('/schedule').on('value', snap => {
    const schedules = snap.val() || {};
    const container = document.getElementById('schedulesList');
    if (!container) return;
    
    container.innerHTML = '';
    
    if (Object.keys(schedules).length === 0) {
      container.innerHTML = `
        <div class="empty-state">
          <i class="far fa-calendar-plus"></i>
          <p>Chưa có lịch trình nào</p>
          <p>Thêm lịch trình để hệ thống tự động cho thú cưng ăn</p>
        </div>
      `;
      return;
    }
    
    const dayNames = ['CN', 'T2', 'T3', 'T4', 'T5', 'T6', 'T7'];
    
    Object.entries(schedules).forEach(([key, schedule]) => {
      const item = document.createElement('div');
      item.className = 'schedule-item';
      
      const time = `${String(schedule.hour).padStart(2, '0')}:${String(schedule.minute).padStart(2, '0')}`;
      const portionText = ['Nhỏ', 'Vừa', 'Lớn'][schedule.portion - 1] || 'Nhỏ';
      const daysText = schedule.days?.map(d => dayNames[d]).join(', ') || 'Hàng ngày';
      
      item.innerHTML = `
        <div>
          <span class="schedule-time">${time}</span>
          <span class="schedule-portion">${portionText}</span>
          <span class="schedule-days">(${daysText})</span>
          ${schedule.enabled ? 
            '<i class="fas fa-check-circle enabled-icon"></i>' : 
            '<i class="fas fa-times-circle disabled-icon"></i>'}
        </div>
        <div class="schedule-actions">
          <button class="btn-edit" data-id="${key}"><i class="fas fa-edit"></i></button>
          <button class="btn-delete" data-id="${key}"><i class="fas fa-trash"></i></button>
        </div>
      `;
      
      container.appendChild(item);
    });
    
    // Update schedule count
    const scheduleCount = document.getElementById('scheduleCount');
    if (scheduleCount) {
      scheduleCount.textContent = `${Object.keys(schedules).length} lịch trình`;
    }
    
    // Add event listeners to edit/delete buttons
    document.querySelectorAll('.btn-edit').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const id = e.target.closest('button').dataset.id;
        editSchedule(id);
      });
    });
    
    document.querySelectorAll('.btn-delete').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const id = e.target.closest('button').dataset.id;
        deleteSchedule(id);
      });
    });
  });
}

function setupScheduleForm() {
  const form = document.getElementById('addScheduleForm');
  if (!form) return;
  
  const submitBtn = form.querySelector('button[type="submit"]');
  const timeInput = document.getElementById('scheduleTime');
  const portionSelect = document.getElementById('schedulePortion');
  const enabledCheckbox = document.getElementById('scheduleEnabled');
  
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    const dayCheckboxes = document.querySelectorAll('input[name="scheduleDay"]:checked');
    const selectedDays = Array.from(dayCheckboxes).map(cb => parseInt(cb.value));
    
    if (selectedDays.length === 0) {
      showToast('Vui lòng chọn ít nhất một ngày', 'error');
      return;
    }
    
    const [hours, minutes] = timeInput.value.split(':');
    if (!hours || !minutes) {
      showToast('Vui lòng nhập thời gian hợp lệ', 'error');
      return;
    }
    
    const newSchedule = {
      hour: parseInt(hours),
      minute: parseInt(minutes),
      portion: parseInt(portionSelect.value),
      enabled: enabledCheckbox.checked,
      days: selectedDays
    };
    
    try {
      if (editingScheduleId) {
        // Update existing schedule
        await db.ref(`/schedule/${editingScheduleId}`).update(newSchedule);
        showToast('Đã cập nhật lịch trình', 'success');
      } else {
        // Add new schedule
        await db.ref('/schedule').push(newSchedule);
        showToast('Đã thêm lịch trình mới', 'success');
      }
      resetScheduleForm();
    } catch (error) {
      showToast(`Lỗi: ${error.message}`, 'error');
    }
  });
  
  function resetScheduleForm() {
    if (timeInput) timeInput.value = '';
    if (portionSelect) portionSelect.value = '2';
    if (enabledCheckbox) enabledCheckbox.checked = true;
    
    // Reset day checkboxes
    document.querySelectorAll('input[name="scheduleDay"]').forEach(checkbox => {
      checkbox.checked = true;
    });
    
    editingScheduleId = null;
    if (submitBtn) {
      submitBtn.innerHTML = '<i class="fas fa-plus"></i> Thêm lịch trình';
    }
  }
}

function editSchedule(id) {
  db.ref(`/schedule/${id}`).once('value').then(snap => {
    const schedule = snap.val();
    if (!schedule) return;
    
    editingScheduleId = id;
    
    const timeInput = document.getElementById('scheduleTime');
    const portionSelect = document.getElementById('schedulePortion');
    const enabledCheckbox = document.getElementById('scheduleEnabled');
    const submitBtn = document.getElementById('addScheduleForm')?.querySelector('button[type="submit"]');
    
    if (timeInput) {
      timeInput.value = `${String(schedule.hour).padStart(2, '0')}:${String(schedule.minute).padStart(2, '0')}`;
    }
    if (portionSelect) {
      portionSelect.value = schedule.portion;
    }
    if (enabledCheckbox) {
      enabledCheckbox.checked = schedule.enabled;
    }
    if (submitBtn) {
      submitBtn.innerHTML = '<i class="fas fa-save"></i> Lưu thay đổi';
    }
    
    // Set day checkboxes
    document.querySelectorAll('input[name="scheduleDay"]').forEach(checkbox => {
      checkbox.checked = schedule.days?.includes(parseInt(checkbox.value)) || false;
    });
    
    // Scroll to form
    document.getElementById('addScheduleForm')?.scrollIntoView({ behavior: 'smooth' });
  }).catch(error => {
    showToast('Lỗi khi tải lịch trình: ' + error.message, 'error');
  });
}

function deleteSchedule(id) {
  if (confirm('Bạn có chắc chắn muốn xóa lịch trình này?')) {
    db.ref(`/schedule/${id}`).remove()
      .then(() => {
        showToast('Đã xóa lịch trình', 'success');
      })
      .catch(error => {
        showToast('Lỗi khi xóa lịch trình: ' + error.message, 'error');
      });
  }
}

// ==================== HISTORY FUNCTIONS ====================
function loadHistory(filter = {}) {
  db.ref('/history').orderByChild('timestamp').on('value', snap => {
    const history = snap.val() || {};
    const tbody = document.getElementById('historyTableBody');
    if (!tbody) return;
    
    tbody.innerHTML = '';
    
    let filteredData = Object.entries(history);
    
    // Apply filters
    if (filter.date) {
      const filterDateStr = new Date(filter.date).toLocaleDateString('vi-VN');
      filteredData = filteredData.filter(([_, record]) => {
        const recordDate = new Date(record.timestamp).toLocaleDateString('vi-VN');
        return recordDate === filterDateStr;
      });
    }
    
    if (filter.type && filter.type !== 'all') {
      filteredData = filteredData.filter(([_, record]) => record.type === filter.type);
    }
    
    // Sort by timestamp (newest first)
    filteredData.sort((a, b) => b[1].timestamp.localeCompare(a[1].timestamp));
    
    if (filteredData.length === 0) {
      tbody.innerHTML = `
        <tr class="empty-row">
          <td colspan="4">
            <div class="empty-state">
              <i class="far fa-calendar-times"></i>
              <p>Không tìm thấy bản ghi nào phù hợp</p>
            </div>
          </td>
        </tr>
      `;
      return;
    }
    
    filteredData.forEach(([_, record]) => {
      const row = document.createElement('tr');
      const dateTime = record.timestamp ? new Date(record.timestamp) : new Date();
      const date = dateTime.toLocaleDateString('vi-VN');
      const time = dateTime.toLocaleTimeString('vi-VN');
      const typeText = record.type === 'manual' ? 'Thủ công' : 'Tự động';
      const portionText = ['Nhỏ', 'Vừa', 'Lớn'][record.portion - 1] || 'Nhỏ';
      
      row.innerHTML = `
        <td>${date} ${time}</td>
        <td>${typeText}</td>
        <td>${portionText}</td>
        <td>${record.status || '--'}</td>
      `;
      
      tbody.appendChild(row);
    });
    
    // Update history count
    const historyCount = document.getElementById('historyCount');
    if (historyCount) {
      historyCount.textContent = `${filteredData.length} bản ghi`;
    }
  });
}

function setupHistoryFilters() {
  const applyFilterBtn = document.getElementById('applyFilter');
  if (applyFilterBtn) {
    applyFilterBtn.addEventListener('click', () => {
      const dateFilter = document.getElementById('filterDate')?.value;
      const typeFilter = document.getElementById('filterType')?.value;
      
      loadHistory({
        date: dateFilter,
        type: typeFilter
      });
    });
  }
}

// ==================== SETTINGS FUNCTIONS ====================
function loadSettings() {
  // Settings are loaded in the main firebase listener
}

function setupSettingsForms() {
  // Network settings form
  const networkForm = document.getElementById('networkSettingsForm');
  if (networkForm) {
    networkForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      const ssid = document.getElementById('wifiSSID')?.value;
      const password = document.getElementById('wifiPassword')?.value;
      
      if (!ssid) {
        showToast('Vui lòng nhập tên mạng WiFi', 'error');
        return;
      }
      
      try {
        await db.ref('/settings/wifi').update({
          ssid: ssid,
          password: password || ''
        });
        showToast('Đã lưu cài đặt mạng', 'success');
      } catch (error) {
        showToast('Lỗi khi lưu cài đặt mạng: ' + error.message, 'error');
      }
    });
  }
  
  // Food settings form
  const foodForm = document.getElementById('foodSettingsForm');
  if (foodForm) {
    foodForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      
      const feedDuration = parseInt(document.getElementById('feedDuration')?.value) || 2000;
      const lowFoodThreshold = parseInt(document.getElementById('lowFoodThreshold')?.value) || 20;
      
      try {
        await db.ref('/settings').update({
          feed_duration: feedDuration,
          low_food_threshold: lowFoodThreshold
        });
        showToast('Đã lưu cài đặt thức ăn', 'success');
      } catch (error) {
        showToast('Lỗi khi lưu cài đặt: ' + error.message, 'error');
      }
    });
  }
  
  // Check update button
  const checkUpdateBtn = document.getElementById('checkUpdate');
  if (checkUpdateBtn) {
    checkUpdateBtn.addEventListener('click', () => {
      showToast('Đang kiểm tra cập nhật...', 'info');
      setTimeout(() => {
        showToast('Bạn đang sử dụng phiên bản mới nhất', 'success');
      }, 1500);
    });
  }
  
  // Restart device button
  const restartDeviceBtn = document.getElementById('restartDevice');
  if (restartDeviceBtn) {
    restartDeviceBtn.addEventListener('click', () => {
      if (confirm('Bạn có chắc muốn khởi động lại thiết bị?')) {
        showToast('Đang khởi động lại thiết bị...', 'info');
        db.ref('/controls').update({
          restart: true
        }).then(() => {
          setTimeout(() => {
            showToast('Thiết bị đang khởi động lại', 'success');
          }, 1000);
        }).catch(error => {
          showToast('Lỗi khi khởi động lại: ' + error.message, 'error');
        });
      }
    });
  }
}

// ==================== SCHEDULE CHECKER ====================
async function checkSchedules() {
  const now = new Date();
  const currentMinute = now.getMinutes();
  
  // Only check when minute changes to avoid duplicates
  if (currentMinute === lastCheckedMinute) return;
  
  lastCheckedMinute = currentMinute;
  const currentDay = now.getDay();
  const currentHour = now.getHours();
  const currentTimeStr = now.toISOString();

  try {
    const snapshot = await db.ref('/schedule').once('value');
    const schedules = snapshot.val() || {};

    for (const [id, schedule] of Object.entries(schedules)) {
      if (schedule.enabled && 
          schedule.days.includes(currentDay) &&
          schedule.hour === currentHour &&
          schedule.minute === currentMinute) {
        
        // Check if this schedule has already been executed
        const historySnapshot = await db.ref('/history')
          .orderByChild('schedule_id')
          .equalTo(id)
          .limitToLast(1)
          .once('value');

        let shouldExecute = true;
        
        if (historySnapshot.exists()) {
          const lastExecution = Object.values(historySnapshot.val())[0];
          const lastExecTime = new Date(lastExecution.timestamp);
          const diffMinutes = (now - lastExecTime) / (1000 * 60);
          
          // If executed within the last minute, skip
          if (diffMinutes < 1) {
            shouldExecute = false;
          }
        }

        if (shouldExecute) {
          console.log('Executing schedule:', id);
          
          // Activate feeding
          await db.ref('/controls').update({
            auto_feed: true,
            feed_timestamp: currentTimeStr
          });

          // Add to history
          await db.ref('/history').push().set({
            timestamp: currentTimeStr,
            type: 'schedule',
            portion: schedule.portion,
            schedule_id: id,
            status: 'completed'
          });

          // Update last fed time
          await db.ref('/sensors').update({
            last_fed: now.toLocaleString('vi-VN')
          });

          console.log('Schedule executed and logged');
        }
      }
    }
  } catch (error) {
    console.error('Error checking schedules:', error);
  }
}

// ==================== UTILITY FUNCTIONS ====================
function updateFoodLevel(level) {
  const foodLevelBar = document.getElementById('foodLevelBar');
  const foodLevelText = document.getElementById('foodLevelText');
  
  if (foodLevelBar && foodLevelText) {
    const clampedLevel = Math.max(0, Math.min(100, level));
    foodLevelBar.style.width = `${clampedLevel}%`;
    foodLevelText.textContent = `${clampedLevel}%`;
    
    if (clampedLevel < 10) {
      foodLevelBar.style.backgroundColor = 'var(--danger)';
    } else if (clampedLevel < 30) {
      foodLevelBar.style.backgroundColor = 'var(--warning)';
    } else {
      foodLevelBar.style.backgroundColor = 'var(--primary)';
    }
  }
}

function updateConnectionStatus(status) {
  const connectionStatus = document.getElementById('connectionStatus');
  if (connectionStatus) {
    connectionStatus.className = `status ${status.toLowerCase()}`;
    connectionStatus.innerHTML = `<i class="fas fa-circle"></i> ${status}`;
  }
}

function updateDateTime() {
  const now = new Date();
  const timeElement = document.getElementById('currentTime');
  const dateElement = document.getElementById('currentDate');
  
  if (timeElement) {
    timeElement.textContent = now.toLocaleTimeString('vi-VN');
  }
  
  if (dateElement) {
    dateElement.textContent = now.toLocaleDateString('vi-VN');
  }
}

function showAlert(id) {
  const alert = document.getElementById(id);
  if (alert) alert.style.display = 'flex';
}

function hideAlert(id) {
  const alert = document.getElementById(id);
  if (alert) alert.style.display = 'none';
}

function showToast(message, type = 'info') {
  // Remove existing toasts
  document.querySelectorAll('.toast').forEach(toast => toast.remove());
  
  const toast = document.createElement('div');
  toast.className = `toast ${type}`;
  toast.textContent = message;
  document.body.appendChild(toast);
  
  setTimeout(() => {
    toast.classList.add('show');
  }, 10);
  
  setTimeout(() => {
    toast.classList.remove('show');
    setTimeout(() => {
      toast.remove();
    }, 300);
  }, 3000);
}

// Debug function
window.debugCheckSchedules = checkSchedules;
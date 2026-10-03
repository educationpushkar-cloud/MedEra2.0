const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const state = {
  latitude: null, longitude: null, emergencyLatitude: null, emergencyLongitude: null,
  hospitals: [], emergencyOnly: false, mapMode: false,
  favorites: (() => { try { return new Set(JSON.parse(localStorage.getItem('medera_favorites') || '[]')); } catch { return new Set(); } })(),
  staffToken: sessionStorage.getItem('medera_token') || '', dashboardTimer: null
};

function escapeHtml(value = '') {
  return String(value).replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[character]));
}
function formData(form) { return Object.fromEntries(new FormData(form).entries()); }
function queryString(values) {
  const params = new URLSearchParams();
  for (const [key, value] of Object.entries(values)) if (value !== '' && value !== null && value !== undefined) params.set(key, value);
  return params.toString();
}
async function api(path, { method = 'GET', data, auth = false } = {}) {
  const headers = {};
  const options = { method, headers, cache: 'no-store' };
  if (data) {
    headers['Content-Type'] = 'application/x-www-form-urlencoded;charset=UTF-8';
    options.body = new URLSearchParams(data).toString();
  }
  if (auth && state.staffToken) headers.Authorization = `Bearer ${state.staffToken}`;
  const response = await fetch(path, options);
  let result;
  try { result = await response.json(); } catch { throw new Error('The server returned an unreadable response.'); }
  if (!response.ok || result.error) throw new Error(result.error || `Request failed (${response.status}).`);
  return result;
}
function showToast(message, error = false) {
  const toast = $('#toast');
  toast.textContent = message;
  toast.classList.toggle('error-toast', error);
  toast.classList.add('visible');
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => toast.classList.remove('visible'), 3600);
}
function showFormMessage(form, message, error = false) {
  const target = $('.form-message', form);
  if (!target) return;
  target.textContent = message;
  target.classList.toggle('error', error);
}
function formatDate(value) {
  if (!value) return '';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : new Intl.DateTimeFormat(undefined, { day: 'numeric', month: 'short', year: 'numeric' }).format(date);
}
function formatTimestamp(value) {
  if (!value) return 'Not available';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : new Intl.DateTimeFormat(undefined, { day: 'numeric', month: 'short', hour: 'numeric', minute: '2-digit' }).format(date);
}
function timeAgo(value) {
  if (!value) return 'Updated recently';
  const date = new Date(value);
  const minutes = Math.max(0, Math.floor((Date.now() - date.getTime()) / 60000));
  if (Number.isNaN(minutes)) return 'Updated recently';
  if (minutes < 1) return 'Updated just now';
  if (minutes < 60) return `Updated ${minutes} min ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `Updated ${hours} hr${hours === 1 ? '' : 's'} ago`;
  return `Updated ${Math.floor(hours / 24)} day${hours >= 48 ? 's' : ''} ago`;
}
function statusLabel(status) {
  const labels = {
    requested: 'Awaiting hospital confirmation', confirmed: 'Confirmed', completed: 'Completed', cancelled: 'Cancelled',
    hospital_notified: 'Hospital notified', accepted: 'Accepted · bed reserved', callback_validated: 'Callback validated',
    ambulance_assigned: 'Ambulance assigned', dispatched: 'Dispatched', patient_picked_up: 'Patient picked up',
    hospital_reached: 'Reached hospital', no_hospital_available: 'No matching hospital available'
  };
  return labels[status] || status.replaceAll('_', ' ');
}
function renderHospitalCard(hospital, index) {
  const departments = (hospital.departments || []).slice(0, 3);
  const moreDepartments = Math.max(0, (hospital.departments || []).length - departments.length);
  const tel = String(hospital.phone || '').replace(/[^\d+]/g, '');
  const hasDepartments = (hospital.departments || []).length > 0;
  const saved = state.favorites.has(hospital.id);
  const rating = hospital.ratingCount ? `<span class="star" aria-hidden="true">★</span>${Number(hospital.rating).toFixed(1)}` : '<span>New</span>';
  const distance = hospital.distanceKm == null ? '' : `<span class="card-stat card-distance">${Number(hospital.distanceKm).toFixed(1)} km away</span>`;
  const coords = Number(hospital.latitude) !== 0 || Number(hospital.longitude) !== 0;
  const directions = coords ? `https://www.openstreetmap.org/directions?to=${encodeURIComponent(`${hospital.latitude},${hospital.longitude}`)}` : '';
  const bedsText = Number(hospital.availableBeds) === 1 ? 'bed' : 'beds';
  return `<article class="hospital-card" id="hospital-card-${escapeHtml(hospital.id)}">
    <div class="hospital-visual visual-${index % 5}" aria-label="Illustration of a hospital">
      <span class="visual-sun" aria-hidden="true"></span><div class="visual-building" aria-hidden="true"><span class="visual-roof"></span><span class="visual-windows"><i></i><i></i><i></i><i></i><i></i><i></i></span><span class="visual-door"></span></div><span class="visual-cross" aria-hidden="true">✚</span>
      <span class="listing-badge">Hospital-managed</span>
      <button class="save-hospital${saved ? ' saved' : ''}" type="button" data-save="${escapeHtml(hospital.id)}" aria-label="${saved ? 'Remove saved hospital' : 'Save hospital'}" aria-pressed="${saved}">♥</button>
      <span class="visual-caption">${escapeHtml(hospital.area || hospital.city)}</span>
    </div>
    <div class="hospital-card-content">
      <div class="hospital-title-row"><h2>${escapeHtml(hospital.name)}</h2><span class="card-rating">${rating}</span></div>
      <p class="card-location">⌖ ${escapeHtml([hospital.area, hospital.city].filter(Boolean).join(', '))}</p>
      <p class="card-address" title="${escapeHtml(hospital.address || '')}">${escapeHtml(hospital.address || 'Address details are being added by the hospital.')}</p>
      <div class="card-stats"><span class="card-stat"><strong>${Number(hospital.availableBeds) || 0}</strong> ${bedsText}</span><span class="card-stat-separator" aria-hidden="true"></span><span class="card-stat"><strong>${Number(hospital.availableAmbulances) || 0}</strong> ambulances</span>${distance}</div>
      <p class="card-departments">${hasDepartments ? departments.map(escapeHtml).join(' · ') + (moreDepartments ? ` · +${moreDepartments}` : '') : 'Hospital team is completing this listing.'}</p>
      <div class="hospital-card-actions"><button class="card-book" type="button" data-book="${escapeHtml(hospital.id)}" ${hasDepartments ? '' : 'disabled title="The hospital has not listed departments yet"'}>Request appointment <span aria-hidden="true">↗</span></button>${tel ? `<a class="card-call" href="tel:${escapeHtml(tel)}">Call</a>` : ''}${directions ? `<a class="card-call" href="${directions}" target="_blank" rel="noopener noreferrer">Directions</a>` : ''}</div>
      <p class="hospital-updated">${escapeHtml(timeAgo(hospital.updatedAt))} · Confirm details by phone</p>
    </div>
  </article>`;
}
function setResultsView(mapMode) {
  state.mapMode = mapMode;
  $('#hospitalResults').hidden = mapMode || state.hospitals.length === 0;
  $('#mapView').hidden = !mapMode || state.hospitals.length === 0;
  $('#listViewButton').classList.toggle('active', !mapMode);
  $('#mapViewButton').classList.toggle('active', mapMode);
  $('#listViewButton').setAttribute('aria-pressed', String(!mapMode));
  $('#mapViewButton').setAttribute('aria-pressed', String(mapMode));
}
function renderMap() {
  const plotted = state.hospitals.filter(hospital => Number.isFinite(Number(hospital.latitude)) && Number.isFinite(Number(hospital.longitude)) && (Number(hospital.latitude) !== 0 || Number(hospital.longitude) !== 0));
  if (!plotted.length) {
    $('#mapPlot').innerHTML = '<p class="map-empty">Hospitals add their coordinates in the partner workspace to appear here.</p>';
  } else {
    const lats = plotted.map(h => Number(h.latitude)), lons = plotted.map(h => Number(h.longitude));
    if (state.latitude !== null && state.longitude !== null) { lats.push(state.latitude); lons.push(state.longitude); }
    const minLat = Math.min(...lats), maxLat = Math.max(...lats), minLon = Math.min(...lons), maxLon = Math.max(...lons);
    const clamp = value => Math.max(8, Math.min(92, value));
    const x = value => clamp(maxLon === minLon ? 50 : 12 + ((value - minLon) / (maxLon - minLon)) * 76);
    const y = value => clamp(maxLat === minLat ? 50 : 14 + ((maxLat - value) / (maxLat - minLat)) * 72);
    const userPin = state.latitude === null ? '' : `<span class="map-you" style="left:${x(state.longitude)}%;top:${y(state.latitude)}%" title="Your location"></span>`;
    $('#mapPlot').innerHTML = userPin + plotted.map((hospital, index) => `<button class="map-point" style="left:${x(Number(hospital.longitude))}%;top:${y(Number(hospital.latitude))}%" type="button" data-map-hospital="${escapeHtml(hospital.id)}" aria-label="Show ${escapeHtml(hospital.name)}"><span class="map-pin"><span>${index + 1}</span></span><span class="map-point-label">${escapeHtml(hospital.name)}</span></button>`).join('');
  }
  $('#mapHospitalList').innerHTML = state.hospitals.map((hospital, index) => `<button class="map-result-item" type="button" data-map-hospital="${escapeHtml(hospital.id)}"><strong>${index + 1}. ${escapeHtml(hospital.name)}</strong><span>${escapeHtml([hospital.area, hospital.city].filter(Boolean).join(', '))}${hospital.distanceKm == null ? '' : ` · ${Number(hospital.distanceKm).toFixed(1)} km`}</span></button>`).join('');
}
async function loadHospitals() {
  const values = {
    city: $('#searchCity').value.trim(), area: $('#searchArea').value.trim(), department: $('#searchDepartment').value.trim(),
    beds: ($('#bedsOnly').checked || state.emergencyOnly) ? '1' : '',
    ambulances: state.emergencyOnly ? '1' : '', sort: $('#searchSort').value
  };
  if (state.latitude !== null && state.longitude !== null) { values.lat = state.latitude; values.lon = state.longitude; }
  try {
    const data = await api(`/api/hospitals?${queryString(values)}`);
    state.hospitals = data.results || [];
    $('#resultsTitle').textContent = state.emergencyOnly ? 'Emergency-ready hospitals' : values.city || values.area || values.department ? 'Hospitals for your search' : 'Hospitals to know near you';
    $('#directorySubtitle').textContent = state.emergencyOnly ? 'Showing listings with a hospital-reported open bed and available ambulance.' : 'Compare hospital-managed details and contact the care team directly.';
    $('#resultsCount').textContent = `${data.count} ${data.count === 1 ? 'hospital' : 'hospitals'}`;
    $('#filterButton').classList.toggle('active', $('#bedsOnly').checked);
    $('#hospitalResults').innerHTML = state.hospitals.map(renderHospitalCard).join('');
    renderMap();
    $('#emptyState').hidden = state.hospitals.length !== 0;
    setResultsView(state.mapMode);
  } catch (error) {
    $('#hospitalResults').innerHTML = '';
    $('#hospitalResults').hidden = true;
    $('#mapView').hidden = true;
    $('#emptyState').hidden = false;
    $('#emptyState h3').textContent = 'Directory is not available right now';
    $('#emptyState p').textContent = error.message;
  }
}
function setLocation(button, status, targetForm = null) {
  if (!navigator.geolocation) { status.textContent = 'Location is not available in this browser.'; return; }
  button.disabled = true;
  status.textContent = 'Waiting for location permission…';
  navigator.geolocation.getCurrentPosition(position => {
    button.disabled = false;
    const { latitude, longitude } = position.coords;
    if (targetForm === 'emergency') {
      state.emergencyLatitude = latitude;
      state.emergencyLongitude = longitude;
      $('input[name="latitude"]', $('#emergencyForm')).value = latitude;
      $('input[name="longitude"]', $('#emergencyForm')).value = longitude;
      status.textContent = 'Location added to this request.';
    } else {
      state.latitude = latitude;
      state.longitude = longitude;
      status.textContent = 'Ranking hospitals near you.';
      loadHospitals();
    }
  }, error => {
    button.disabled = false;
    status.textContent = error.code === 1 ? 'Location permission was not granted.' : 'Could not read location. You can search by city.';
  }, { enableHighAccuracy: false, timeout: 10000, maximumAge: 120000 });
}
function captureHospitalLocation(formId, buttonId, statusId) {
  const form = $(`#${formId}`), button = $(`#${buttonId}`), status = $(`#${statusId}`);
  if (!navigator.geolocation) { status.textContent = 'This browser does not provide location access. Enter coordinates manually.'; return; }
  button.disabled = true;
  status.textContent = 'Waiting for location permission…';
  navigator.geolocation.getCurrentPosition(position => {
    form.elements.latitude.value = position.coords.latitude.toFixed(6);
    form.elements.longitude.value = position.coords.longitude.toFixed(6);
    status.textContent = 'Coordinates added. You can adjust them before saving.';
    button.disabled = false;
  }, error => {
    status.textContent = error.code === 1 ? 'Location permission was not granted. You can enter coordinates manually.' : 'Could not read location. You can enter coordinates manually.';
    button.disabled = false;
  }, { enableHighAccuracy: false, timeout: 12000, maximumAge: 120000 });
}
function openAppointment(hospitalId) {
  const hospital = state.hospitals.find(item => item.id === hospitalId);
  if (!hospital) return;
  const form = $('#appointmentForm');
  form.reset();
  showFormMessage(form, '');
  form.elements.hospitalId.value = hospital.id;
  $('#dialogHospitalName').textContent = `${hospital.name} · ${hospital.area}, ${hospital.city}`;
  const departments = hospital.departments || [];
  form.elements.department.innerHTML = departments.map(value => `<option value="${escapeHtml(value)}">${escapeHtml(value)}</option>`).join('');
  form.elements.doctor.innerHTML = '<option value="">No preference</option>' + (hospital.doctors || []).map(value => `<option value="${escapeHtml(value)}">${escapeHtml(value)}</option>`).join('');
  const tomorrow = new Date(); tomorrow.setDate(tomorrow.getDate() + 1);
  form.elements.date.min = tomorrow.toISOString().slice(0, 10);
  $('#appointmentDialog').showModal();
}
function renderTimeline(items) {
  if (!items?.length) return '<p class="inbox-detail">No updates recorded yet.</p>';
  return `<ol class="timeline">${items.map(item => `<li><span class="timeline-dot"></span><div><strong>${escapeHtml(item.label)}</strong><small>${escapeHtml(formatTimestamp(item.at))}</small></div></li>`).join('')}</ol>`;
}
function renderAppointmentTracking(appointment) {
  const container = $('#appointmentTrackResult');
  container.classList.remove('error');
  const canRate = appointment.status === 'completed' && !appointment.ratingSubmitted;
  const reviewPanel = canRate
    ? `<form class="rating-form" data-rating-code="${escapeHtml(appointment.id)}"><strong>How was your visit?</strong><label for="reviewStars">Your rating<select id="reviewStars" name="stars"><option value="5">★★★★★ · Excellent</option><option value="4">★★★★ · Good</option><option value="3">★★★ · Okay</option><option value="2">★★ · Poor</option><option value="1">★ · Very poor</option></select></label><label class="review-comment-label" for="reviewComment">Your review <span>optional</span></label><textarea id="reviewComment" name="comment" maxlength="800" rows="3" placeholder="Share a helpful note about your visit"></textarea><button type="submit">Submit review</button><p class="form-message review-message" aria-live="polite"></p></form>`
    : appointment.status === 'completed'
      ? '<p class="review-thanks">Thanks — your review has already been submitted for this appointment.</p>'
      : ['requested', 'confirmed'].includes(appointment.status)
        ? '<p class="review-pending">You can leave a review after your visit, once the hospital marks this appointment complete.</p>'
        : '';
  container.innerHTML = `<div class="track-status"><span class="status-pill">${escapeHtml(statusLabel(appointment.status))}</span><strong>${escapeHtml(appointment.hospitalName || 'Hospital')}</strong><span>${escapeHtml(appointment.department || '')}${appointment.doctor ? ` · ${escapeHtml(appointment.doctor)}` : ''}</span><span>${escapeHtml(formatDate(appointment.date))} · ${escapeHtml(appointment.time || '')}</span></div>${reviewPanel}`;
}
function renderEmergencyTracking(emergency) {
  const container = $('#emergencyTrackResult');
  container.classList.remove('error');
  const canRequestFallback = emergency.status === 'hospital_notified';
  const canCancel = ['hospital_notified', 'accepted', 'callback_validated', 'ambulance_assigned'].includes(emergency.status);
  const actions = `${canRequestFallback ? '<button type="button" data-emergency-action="no-response">No response · try next hospital</button>' : ''}${canCancel ? '<button type="button" class="danger-action" data-emergency-action="cancel">Cancel request</button>' : ''}`;
  container.innerHTML = `<div class="emergency-track-card"><div class="track-status"><span class="status-pill status-urgent">${escapeHtml(statusLabel(emergency.status))}</span><strong>${escapeHtml(emergency.hospitalName || 'No hospital assigned')}</strong><span>${emergency.hospitalName ? 'A hospital team must respond manually.' : 'Contact local emergency services now.'}</span></div>${renderTimeline(emergency.timeline)}${actions ? `<div class="tracking-actions">${actions}</div>` : ''}<p class="private-code">Private code: <code>${escapeHtml(emergency.id)}</code></p></div>`;
}
async function trackAppointment(code) {
  const result = $('#appointmentTrackResult');
  result.textContent = 'Checking status…'; result.classList.remove('error');
  try { renderAppointmentTracking(await api(`/api/appointments/status?${queryString({ code })}`)); }
  catch (error) { result.textContent = error.message; result.classList.add('error'); }
}
async function trackEmergency(code) {
  const result = $('#emergencyTrackResult');
  result.textContent = 'Checking status…'; result.classList.remove('error');
  try { renderEmergencyTracking(await api(`/api/emergencies/status?${queryString({ code })}`)); }
  catch (error) { result.textContent = error.message; result.classList.add('error'); }
}
function setAuthTab(tab) {
  const registering = tab === 'register';
  $('#loginForm').hidden = registering;
  $('#registerForm').hidden = !registering;
  $$('[data-auth-tab]').forEach(button => {
    const active = button.dataset.authTab === tab;
    button.classList.toggle('active', active);
    button.setAttribute('aria-selected', String(active));
  });
}
function fillProfile(hospital) {
  const form = $('#profileForm');
  for (const key of ['name', 'city', 'area', 'address', 'phone', 'latitude', 'longitude', 'totalBeds', 'availableBeds', 'totalAmbulances', 'availableAmbulances']) {
    form.elements[key].value = hospital[key] ?? '';
  }
  form.elements.departments.value = (hospital.departments || []).join(', ');
  form.elements.doctors.value = (hospital.doctors || []).join(', ');
  $('#dashboardName').textContent = hospital.name;
  $('#dashboardEmail').textContent = hospital.accountEmail || '';
  $('#statBeds').textContent = `${hospital.availableBeds}/${hospital.totalBeds}`;
  $('#statAmbulances').textContent = `${hospital.availableAmbulances}/${hospital.totalAmbulances}`;
}
function appointmentInboxCard(item) {
  let actions = '';
  if (item.status === 'requested') actions = `<button type="button" data-appointment-action="confirm" data-id="${escapeHtml(item.id)}">Confirm</button><button type="button" class="danger-action" data-appointment-action="cancel" data-id="${escapeHtml(item.id)}">Decline</button>`;
  else if (item.status === 'confirmed') actions = `<button type="button" data-appointment-action="complete" data-id="${escapeHtml(item.id)}">Mark visit complete</button><button type="button" class="danger-action" data-appointment-action="cancel" data-id="${escapeHtml(item.id)}">Cancel</button>`;
  const contact = item.phone ? `<a href="tel:${escapeHtml(String(item.phone).replace(/[^\d+]/g, ''))}">${escapeHtml(item.phone)}</a>` : '';
  return `<article class="inbox-card"><div class="inbox-card-head"><strong>${escapeHtml(item.patientName)}</strong><span>${escapeHtml(statusLabel(item.status))}</span></div><p class="inbox-detail">${escapeHtml(item.department)}${item.doctor ? ` · ${escapeHtml(item.doctor)}` : ''}<br>${escapeHtml(formatDate(item.date))} · ${escapeHtml(item.time)}${contact ? `<br>${contact}` : ''}${item.email ? `<br>${escapeHtml(item.email)}` : ''}${item.notes ? `<br>Note: ${escapeHtml(item.notes)}` : ''}</p>${actions ? `<div class="inbox-actions">${actions}</div>` : ''}</article>`;
}
function emergencyActions(item) {
  const actions = {
    hospital_notified: [['accept', 'Accept request'], ['reject', 'Decline · try next']],
    accepted: [['callback', 'Callback complete · validate']],
    callback_validated: [['assign', 'Assign ambulance']],
    ambulance_assigned: [['dispatch', 'Mark dispatched']],
    dispatched: [['pickup', 'Mark patient picked up']],
    patient_picked_up: [['arrive', 'Mark hospital reached']],
    hospital_reached: [['complete', 'Complete request']]
  }[item.status] || [];
  return actions.map(([action, label]) => `<button type="button" ${action === 'reject' ? 'class="danger-action"' : ''} data-emergency-staff-action="${action}" data-id="${escapeHtml(item.id)}">${escapeHtml(label)}</button>`).join('');
}
function emergencyInboxCard(item) {
  const phone = String(item.phone || '').replace(/[^\d+]/g, '');
  const location = Number.isFinite(Number(item.latitude)) && Number.isFinite(Number(item.longitude)) && (item.latitude !== 0 || item.longitude !== 0) ? `<br>Location: ${Number(item.latitude).toFixed(5)}, ${Number(item.longitude).toFixed(5)}` : '';
  return `<article class="inbox-card urgent-inbox"><div class="inbox-card-head"><strong>${escapeHtml(item.patientName)}</strong><span>${escapeHtml(statusLabel(item.status))}</span></div><p class="inbox-detail">${phone ? `<a href="tel:${escapeHtml(phone)}">${escapeHtml(item.phone)}</a>` : ''}${location}<br>${item.symptoms ? escapeHtml(item.symptoms) : 'No additional details shared.'}</p><div class="inbox-actions">${emergencyActions(item)}</div></article>`;
}
async function loadDashboard({ updateProfile = false } = {}) {
  if (!state.staffToken) return;
  try {
    const data = await api('/api/staff/overview', { auth: true });
    if (updateProfile) fillProfile(data.hospital);
    else {
      $('#dashboardName').textContent = data.hospital.name;
      $('#dashboardEmail').textContent = data.hospital.accountEmail || '';
      $('#statBeds').textContent = `${data.hospital.availableBeds}/${data.hospital.totalBeds}`;
      $('#statAmbulances').textContent = `${data.hospital.availableAmbulances}/${data.hospital.totalAmbulances}`;
    }
    const pending = data.appointments.filter(item => ['requested', 'confirmed'].includes(item.status));
    const activeEmergency = data.emergencies.filter(item => !['completed', 'cancelled', 'no_hospital_available'].includes(item.status));
    $('#statAppointments').textContent = pending.length;
    $('#statEmergencies').textContent = activeEmergency.length;
    $('#appointmentInbox').innerHTML = data.appointments.length ? data.appointments.slice().reverse().map(appointmentInboxCard).join('') : '<div class="inbox-empty">No appointment requests yet.</div>';
    $('#emergencyInbox').innerHTML = activeEmergency.length ? activeEmergency.map(emergencyInboxCard).join('') : '<div class="inbox-empty">No open emergency requests.</div>';
  } catch (error) {
    showToast(error.message, true);
    if (error.message.toLowerCase().includes('sign in')) signOut(false);
  }
}
function showDashboard() {
  if (!$('#staffDialog').open) $('#staffDialog').showModal();
  $('#authView').hidden = true;
  $('#dashboardView').hidden = false;
  $('#portalCard').scrollIntoView({ behavior: 'smooth', block: 'start' });
  loadDashboard({ updateProfile: true });
  clearInterval(state.dashboardTimer);
  state.dashboardTimer = setInterval(() => loadDashboard(), 15000);
}
function signOut(callApi = true) {
  if (callApi && state.staffToken) api('/api/logout', { method: 'POST', auth: true }).catch(() => {});
  state.staffToken = '';
  sessionStorage.removeItem('medera_token');
  clearInterval(state.dashboardTimer);
  $('#dashboardView').hidden = true;
  $('#authView').hidden = false;
  setAuthTab('login');
}
async function establishSession(result) {
  state.staffToken = result.token;
  sessionStorage.setItem('medera_token', result.token);
  showDashboard();
  showToast('You are signed in to your hospital workspace.');
}

function openStaff(register = false) {
  if (!$('#staffDialog').open) $('#staffDialog').showModal();
  if (register) setAuthTab('register');
  if (state.staffToken) showDashboard();
}
function openEmergency() {
  const dialog = $('#emergencyDialog');
  if (!dialog.open) dialog.showModal();
}

$('#searchForm').addEventListener('submit', event => { event.preventDefault(); loadHospitals(); });
$('#bedsOnly').addEventListener('change', loadHospitals);
$('#searchSort').addEventListener('change', () => {
  if ($('#searchSort').value === 'distance' && state.latitude === null) {
    $('#searchSort').value = 'recommended';
    showToast('Choose “Use my location” before sorting by distance.');
    return;
  }
  loadHospitals();
});
$('#useLocationButton').addEventListener('click', event => setLocation(event.currentTarget, $('#locationStatus')));
$('#emergencyLocationButton').addEventListener('click', event => setLocation(event.currentTarget, $('#emergencyLocationStatus'), 'emergency'));
$('#registerLocationButton').addEventListener('click', () => captureHospitalLocation('registerForm', 'registerLocationButton', 'registerLocationStatus'));
$('#profileLocationButton').addEventListener('click', () => captureHospitalLocation('profileForm', 'profileLocationButton', 'profileLocationStatus'));
$('#navStaffButton').addEventListener('click', () => openStaff());
$('#hostCtaButton').addEventListener('click', () => openStaff(true));
$('#emptyRegisterButton').addEventListener('click', () => openStaff(true));
$('#navTrackButton').addEventListener('click', () => $('#track-request').scrollIntoView({ behavior: 'smooth', block: 'center' }));
$('#emergencyNavButton').addEventListener('click', openEmergency);
$('#openEmergencyButton').addEventListener('click', openEmergency);
$('#closeEmergency').addEventListener('click', () => $('#emergencyDialog').close());
$('#emergencyDialog').addEventListener('click', event => { if (event.target === $('#emergencyDialog')) $('#emergencyDialog').close(); });
$('#closeStaff').addEventListener('click', () => $('#staffDialog').close());
$('#staffDialog').addEventListener('click', event => { if (event.target === $('#staffDialog')) $('#staffDialog').close(); });
$('#listViewButton').addEventListener('click', () => setResultsView(false));
$('#mapViewButton').addEventListener('click', () => { renderMap(); setResultsView(true); });
$('#filterButton').addEventListener('click', () => { $('#bedsOnly').checked = !$('#bedsOnly').checked; loadHospitals(); });
$$('[data-search-tab]').forEach(button => button.addEventListener('click', () => {
  state.emergencyOnly = button.dataset.searchTab === 'emergency';
  $$('[data-search-tab]').forEach(tab => { const active = tab === button; tab.classList.toggle('active', active); tab.setAttribute('aria-selected', String(active)); });
  loadHospitals();
}));
$$('[data-top-category]').forEach(button => button.addEventListener('click', () => {
  $$('[data-top-category]').forEach(item => item.classList.toggle('active', item === button));
  if (button.dataset.topCategory === 'emergency') { state.emergencyOnly = true; $$('[data-search-tab]').forEach(tab => { const active = tab.dataset.searchTab === 'emergency'; tab.classList.toggle('active', active); tab.setAttribute('aria-selected', String(active)); }); loadHospitals(); openEmergency(); }
  else if (button.dataset.topCategory === 'specialties') document.querySelector('.specialty-rail').scrollIntoView({ behavior: 'smooth', block: 'center' });
  else { state.emergencyOnly = false; $$('[data-search-tab]').forEach(tab => { const active = tab.dataset.searchTab === 'all'; tab.classList.toggle('active', active); tab.setAttribute('aria-selected', String(active)); }); loadHospitals(); }
}));
$('#categoryChips').addEventListener('click', event => {
  const chip = event.target.closest('[data-department]');
  if (!chip) return;
  $('#searchDepartment').value = chip.dataset.department;
  $$('#categoryChips [data-department]').forEach(item => item.classList.toggle('active', item === chip));
  loadHospitals();
});
$('#hospitalResults').addEventListener('click', event => {
  const saveButton = event.target.closest('[data-save]');
  if (saveButton) {
    const id = saveButton.dataset.save;
    if (state.favorites.has(id)) state.favorites.delete(id); else state.favorites.add(id);
    try { localStorage.setItem('medera_favorites', JSON.stringify([...state.favorites])); } catch { /* Favorites remain available for this page session. */ }
    saveButton.classList.toggle('saved', state.favorites.has(id));
    saveButton.setAttribute('aria-pressed', String(state.favorites.has(id)));
    saveButton.setAttribute('aria-label', state.favorites.has(id) ? 'Remove saved hospital' : 'Save hospital');
    return;
  }
  const button = event.target.closest('[data-book]');
  if (button && !button.disabled) openAppointment(button.dataset.book);
});
$('#mapView').addEventListener('click', event => {
  const button = event.target.closest('[data-map-hospital]');
  if (!button) return;
  setResultsView(false);
  const card = $(`#hospital-card-${CSS.escape(button.dataset.mapHospital)}`);
  card?.scrollIntoView({ behavior: 'smooth', block: 'center' });
  card?.classList.add('map-highlight');
  setTimeout(() => card?.classList.remove('map-highlight'), 1500);
});
$('#closeAppointment').addEventListener('click', () => $('#appointmentDialog').close());
$('#appointmentDialog').addEventListener('click', event => { if (event.target === $('#appointmentDialog')) $('#appointmentDialog').close(); });

$('#appointmentForm').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const button = $('button[type="submit"]', form);
  button.disabled = true;
  showFormMessage(form, 'Sending your request…');
  try {
    const result = await api('/api/appointments', { method: 'POST', data: formData(form) });
    $('#appointmentDialog').close();
    $('#appointmentTrackForm').elements.code.value = result.appointment.id;
    await trackAppointment(result.appointment.id);
    $('#appointmentTrackResult').insertAdjacentHTML('afterbegin', `<div class="tracking-code-note">Request code <code>${escapeHtml(result.appointment.id)}</code> · save this code to check status.</div>`);
    $('#track-request').scrollIntoView({ behavior: 'smooth', block: 'center' });
    showToast('Appointment request sent. The hospital must confirm your visit.');
  } catch (error) { showFormMessage(form, error.message, true); }
  finally { button.disabled = false; }
});

$('#emergencyForm').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const button = $('button[type="submit"]', form);
  button.disabled = true;
  const data = formData(form);
  if (state.emergencyLatitude !== null && state.emergencyLongitude !== null) {
    data.latitude = state.emergencyLatitude;
    data.longitude = state.emergencyLongitude;
  }
  try {
    const result = await api('/api/emergencies', { method: 'POST', data });
    const code = result.trackingCode;
    $('#emergencyTrackForm').elements.code.value = code;
    await trackEmergency(code);
    $('#emergencyTrackResult').insertAdjacentHTML('afterbegin', `<div class="tracking-code-note">Save this private tracking code: <code>${escapeHtml(code)}</code></div>`);
    $('#emergencyDialog').close();
    form.reset(); state.emergencyLatitude = null; state.emergencyLongitude = null;
    $('#emergencyLocationStatus').textContent = 'Location helps rank nearby hospitals.';
    $('#emergencyTrackForm').scrollIntoView({ behavior: 'smooth', block: 'center' });
    showToast(result.emergency.status === 'no_hospital_available' ? 'No matching hospital is available. Contact local emergency services now.' : 'Request sent. Stay reachable for a hospital callback.', result.emergency.status === 'no_hospital_available');
  } catch (error) { showToast(error.message, true); }
  finally { button.disabled = false; }
});

$('#appointmentTrackForm').addEventListener('submit', event => {
  event.preventDefault();
  trackAppointment(formData(event.currentTarget).code.trim());
});
$('#emergencyTrackForm').addEventListener('submit', event => {
  event.preventDefault();
  trackEmergency(formData(event.currentTarget).code.trim());
});
$('#appointmentTrackResult').addEventListener('submit', async event => {
  if (!event.target.matches('.rating-form')) return;
  event.preventDefault();
  const form = event.target;
  const button = $('button[type="submit"]', form);
  const message = $('.form-message', form);
  button.disabled = true;
  message.textContent = 'Sending your review…';
  message.classList.remove('error');
  try {
    await api('/api/ratings', { method: 'POST', data: { appointmentId: form.dataset.ratingCode, ...formData(form) } });
    showToast('Thanks for sharing your review.');
    await trackAppointment(form.dataset.ratingCode);
    await loadHospitals();
  } catch (error) {
    message.textContent = error.message;
    message.classList.add('error');
    button.disabled = false;
  }
});
$('#emergencyTrackResult').addEventListener('click', async event => {
  const button = event.target.closest('[data-emergency-action]');
  if (!button) return;
  button.disabled = true;
  const code = $('#emergencyTrackForm').elements.code.value.trim();
  try {
    await api('/api/emergencies/action', { method: 'POST', data: { id: code, action: button.dataset.emergencyAction } });
    await trackEmergency(code);
    showToast(button.dataset.emergencyAction === 'cancel' ? 'Request cancelled.' : 'Request moved to the next eligible hospital.');
  } catch (error) { button.disabled = false; showToast(error.message, true); }
});

$$('[data-auth-tab]').forEach(button => button.addEventListener('click', () => setAuthTab(button.dataset.authTab)));
$('#loginForm').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const button = $('button[type="submit"]', form); button.disabled = true;
  showFormMessage(form, 'Signing in…');
  try { await establishSession(await api('/api/login', { method: 'POST', data: formData(form) })); form.reset(); showFormMessage(form, ''); }
  catch (error) { showFormMessage(form, error.message, true); }
  finally { button.disabled = false; }
});
$('#registerForm').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const button = $('button[type="submit"]', form); button.disabled = true;
  showFormMessage(form, 'Creating your account…');
  try { await establishSession(await api('/api/register', { method: 'POST', data: formData(form) })); form.reset(); showFormMessage(form, ''); }
  catch (error) { showFormMessage(form, error.message, true); }
  finally { button.disabled = false; }
});
$('#profileForm').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget; const button = $('button[type="submit"]', form); button.disabled = true;
  showFormMessage(form, 'Saving…');
  try {
    const result = await api('/api/staff/profile', { method: 'POST', data: formData(form), auth: true });
    fillProfile(result.hospital); showFormMessage(form, 'Directory details saved.'); await loadDashboard(); await loadHospitals();
  } catch (error) { showFormMessage(form, error.message, true); }
  finally { button.disabled = false; }
});
$('#logoutButton').addEventListener('click', () => signOut(true));
$('#refreshDashboard').addEventListener('click', () => loadDashboard());
$('#appointmentInbox').addEventListener('click', async event => {
  const button = event.target.closest('[data-appointment-action]'); if (!button) return;
  button.disabled = true;
  try { await api('/api/staff/appointment', { method: 'POST', data: { id: button.dataset.id, action: button.dataset.appointmentAction }, auth: true }); await loadDashboard(); showToast('Appointment updated.'); }
  catch (error) { button.disabled = false; showToast(error.message, true); }
});
$('#emergencyInbox').addEventListener('click', async event => {
  const button = event.target.closest('[data-emergency-staff-action]'); if (!button) return;
  button.disabled = true;
  try { await api('/api/staff/emergency', { method: 'POST', data: { id: button.dataset.id, action: button.dataset.emergencyStaffAction }, auth: true }); await loadDashboard(); await loadHospitals(); showToast('Emergency request updated.'); }
  catch (error) { button.disabled = false; showToast(error.message, true); }
});

if (state.staffToken) showDashboard();
loadHospitals();

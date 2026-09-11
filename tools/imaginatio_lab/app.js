(() => {
  'use strict';

  const $ = id => document.getElementById(id);
  const embedded = window.self !== window.top || new URLSearchParams(window.location.search).get('embedded') === '1';
  if (embedded) document.documentElement.classList.add('embedded');
  const reportEmbeddedHeight = () => {
    if (!embedded) return;
    window.parent.postMessage({
      type: 'tatarus-imaginatio-height',
      height: Math.max(document.body.scrollHeight, document.documentElement.scrollHeight),
    }, window.location.origin);
  };
  if (embedded && typeof ResizeObserver !== 'undefined') {
    new ResizeObserver(reportEmbeddedHeight).observe(document.body);
  }
  window.addEventListener('load', reportEmbeddedHeight);
  const side = 512;
  let sourceImageWidth = side;
  let sourceImageHeight = side;
  const target = new Float32Array(side * side * 3);
  const output = new Float32Array(side * side * 3);
  const cue = $('cue-canvas');
  const resultCanvas = $('output-canvas');
  const cueCtx = cue.getContext('2d');
  const resultCtx = resultCanvas.getContext('2d');
  let mode = 'learn';
  let state = null;
  let trace = {events: [], summary: {}};
  let busy = false;
  let replayToken = 0;
  let replaying = false;
  let pointerDown = false;
  let statusData = {};
  let selectedArtifact = null;
  let loadedDataset = [];
  let loadedDatasetIndex = -1;
  let cortexState = null;
  let cortexBusy = false;
  let cortexAutoExecuting = false;
  let lastAutoExecutedRequest = null;

  const clamp = value => Math.max(0, Math.min(1, Number(value) || 0));
  const byte = value => Math.round(clamp(value) * 255);
  const fmt = (value, digits = 2) => Number(value || 0).toLocaleString('de-DE', {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  });
  const pct = value => `${fmt(Number(value || 0) * 100, 1)} %`;
  const setText = (id, value) => { const element = $(id); if (element) element.textContent = value; };
  const setBar = (id, value) => { const element = $(id); if (element) element.style.width = `${clamp(value) * 100}%`; };
  const colorIndex = (x, y) => (y * side + x) * 3;
  const rgbAt = (pixels, x, y) => {
    const index = colorIndex(x, y);
    return [pixels[index], pixels[index + 1], pixels[index + 2]];
  };
  const setRgb = (pixels, x, y, color) => {
    x = Math.round(x); y = Math.round(y);
    if (x < 0 || y < 0 || x >= side || y >= side) return;
    const index = colorIndex(x, y);
    pixels[index] = clamp(color[0]);
    pixels[index + 1] = clamp(color[1]);
    pixels[index + 2] = clamp(color[2]);
  };
  const setRgbBrush = (pixels, x, y, color, radius = 3) => {
    for (let dy = -radius; dy <= radius; dy++) for (let dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dy * dy <= radius * radius) setRgb(pixels, x + dx, y + dy, color);
    }
  };
  const hexColor = color => `#${color.map(value => byte(value).toString(16).padStart(2, '0')).join('').toUpperCase()}`;
  const colorFromHex = value => [
    parseInt(value.slice(1, 3), 16) / 255,
    parseInt(value.slice(3, 5), 16) / 255,
    parseInt(value.slice(5, 7), 16) / 255,
  ];
  const luminance = color => .2126 * color[0] + .7152 * color[1] + .0722 * color[2];
  function rgbBase64(pixels) {
    const bytes = new Uint8Array(pixels.length);
    for (let index = 0; index < pixels.length; index++) bytes[index] = byte(pixels[index]);
    let binary = '';
    for (let offset = 0; offset < bytes.length; offset += 32768) {
      binary += String.fromCharCode(...bytes.subarray(offset, offset + 32768));
    }
    return btoa(binary);
  }
  function base64Bytes(encoded) {
    const binary = atob(encoded || '');
    const bytes = new Uint8Array(binary.length);
    for (let index = 0; index < binary.length; index++) bytes[index] = binary.charCodeAt(index);
    return bytes;
  }
  function rgbFloatsFromBase64(encoded) {
    const bytes = base64Bytes(encoded);
    if (bytes.length !== output.length) throw new Error('Die empfangene 512×512-RGB-Leinwand ist unvollständig.');
    const values = new Float32Array(bytes.length);
    for (let index = 0; index < bytes.length; index++) values[index] = bytes[index] / 255;
    return values;
  }
  const hsv = (h, s = .8, v = 1) => {
    const sector = ((h % 360) + 360) % 360 / 60;
    const c = v * s;
    const x = c * (1 - Math.abs(sector % 2 - 1));
    const pairs = [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]];
    const base = pairs[Math.floor(sector) % 6];
    const m = v - c;
    return base.map(channel => channel + m);
  };

  const actionLabels = {
    move_north: 'MOVE_NORTH', move_east: 'MOVE_EAST', move_south: 'MOVE_SOUTH',
    move_west: 'MOVE_WEST', paint: 'PAINT_RGB', erase: 'ERASE', brighter: 'BRIGHTER',
    darker: 'DARKER', brush_small: 'BRUSH_SMALL', brush_large: 'BRUSH_LARGE',
  };
  const causeLabels = {
    demonstrated_motor_trace: 'Demonstrierte RGB-Malspur',
    recalled_sensorimotor_engram: 'V14-Rekonstruktion aus Self-Motor-/Region-/Stroke-Gedächtnis',
    coactivated_visual_engrams: 'Koaktivierte abstrakte visuelle Engramme',
    coactivated_five_field_form_synthesis: 'Stufe 4 · eine neue Fünf-Feld-Form',
    coactivated_factorized_feature_synthesis: 'Stufe 4 · Teilmerkmale aus mehreren Erinnerungen',
    category_consensus_fusion: 'Stufe 5 · Legacy-Bezeichner (V14: abstrakte Statistikfusion)',
    category_five_field_form_synthesis: 'Stufe 5 · neue gemeinsame Formgeometrie',
    category_factorized_feature_synthesis: 'Stufe 5 · gelernte Teilmerkmale neu kombiniert',
    relational_scene_object_motor_program: 'Stufe 6 · objektweises Szenen-Motorprogramm',
    predicted_scene_object_motor_program: 'Stufe 7 · motorisch ausgedrückter Folgezustand',
    internally_simulated_scene_state: 'Interner Szenenzustand · Leinwand unverändert',
    retinal_reference_and_canvas: 'RGB-Retina: Referenz + eigene Leinwand',
    internal_or_symbol_cue: 'Interner Gedächtnis-/Symbolhinweis',
  };
  const modeInfo = {
    learn: {title: 'Farbreferenz sichtbar', copy: 'TATARUS sieht das sRGB-Zielbild sensorisch, zeichnet es selbst und konsolidiert danach die eigene fertige Leinwand als gekoppeltes Self-Motor-Engramm. Quellraster und Teacher-Trace werden nicht persistiert.', button: 'Farbbild neuronal lernen', visibility: 'visible'},
    recall: {title: 'Farbiger visueller Cue', copy: 'Der RGB-Cue wird nur zur Auswahl des Engramms wahrgenommen und danach entfernt. Das Bild entsteht neu aus Shape-, Feature-, Region-, Stroke- und Self-Motor-Gedächtnis.', button: 'Farbbild aus V14-Gedächtnis malen', visibility: 'visible'},
    symbol: {title: 'Symbolkanal', copy: 'Beim Malen ist keine Bildvorlage im Sensorstrom vorhanden.', button: 'Farbbild aus Symbol malen', secondary: 'Symbol mit aktueller Farbreferenz verknüpfen', visibility: 'hidden'},
    compose: {title: 'Gelernte Teilmerkmale neu verbinden', copy: 'Kanten werden zu allgemeinen Objektteilen verdichtet. Ob Gesicht, Baum, Vogel oder Haus: räumliche Teilfelder können aus verschiedenen Erinnerungen stammen und werden weich verbunden.', button: 'Eine neue Farbform erzeugen', visibility: 'hidden'},
    fusion: {title: 'Hierarchisches Kategoriegedächtnis', copy: 'Bis zu fünf Kategorien aktivieren Form, gelernte Objektteile und ihren Top-down-Prior. Die Teilfelder werden unabhängig ausgewählt und ohne harte Nähte zusammengesetzt.', button: 'Neue Form aus Kategorien bilden', visibility: 'hidden'},
    scene: {title: 'Relationale Szenenvorstellung', copy: 'Objekte behalten Kategorie und Pose. Gelernte Relationen bestimmen Position und Tiefe; jedes Objekt wird einzeln gemalt und danach erneut gesehen.', button: 'Neue Relationsszene imaginieren', secondary: 'Vollständiges Layout als Relationserfahrung lernen', visibility: 'hidden'},
    future: {title: 'Prospektive Vorstellung', copy: 'TATARUS rollt gelernte Handlungswirkungen intern über Zustände vor und malt erst den erwarteten Endzustand.', button: 'Erwartete Zukunft imaginieren', secondary: 'Ausgang → Handlung → Folge lernen', visibility: 'hidden'},
  };
  const presetMeta = {
    circle: ['FARBKREIS', 'FARBE', 'FORMEN'], cross: ['FARBKREUZ', 'X', 'FORMEN'], triangle: ['DREIECK', 'DREIECK', 'FORMEN'],
    tree: ['BAUM', 'BAUM', 'NATUR'], house: ['HAUS', 'HAUS', 'GEBÄUDE'], moon: ['MOND', 'MOND', 'HIMMEL'], blank: ['', '', ''],
  };

  async function getJson(path) {
    const response = await fetch(path, {cache: 'no-store'});
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
    return payload;
  }

  async function postJson(path, body) {
    const response = await fetch(path, {
      method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body),
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
    return payload;
  }


  function cortexConfigured() {
    return Boolean(cortexState?.available && cortexState?.configured);
  }

  function cortexGoalText() {
    return String($('cortex-goal')?.value || '').trim();
  }

  function cortexActiveGoal() {
    return cortexState?.executive?.active_goal || null;
  }

  function setCortexNotice(message, kind = '') {
    const element = $('cortex-notice');
    if (!element) return;
    element.textContent = message || '—';
    element.className = `notice ${kind === 'error' ? 'error' : ''}`;
  }

  function renderCortexChips(containerId, entries) {
    const box = $(containerId);
    if (!box) return;
    box.innerHTML = '';
    if (!entries.length) {
      const empty = document.createElement('span');
      empty.className = 'cortex-chip'; empty.textContent = '—'; box.appendChild(empty); return;
    }
    entries.slice(0, 28).forEach(({text, type = ''}) => {
      const chip = document.createElement('span'); chip.className = `cortex-chip ${type}`; chip.textContent = text; box.appendChild(chip);
    });
  }

  function updateCortex(nextState) {
    if (!nextState) return;
    cortexState = nextState;
    const available = Boolean(nextState.available);
    const configured = Boolean(nextState.configured);
    const connected = Boolean(nextState.lm_studio?.connected);
    const workerBusy = configured && nextState.idle === false;
    const top = $('cortex-top-status');
    if (top) {
      top.className = `status ${connected ? 'live' : (available && configured ? '' : 'error')}`;
      top.querySelector('span').textContent = connected ? 'Cortex online' : (configured ? 'Cortex bereit' : 'Cortex fehlt');
    }
    const badge = $('cortex-state-badge');
    if (badge) {
      badge.className = `badge ${connected ? 'visible' : 'hidden'}`;
      badge.textContent = connected ? 'LM ONLINE' : (configured ? 'LM OFFLINE' : 'NICHT KONFIGURIERT');
    }
    setText('cortex-lm', connected ? 'ONLINE' : (configured ? 'OFFLINE' : '—'));
    const model = nextState.lm_studio?.model || nextState.lm_studio?.models?.[0] || '—';
    setText('cortex-model', model);
    setText('cortex-mode', `${nextState.mode || '—'} / ${nextState.execution_mode || '—'}`);
    setText('cortex-meta', nextState.metacognition?.state || '—');
    setText('cortex-reliability', nextState.metacognition ? pct(nextState.metacognition.reliability || 0) : '—');
    setText('cortex-sleep', nextState.sleep_phase || '—');
    setText('cortex-idle', nextState.idle ? 'IDLE' : 'ARBEITET');

    const active = nextState.executive?.active_goal || null;
    const goalBox = $('cortex-active-goal');
    if (goalBox) {
      goalBox.innerHTML = '';
      const strong = document.createElement('strong');
      const detail = document.createElement('span');
      if (active) {
        strong.textContent = active.text;
        detail.textContent = `Priorität ${fmt(active.priority, 2)} · ${active.status}`;
        setText('cortex-goal-id', `#${active.id}`);
      } else {
        strong.textContent = 'Kein aktives Ziel';
        detail.textContent = 'Text eingeben und „Ziel übernehmen“ wählen.';
        setText('cortex-goal-id', '—');
      }
      goalBox.append(strong, detail);
    }
    if ($('cortex-goal-complete')) $('cortex-goal-complete').disabled = !active || cortexBusy || workerBusy;
    if ($('cortex-goal-cancel')) $('cortex-goal-cancel').disabled = !active || cortexBusy || workerBusy;

    const decision = nextState.last_decision;
    const decisionBox = $('cortex-decision');
    if (decisionBox) {
      decisionBox.innerHTML = '';
      decisionBox.className = 'cortex-box cortex-decision';
      const strong = document.createElement('strong');
      const detail = document.createElement('span');
      if (decision) {
        const strategy = decision.strategy;
        strong.textContent = `${decision.kind}${strategy ? ` · ${strategy.kind}` : ''}`;
        detail.textContent = strategy?.rationale || decision.reason || '—';
        const kind = String(decision.kind || '').toUpperCase();
        if (kind.includes('IMAGINATION')) decisionBox.classList.add('imagine');
        else if (kind.includes('ACCEPT')) decisionBox.classList.add('accept');
        else if (kind.includes('REJECT')) decisionBox.classList.add('reject');
        setText('cortex-decision-score', fmt(decision.score || 0, 3));
      } else {
        strong.textContent = '—'; detail.textContent = 'Noch keine abgeschlossene Cortex-Entscheidung.'; setText('cortex-decision-score', '—');
      }
      decisionBox.append(strong, detail);
    }
    const summary = $('cortex-summary');
    if (summary) {
      summary.innerHTML = '';
      const strong = document.createElement('strong'); strong.textContent = 'LM-Zusammenfassung';
      const detail = document.createElement('span'); detail.textContent = nextState.last_response?.summary || '—';
      summary.append(strong, detail);
    }

    const plan = nextState.executive?.plan || null;
    setText('cortex-plan-id', plan?.id || '—');
    const planBox = $('cortex-plan-list');
    if (planBox) {
      planBox.innerHTML = '';
      if (!plan?.steps?.length) {
        const empty = document.createElement('div'); empty.className = 'cortex-box'; empty.innerHTML = '<span>Noch kein Executive-Plan.</span>'; planBox.appendChild(empty);
      } else {
        plan.steps.forEach((step, index) => {
          const row = document.createElement('div'); row.className = `cortex-plan-step ${index === Number(plan.active_step || 0) ? 'active' : ''}`;
          const number = document.createElement('b'); number.textContent = String(index + 1);
          const text = document.createElement('span'); text.textContent = `${step.kind}: ${step.objective}`;
          const state = document.createElement('em'); state.textContent = step.status || '—';
          row.append(number, text, state); planBox.appendChild(row);
        });
      }
    }

    const grounding = nextState.grounding || {};
    const groundEntries = [
      ...(grounding.known_symbols || []).map(text => ({text, type: 'symbol'})),
      ...(grounding.known_concepts || []).map(text => ({text, type: 'concept'})),
      ...(grounding.known_categories || []).map(text => ({text, type: 'category'})),
    ];
    renderCortexChips('cortex-grounding', groundEntries);
    setText('cortex-grounding-count', `${grounding.visual_engrams || 0}V / ${grounding.symbol_engrams || 0}S`);

    const memory = nextState.executive?.working_memory || [];
    renderCortexChips('cortex-memory-list', memory.map(item => ({text: `${item.key}: ${item.value}`})));
    setText('cortex-memory-count', String(memory.length));

    const configuredButtons = ['cortex-analyze','cortex-imagine','cortex-hybrid','cortex-plan','cortex-autonomous','cortex-add-goal','cortex-remember'];
    configuredButtons.forEach(id => {
      if ($(id)) $(id).disabled = !configured || cortexBusy || workerBusy;
    });
    if ($('cortex-probe')) $('cortex-probe').disabled = !available || cortexBusy || workerBusy;
    if ($('cortex-configure')) $('cortex-configure').disabled = !available || cortexBusy || workerBusy;
    if ($('cortex-execute')) $('cortex-execute').disabled = !configured || !nextState.imagination_executable || cortexBusy;
    if ($('cortex-autonomous')) {
      $('cortex-autonomous').textContent = nextState.autonomous ? 'Autonom stoppen' : 'Autonom starten';
      if (nextState.autonomous) $('cortex-autonomous').disabled = !configured || cortexBusy;
    }

    const rejectedDirective = nextState.imagination_directive
      && !nextState.imagination_directive.executable
      && nextState.last_decision?.kind === 'SEND_TO_IMAGINATION'
      ? nextState.imagination_directive.reason
      : '';
    const errorText = nextState.last_error || nextState.lm_studio?.error
      || nextState.probe_error || rejectedDirective || '';
    if (errorText) setCortexNotice(errorText, 'error');
    else if (nextState.last_execution) setCortexNotice(nextState.last_execution);
    else if (configured) setCortexNotice('Cortex bereit. LM Studio prüfen oder direkt eine kognitive Anfrage starten.');
    else setCortexNotice('Cortex nicht konfiguriert.', 'error');

    const requestId = nextState.last_request?.id || null;
    if (nextState.imagination_executable && $('cortex-auto-execute')?.checked && requestId
        && requestId !== lastAutoExecutedRequest && !cortexAutoExecuting && !cortexBusy) {
      lastAutoExecutedRequest = requestId;
      setTimeout(() => executeCortexImagination(true).catch(exception => setCortexNotice(exception.message, 'error')), 30);
    }
  }

  async function cortexAction(action, extra = {}, quiet = false) {
    if (cortexBusy) return cortexState;
    cortexBusy = true;
    if (!quiet) setCortexNotice(`Cortex: ${action} …`);
    try {
      const payload = await postJson('/api/cortex/control', {action, ...extra});
      cortexBusy = false;
      updateCortex(payload);
      return payload;
    } catch (exception) {
      cortexBusy = false;
      setCortexNotice(exception.message, 'error');
      if (!quiet) showError(`Cortex: ${exception.message}`);
      updateCortex(cortexState || {available: false, configured: false});
      throw exception;
    }
  }

  async function ensureExecutiveGoal() {
    if (cortexActiveGoal()) return cortexActiveGoal();
    const text = cortexGoalText();
    if (!text) return null;
    await cortexAction('add_goal', {text, priority: Number($('cortex-priority')?.value || .9)}, true);
    return cortexActiveGoal();
  }

  async function submitCortex(action) {
    const text = cortexGoalText();
    if (action === 'plan' || action === 'autonomous_tick') await ensureExecutiveGoal();
    await cortexAction(action, {goal: text});
  }

  async function executeCortexImagination(automatic = false) {
    if (cortexAutoExecuting) return;
    cortexAutoExecuting = true;
    try {
      const payload = await cortexAction('execute_imagination', {}, automatic);
      if (payload?.imaginatio && payload?.trace) {
        updateState(payload.imaginatio, payload.trace, false);
        setText('operation-notice', automatic ? 'Cortex-IMAGINATIO wurde nach Arbiter-Freigabe ausgeführt.' : 'Cortex-IMAGINATIO ausgeführt.');
        await replay();
      }
    } finally { cortexAutoExecuting = false; }
  }

  function showError(message = '') {
    const banner = $('error-banner');
    banner.textContent = message;
    banner.classList.toggle('visible', Boolean(message));
  }

  function line(pixels, x0, y0, x1, y1, color) {
    const scale = side / 32;
    x0 *= scale; y0 *= scale; x1 *= scale; y1 *= scale;
    x0 = Math.round(x0); y0 = Math.round(y0); x1 = Math.round(x1); y1 = Math.round(y1);
    const dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    let error = dx + dy;
    for (;;) {
      setRgbBrush(pixels, x0, y0, color, Math.max(2, Math.round(scale * .16)));
      if (x0 === x1 && y0 === y1) break;
      const twice = 2 * error;
      if (twice >= dy) { error += dy; x0 += sx; }
      if (twice <= dx) { error += dx; y0 += sy; }
    }
  }

  function circle(pixels, cx, cy, radius, start = 0, end = Math.PI * 2, color = [1, 1, 1], rainbow = false) {
    const scale = side / 32;
    cx *= scale; cy *= scale; radius *= scale;
    const steps = Math.max(40, Math.ceil(radius * Math.abs(end - start) * 2));
    for (let index = 0; index <= steps; index++) {
      const angle = start + (end - start) * index / steps;
      setRgbBrush(pixels, cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius,
        rainbow ? hsv(index / steps * 360) : color);
    }
  }

  function loadPreset(name) {
    target.fill(0);
    if (name === 'circle') circle(target, 15.5, 15.5, 10.5, 0, Math.PI * 2, [1, 1, 1], true);
    if (name === 'cross') {
      line(target, 7, 7, 24, 24, [1, .16, .28]);
      line(target, 24, 7, 7, 24, [.12, .65, 1]);
    }
    if (name === 'triangle') {
      line(target, 16, 5, 5, 25, [1, .24, .42]);
      line(target, 5, 25, 27, 25, [.2, 1, .48]);
      line(target, 27, 25, 16, 5, [.2, .55, 1]);
    }
    if (name === 'tree') {
      line(target, 15, 27, 15, 15, [.48, .22, .07]); line(target, 16, 27, 16, 15, [.48, .22, .07]);
      line(target, 15, 18, 8, 12, [.15, .84, .35]); line(target, 16, 18, 24, 10, [.08, .66, .27]);
      line(target, 15, 14, 11, 7, [.34, .94, .42]); line(target, 16, 14, 20, 6, [.18, .74, .32]);
      line(target, 10, 27, 21, 27, [.7, .38, .12]);
    }
    if (name === 'house') {
      line(target, 7, 14, 16, 5, [1, .3, .22]); line(target, 16, 5, 25, 14, [1, .3, .22]);
      line(target, 7, 14, 7, 27, [.95, .72, .25]); line(target, 25, 14, 25, 27, [.95, .72, .25]);
      line(target, 7, 27, 25, 27, [.95, .72, .25]); line(target, 14, 27, 14, 20, [.24, .65, 1]);
      line(target, 19, 27, 19, 20, [.24, .65, 1]); line(target, 14, 20, 19, 20, [.24, .65, 1]);
    }
    if (name === 'moon') {
      circle(target, 15, 15, 11, Math.PI * .42, Math.PI * 1.58, [1, .84, .25]);
      circle(target, 19, 15, 8, Math.PI * .55, Math.PI * 1.45, [.58, .48, 1]);
      line(target, 13, 5, 18, 7, [1, .84, .25]); line(target, 13, 25, 18, 23, [.58, .48, 1]);
    }
    const meta = presetMeta[name] || ['', ''];
    $('concept').value = meta[0]; $('symbol').value = meta[1]; $('category').value = meta[2];
    loadedDataset = []; loadedDatasetIndex = -1; updateDatasetStatus();
    render(cueCtx, target); updateSensors();
  }

  function render(context, pixels) {
    const width = context.canvas.width, height = context.canvas.height;
    const image = context.createImageData(side, side);
    for (let pixel = 0; pixel < side * side; pixel++) {
      image.data[pixel * 4] = byte(pixels[pixel * 3]);
      image.data[pixel * 4 + 1] = byte(pixels[pixel * 3 + 1]);
      image.data[pixel * 4 + 2] = byte(pixels[pixel * 3 + 2]);
      image.data[pixel * 4 + 3] = 255;
    }
    if (width === side && height === side) context.putImageData(image, 0, 0);
    else {
      const buffer = document.createElement('canvas'); buffer.width = side; buffer.height = side;
      buffer.getContext('2d').putImageData(image, 0, 0);
      context.imageSmoothingEnabled = true; context.clearRect(0, 0, width, height);
      context.drawImage(buffer, 0, 0, width, height);
    }
  }

  function cursorAt(x, y) {
    $('cursor').style.left = `${(Number(x) + .5) / side * 100}%`;
    $('cursor').style.top = `${(Number(y) + .5) / side * 100}%`;
  }

  function fillGrid(containerId, size) {
    const container = $(containerId); container.innerHTML = '';
    for (let index = 0; index < size * size; index++) container.appendChild(document.createElement('i'));
  }

  function pooled(pixels, cellX, cellY) {
    const sum = [0, 0, 0]; let maximum = 0, samples = 0;
    const cellSide = side / 4;
    const sampleStride = Math.max(1, Math.floor(cellSide / 16));
    for (let y = cellY * cellSide; y < (cellY + 1) * cellSide; y += sampleStride) {
      for (let x = cellX * cellSide; x < (cellX + 1) * cellSide; x += sampleStride) {
        const color = rgbAt(pixels, x, y);
        sum[0] += color[0]; sum[1] += color[1]; sum[2] += color[2];
        maximum = Math.max(maximum, luminance(color)); samples++;
      }
    }
    return [sum.map(channel => channel / Math.max(1, samples)), maximum];
  }

  function updateSensors(cursor = state?.canvas?.cursor || [256, 256]) {
    ['reference', 'canvas'].forEach(kind => {
      const source = kind === 'reference' ? target : output;
      const cells = $(`retina-${kind}`).children;
      for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) {
        const [color, maximum] = pooled(source, x, y);
        cells[y * 4 + x].style.background = `rgb(${byte(color[0])} ${byte(color[1])} ${byte(color[2])})`;
        cells[y * 4 + x].title = `RGB ${color.map(byte).join(' / ')} · Lmax ${fmt(maximum, 2)}`;
      }
    });
    const local = $('local-field').children;
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
      const x = Number(cursor[0]) + dx, y = Number(cursor[1]) + dy;
      const color = x >= 0 && y >= 0 && x < side && y < side ? rgbAt(output, x, y) : [0, 0, 0];
      local[(dy + 1) * 3 + dx + 1].style.background = `rgb(${byte(color[0])} ${byte(color[1])} ${byte(color[2])})`;
      local[(dy + 1) * 3 + dx + 1].title = `RGB ${color.map(byte).join(' / ')}`;
    }
  }

  function setMode(next) {
    mode = next;
    document.querySelectorAll('.mode').forEach(button => button.classList.toggle('active', button.dataset.mode === mode));
    const info = modeInfo[mode];
    setText('cue-title', info.title); setText('cue-copy', info.copy); setText('primary-action', info.button);
    $('secondary-action').hidden = !info.secondary;
    if (info.secondary) setText('secondary-action', info.secondary);
    $('scene-tools').hidden = !['scene', 'future'].includes(mode);
    $('future-tools').hidden = mode !== 'future';
    setVisibility(info.visibility === 'hidden'); renderChips();
  }

  function setVisibility(hidden) {
    $('removed-overlay').classList.toggle('visible', hidden);
    const badge = $('visibility-badge'); badge.className = `badge ${hidden ? 'hidden' : 'visible'}`;
    badge.textContent = hidden ? 'VORLAGE ENTFERNT' : 'RGB-VORLAGE SICHTBAR';
  }

  function renderChips() {
    const box = $('composition-chips');
    const fusionMode = mode === 'fusion';
    const items = fusionMode ? (state?.category_engrams || []) : (state?.symbol_engrams || []);
    const field = fusionMode ? $('category') : $('symbol');
    box.innerHTML = ''; box.hidden = !['compose', 'fusion'].includes(mode) || !items.length;
    if (box.hidden) return;
    const selected = field.value.split(',').map(value => value.trim().toUpperCase());
    items.forEach(item => {
      const name = fusionMode ? item.category : item.symbol;
      const button = document.createElement('button'); button.type = 'button';
      button.className = `chip ${selected.includes(String(name).toUpperCase()) ? 'selected' : ''}`;
      button.textContent = fusionMode ? `${name} · ${item.examples}` : name;
      button.addEventListener('click', () => {
        const current = new Set(field.value.split(',').map(value => value.trim()).filter(Boolean));
        current.has(name) ? current.delete(name) : current.add(name);
        field.value = [...current].slice(0, 5).join(', '); renderChips();
      });
      box.appendChild(button);
    });
  }

  function grayscaleToRgb(pixels) {
    const rgb = new Float32Array(pixels.length * 3);
    pixels.forEach((value, index) => rgb.set([value, value, value], index * 3));
    return rgb;
  }

  function updateState(nextState, nextTrace, renderFinal = true) {
    state = nextState; trace = nextTrace || {events: [], summary: {}};
    const visualEngrams = state.visual_engrams || [];
    setText('m-visual', visualEngrams.length);
    setText('m-regions', visualEngrams.reduce((sum, engram) => sum + Number(engram.region_tokens || 0), 0).toLocaleString('de-DE'));
    setText('m-strokes', visualEngrams.reduce((sum, engram) => sum + Number(engram.stroke_tokens || 0), 0).toLocaleString('de-DE'));
    setText('m-symbols', (state.symbol_engrams || []).length);
    setText('m-categories', (state.category_engrams || []).length);
    setText('m-poses', (state.pose_engrams || []).length);
    setText('m-relations', (state.relation_engrams || []).length);
    setText('m-actions', (state.action_engrams || []).length);
    if (renderFinal) {
      const rgb = state.canvas?.rgb_pixels;
      const luminancePixels = state.canvas?.pixels;
      if (state.canvas?.rgb8_base64) output.set(rgbFloatsFromBase64(state.canvas.rgb8_base64));
      else if (rgb?.length === output.length) output.set(rgb);
      else if (luminancePixels?.length === side * side) output.set(grayscaleToRgb(luminancePixels));
      render(resultCtx, output); cursorAt(...(state.canvas?.cursor || [256, 256]));
      const brush = state.canvas?.brush_color || [state.canvas?.brush_tone || 1, state.canvas?.brush_tone || 1, state.canvas?.brush_tone || 1];
      setText('cursor-readout', `Cursor ${(state.canvas?.cursor || [256, 256]).join(' / ')} · RGB ${brush.map(byte).join(' / ')}`);
      updateSensors();
    }
    renderChips();
  }

  function featureSummary(result = state?.last_result) {
    const labels = {BACKGROUND: 'Hintergrund', UPPER_LEFT: 'oben links', UPPER_CENTER: 'oben Mitte', UPPER_RIGHT: 'oben rechts', MIDDLE_LEFT: 'Mitte links', CENTER: 'Zentrum', MIDDLE_RIGHT: 'Mitte rechts', LOWER_LEFT: 'unten links', LOWER_CENTER: 'unten Mitte', LOWER_RIGHT: 'unten rechts'};
    const sources = result?.feature_sources || [];
    if (!sources.length) return '';
    return sources.map(item => `${labels[item.feature] || item.feature} ← Quelle ${Number(item.source_index) + 1}`).join(' · ');
  }

  function updateLive(live) {
    const metrics = live.metrics || {}, biology = live.biology || {}, physiology = live.physiology || {};
    setText('m-experiences', Number(live.experiences ?? metrics.experiences ?? 0).toLocaleString('de-DE'));
    setText('m-spikes', Number(metrics.total_spikes || 0).toLocaleString('de-DE'));
    setText('m-dendrites', Number(biology.dendritic_spikes || 0).toLocaleString('de-DE'));
    setText('m-synapses', Number(metrics.active_synapses || 0).toLocaleString('de-DE'));
    setText('m-atp', pct(physiology.atp || 0));
  }

  function neural(event) {
    const neuralState = event?.neural || {}, plasticity = event?.plasticity || {}, metabolism = event?.metabolism || {};
    const motor = neuralState.motor || {}, directions = motor.directional_activity || [0, 0, 0, 0];
    setText('n-assembly', neuralState.active_assembly || '—'); setText('n-predicted', neuralState.predicted_assembly || '—');
    setText('n-prospection', fmt(neuralState.prospective_activation || 0, 3)); setText('n-error', fmt(neuralState.prediction_error || 0, 3));
    setText('n-motor-confidence', pct(motor.confidence || 0)); setText('n-familiarity', pct(neuralState.sequence_familiarity || 0));
    setText('n-tags', `${Number(plasticity.tagged_synapses || 0).toLocaleString('de-DE')} · ${fmt(plasticity.synaptic_tag || 0, 3)}`);
    setText('n-creb', `${fmt(plasticity.creb || 0, 3)} / ${fmt(plasticity.protein || 0, 3)}`);
    setText('n-atp', `${pct(metabolism.atp || 0)} / ${pct(metabolism.sleep_pressure || 0)}`);
    setText('n-dendrites', Number(neuralState.dendritic_spikes || 0).toLocaleString('de-DE'));
    ['n', 'e', 's', 'w'].forEach((direction, index) => {
      setBar(`motor-${direction}`, directions[index] || 0); setText(`motor-${direction}-v`, fmt(directions[index] || 0, 2));
    });
    setText('cause-title', `Auswahlquelle: ${causeLabels[event?.cause] || event?.cause || '—'}`);
    const pigment = event?.action?.color;
    const recalled = event?.cause === 'recalled_sensorimotor_engram';
    setText('cause-copy', event?.action
      ? recalled
        ? `V14 reaktiviert für diese Malhandlung das gekoppelte Self-Motor-Engramm der eigenen früheren Zeichnung. ${pigment ? `Aktuelles Pigment ${pigment.map(byte).join(' / ')}.` : ''} Es wird kein gespeicherter Source-/Teacher-Patch abgespielt.`
        : `Bewegung und ${pigment ? `Pigment ${pigment.map(byte).join(' / ')}` : 'Pinselstärke'} stammen aus dieser aktuellen Aktionsspur; die Richtungs-Populationen bleiben gemessener Gewebezustand.`
      : 'Das RGB-Sensorereignis aktiviert Assembly und Prospektion des persistenten Nervensystems.');
  }

  function flow(name) {
    ['see', 'neural', 'act', 'canvas'].forEach(part => $(`flow-${part}`).classList.toggle('active', part === name));
  }

  function stream(events, current) {
    const box = $('action-stream'); box.innerHTML = '';
    events.slice(Math.max(0, current - 10), Math.min(events.length, current + 1)).forEach(event => {
      const row = document.createElement('div'); row.className = `action-row ${event.sequence === events[current]?.sequence ? 'current' : ''}`;
      const sequence = document.createElement('span'); sequence.textContent = String(event.sequence).padStart(6, '0');
      const action = document.createElement('b'); action.textContent = event.action ? actionLabels[event.action.kind] || event.action.kind : 'SENSE_RGB';
      const detail = document.createElement('span');
      detail.textContent = event.action ? `${event.action.color ? hexColor(event.action.color) : `${event.action.repetitions}×`} · A${event.neural?.active_assembly || '—'}` : `A${event.neural?.active_assembly || '—'}`;
      row.append(sequence, action, detail); box.appendChild(row);
    });
    box.scrollTop = box.scrollHeight;
  }

  const wait = milliseconds => new Promise(resolve => setTimeout(resolve, Math.max(0, milliseconds)));
  async function replay() {
    const events = trace.events || [];
    if (!events.length) return;
    const token = ++replayToken, totalActions = events.filter(event => event.action).length;
    let actionIndex = 0; replaying = true;
    if (totalActions) { output.fill(0); render(resultCtx, output); cursorAt(256, 256); }
    setText('canvas-stage', state?.last_result?.stage || 'RGB-Malvorgang');
    const speed = $('replay-speed').value;
    const delay = speed === 'live' ? 28 : speed === 'fast' ? 4 : 0;
    const visualStride = speed === 'live' ? 1 : speed === 'fast' ? (totalActions > 1000 ? 32 : 2) : 64;
    for (let index = 0; index < events.length; index++) {
      if (token !== replayToken) break;
      const event = events[index];
      const visualStep = !event.action || actionIndex % visualStride === 0 || index === events.length - 1;
      if (visualStep) stream(events, index);
      if (!event.action) {
        flow('see'); setVisibility(!event.reference_visible); if (delay) await wait(delay);
        neural(event); flow('neural'); if (delay) await wait(delay); continue;
      }
      if (['recall', 'symbol', 'compose', 'fusion', 'scene', 'future'].includes(mode)) setVisibility(true);
      if (visualStep) {
        neural(event); flow('neural');
        if (delay) await wait(delay); flow('act');
        document.querySelectorAll('.vocab').forEach(value => value.classList.toggle('active', value.dataset.action === event.action.kind));
      }
      const changes = event.changed_colors || [];
      if (changes.length) changes.forEach(([pixel, red, green, blue]) => output.set([red, green, blue], pixel * 3));
      else (event.changed_pixels || []).forEach(([pixel, value]) => output.set([value, value, value], pixel * 3));
      if (event.changed_rgb8_base64) {
        const packed = base64Bytes(event.changed_rgb8_base64);
        for (let offset = 0; offset + 6 < packed.length; offset += 7) {
          const pixel = packed[offset] | (packed[offset + 1] << 8)
            | (packed[offset + 2] << 16) | (packed[offset + 3] << 24);
          output[pixel * 3] = packed[offset + 4] / 255;
          output[pixel * 3 + 1] = packed[offset + 5] / 255;
          output[pixel * 3 + 2] = packed[offset + 6] / 255;
        }
      }
      const shouldRender = visualStep;
      if (shouldRender) { render(resultCtx, output); cursorAt(...(event.cursor_after || [16, 16])); updateSensors(event.cursor_after); }
      if (visualStep) flow('canvas'); actionIndex++;
      if (visualStep) {
        setText('action-counter', `${actionIndex} / ${totalActions}`); $('action-progress').style.width = `${totalActions ? actionIndex / totalActions * 100 : 0}%`;
      }
      const pigment = event.action.color || [event.action.intensity, event.action.intensity, event.action.intensity];
      if (visualStep) setText('cursor-readout', `Cursor ${(event.cursor_after || [16, 16]).join(' / ')} · ${actionLabels[event.action.kind]} · RGB ${pigment.map(byte).join(' / ')}`);
      if (visualStep && delay) await wait(delay); else if (actionIndex % 64 === 0) await wait(0);
    }
    if (token === replayToken) {
      replaying = false; render(resultCtx, output); updateSensors(); flow('');
      document.querySelectorAll('.vocab').forEach(value => value.classList.remove('active'));
      const parts = featureSummary();
      setText('operation-notice', parts
        ? `Teilmerkmale neu erzeugt: ${parts}.`
        : 'RGB-Spur abgeschlossen: Farbsehen, Nervensystem, Pigmenthandlung und Leinwand bleiben kausal getrennt prüfbar.');
    }
  }

  function setBusy(value, message = '') {
    busy = value;
    document.querySelectorAll('#primary-action,#secondary-action,#recognize-current,#learn-pose,#batch-train,#canvas-reset,#save-image,#archive-image').forEach(button => button.disabled = value);
    const status = $('system-status'); status.className = `status ${value ? '' : 'live'}`;
    status.querySelector('span').textContent = value ? 'Nervensystem arbeitet' : 'System aktiv';
    if (message) setText('operation-notice', message);
  }

  function optionalNumber(raw, label) {
    const value = String(raw ?? '').trim();
    if (!value) return null;
    const number = Number(value.replace(',', '.'));
    if (!Number.isFinite(number)) throw new Error(`${label} ist keine gültige Zahl.`);
    return number;
  }

  function sceneFromEditor(objectText, relationText, name) {
    const objects = String(objectText || '').split(/\r?\n/).map(line => line.trim()).filter(Boolean).map((line, row) => {
      const fields = line.split('|').map(value => value.trim());
      if (fields.length < 2 || !fields[0] || !fields[1]) {
        throw new Error(`Objektzeile ${row + 1}: Instanz und Kategorie fehlen.`);
      }
      const object = {
        instance: fields[0], category: fields[1], pose: fields[2] || 'CANONICAL',
      };
      const values = [
        ['x', fields[3]], ['y', fields[4]], ['scale', fields[5]],
        ['depth', fields[6]], ['rotation', fields[7]],
      ];
      values.forEach(([key, raw]) => {
        const value = optionalNumber(raw, `Objektzeile ${row + 1} · ${key}`);
        if (value !== null) object[key] = value;
      });
      return object;
    });
    if (!objects.length) throw new Error('Mindestens ein Szenenobjekt wird benötigt.');
    const relations = String(relationText || '').split(/\r?\n/).map(line => line.trim()).filter(Boolean).map((line, row) => {
      const fields = line.split('|').map(value => value.trim());
      if (fields.length < 3 || !fields[0] || !fields[1] || !fields[2]) {
        throw new Error(`Relationszeile ${row + 1}: erwartet wird Instanz | Relation | Instanz.`);
      }
      return {subject: fields[0], relation: fields[1].toLowerCase(), object: fields[2], confidence: 1};
    });
    return {name, objects, relations, background: [0, 0, 0]};
  }

  function actionsFromEditor(text) {
    const actions = String(text || '').split(/\r?\n/).map(line => line.trim()).filter(Boolean).map((line, row) => {
      const fields = line.split('|').map(value => value.trim());
      if (fields.length < 3 || !fields[0] || !fields[1]) {
        throw new Error(`Handlungszeile ${row + 1}: Handlung, Akteur und Ziel fehlen.`);
      }
      return {
        kind: fields[0].toLowerCase(), actor: fields[1], target: fields[2],
        direction: [optionalNumber(fields[3], 'Richtung X') ?? 0, optionalNumber(fields[4], 'Richtung Y') ?? 0],
        magnitude: optionalNumber(fields[5], 'Stärke') ?? 1,
        duration: optionalNumber(fields[6], 'Dauer') ?? 1,
      };
    });
    if (!actions.length) throw new Error('Mindestens eine Handlung wird benötigt.');
    return actions;
  }

  async function run(action) {
    if (busy) return;
    $('operation-notice').className = 'notice'; setBusy(true, 'TATARUS verarbeitet RGB-Retina und serielle Pigmenthandlungen …'); showError('');
    try {
      const command = {action};
      if (['learn', 'learn_pose', 'recognize', 'recall', 'associate_symbol'].includes(action)) {
        command.rgb8_base64 = rgbBase64(target); command.width = side; command.height = side;
        command.source_width = sourceImageWidth; command.source_height = sourceImageHeight;
      }
      if (action === 'recognize') {
        command.category = $('category').value.trim();
        command.maximum_matches = 3;
        command.maximum_objects = 16;
      }
      if (action === 'learn') command.patch_side = 8;
      if (action === 'learn_pose') {
        command.patch_side = 8;
        command.text = $('concept').value;
        command.category = $('pose-category').value;
        command.pose = $('pose-name').value;
      }
      if (action === 'learn' && $('category').value.trim()) command.category = $('category').value.trim().split(',')[0];
      if (action === 'fuse_categories') {
        command.category = $('category').value; command.variation_seed = Number($('variation-seed').value) || 0;
      }
      if (['observe_scene', 'draw_scene', 'learn_transition', 'imagine_future'].includes(action)) {
        const baseName = $('concept').value.trim() || 'SZENE';
        const scene = sceneFromEditor($('scene-objects').value, $('scene-relations').value, baseName);
        command.variation_seed = Number($('variation-seed').value) || 0;
        if (action === 'observe_scene' || action === 'draw_scene') command.scene = scene;
        if (action === 'learn_transition') {
          command.before = scene;
          command.after = sceneFromEditor($('future-objects').value, '', `${baseName}-FOLGE`);
          command.scene_action = actionsFromEditor($('future-actions').value)[0];
        }
        if (action === 'imagine_future') {
          command.initial = scene;
          command.actions = actionsFromEditor($('future-actions').value);
        }
      }
      if (action === 'learn' || action === 'draw_free') command.text = $('concept').value;
      if (['associate_symbol', 'draw_symbol', 'compose'].includes(action)) command.text = $('symbol').value;
      const payload = await postJson('/api/imaginatio/control', command);
      if (action === 'recognize') {
        const recognition = payload.recognition || {};
        if (recognition.mode === 'multi_object_scene' || Array.isArray(recognition.objects)) {
          const objects = recognition.objects || [];
          const details = objects.map((object, index) => {
            const semantic = object.category || object.instance || `Objekt ${index + 1}`;
            const memory = object.object_engram_id
              ? `ID ${object.object_engram_id} · Gedächtnis ${pct(object.object_memory_similarity || 0)}`
              : 'neues Objekt';
            const evidence = (object.matches || [])[0];
            const categoryEvidence = evidence
              ? ` · Bild ${pct(evidence.bottom_up_similarity || 0)}`
                + ` · Erwartung +${pct(evidence.top_down_boost || 0)}`
              : '';
            return `${semantic} [${memory}${categoryEvidence}]`;
          }).join(' · ');
          setText(
            'operation-notice',
            `${payload.label} · ${recognition.recognized_count || 0}/${recognition.candidate_count || 0} semantisch erkannt`
            + ` · ${recognition.novel_count || 0} neu`
            + (details ? ` · ${details}` : '')
          );
        } else {
          const matches = (recognition.matches || []).map(match =>
            match.category + ': ' + pct(match.score)
            + ' (Bild ' + pct(match.bottom_up_similarity)
            + ', Erwartung +' + pct(match.top_down_boost) + ')'
          ).join(' · ');
          const decision = recognition.recognized ? 'erkannt' : 'noch nicht eindeutig';
          setText(
            'operation-notice',
            payload.label + ' · ' + decision
            + ' · Areal ' + (recognition.dominant_ventral_area || '—')
            + (matches ? ' · ' + matches : '')
          );
        }
        setBusy(false);
        return;
      }
      if (['fuse_categories', 'draw_scene', 'imagine_future'].includes(action)) {
        $('variation-seed').value = String((Number($('variation-seed').value) || 0) + 1);
      }
      updateState(payload.imaginatio, payload.trace, false);
      if (!(payload.trace.events || []).some(event => event.action)) updateState(payload.imaginatio, payload.trace, true);
      setText('operation-notice', `${payload.label}. Kausale RGB-Spur wird wiedergegeben …`); setBusy(false); await replay();
    } catch (exception) {
      setBusy(false); $('operation-notice').className = 'notice error';
      setText('operation-notice', `Nicht ausgeführt: ${exception.message}`); showError(exception.message);
    }
  }

  function actionForMode() { return {learn: 'learn', recall: 'recall', symbol: 'draw_symbol', compose: 'compose', fusion: 'fuse_categories', scene: 'draw_scene', future: 'imagine_future'}[mode]; }

  function secondaryActionForMode() {
    return {symbol: 'associate_symbol', scene: 'observe_scene', future: 'learn_transition'}[mode];
  }

  function filenameConcept(file) { return file.name.replace(/\.[^.]+$/, '').replace(/[_-]+/g, ' ').trim().slice(0, 120) || 'Farbbild'; }

  async function imageFileToPixels(file) {
    const image = new Image();
    const url = URL.createObjectURL(file);
    try {
      await new Promise((resolve, reject) => {
        image.onload = resolve; image.onerror = () => reject(new Error(`Bild nicht lesbar: ${file.name}`)); image.src = url;
      });
      sourceImageWidth = image.naturalWidth || side;
      sourceImageHeight = image.naturalHeight || side;
      const canvas = document.createElement('canvas'); canvas.width = side; canvas.height = side;
      const context = canvas.getContext('2d', {willReadFrequently: true});
      const profile = $('import-profile').value;
      context.fillStyle = profile === 'text' ? '#ffffff' : '#000000'; context.fillRect(0, 0, side, side);
      context.imageSmoothingEnabled = profile !== 'graphic'; context.imageSmoothingQuality = 'high';
      const scale = $('image-fit').value === 'contain'
        ? Math.min(side / image.naturalWidth, side / image.naturalHeight)
        : Math.max(side / image.naturalWidth, side / image.naturalHeight);
      const width = image.naturalWidth * scale, height = image.naturalHeight * scale;
      context.drawImage(image, (side - width) / 2, (side - height) / 2, width, height);
      const data = context.getImageData(0, 0, side, side).data;
      const result = new Float32Array(side * side * 3);
      for (let pixel = 0; pixel < side * side; pixel++) {
        for (let channel = 0; channel < 3; channel++) {
          let value = data[pixel * 4 + channel] / 255;
          if (profile === 'text') value = clamp((value - .5) * 1.55 + .5);
          result[pixel * 3 + channel] = value;
        }
      }
      return result;
    } finally { URL.revokeObjectURL(url); }
  }

  function updateDatasetStatus() {
    const count = loadedDataset.length;
    $('batch-train').hidden = count < 2;
    setText('batch-train', count > 1 ? `Alle ${count} Bilder in einem Lauf lernen` : 'Alle geladenen Bilder in einem Lauf lernen');
    if (!count) setText('dataset-status', 'Fotos, Grafiken und Textbilder · echte 512×512-Auflösung · 24-Bit-sRGB');
    else setText('dataset-status', `${count} Bild${count === 1 ? '' : 'er'} geladen · aktuell ${loadedDatasetIndex + 1}/${count}: ${loadedDataset[loadedDatasetIndex]?.name || '—'} · Quelle ${sourceImageWidth}×${sourceImageHeight} → Retina ${side}×${side}`);
  }

  async function loadDatasetImage(index) {
    if (!loadedDataset[index]) return;
    loadedDatasetIndex = index; target.set(await imageFileToPixels(loadedDataset[index]));
    $('concept').value = filenameConcept(loadedDataset[index]); $('preset').value = 'blank';
    render(cueCtx, target); updateSensors(); updateDatasetStatus();
  }

  async function trainDataset() {
    if (busy || loadedDataset.length < 2) return;
    if (statusData.evaluation_mode) { showError('Für neue Farbbild-Engramme zuerst den Lernmodus aktivieren.'); return; }
    const maximumEngrams = Math.max(1, Number(state?.analysis?.maximum_engrams) || 64);
    if (loadedDataset.length > maximumEngrams) {
      showError(`Der aktuelle Organismus hält höchstens ${maximumEngrams} Bildengramme. Bitte den Datensatz aufteilen.`);
      return;
    }
    setBusy(true); showError('');
    try {
      let lastPayload = null;
      for (let index = 0; index < loadedDataset.length; index++) {
        await loadDatasetImage(index);
        setText('operation-notice', `RGB-Datensatz ${index + 1}/${loadedDataset.length}: ${loadedDataset[index].name} wird neuronal in V14 gelernt …`);
        lastPayload = await postJson('/api/imaginatio/control', {
          action: 'learn', rgb8_base64: rgbBase64(target), width: side, height: side,
          source_width: sourceImageWidth, source_height: sourceImageHeight,
          patch_side: 8, text: filenameConcept(loadedDataset[index]),
          category: $('category').value.trim().split(',')[0],
        });
        updateState(lastPayload.imaginatio, lastPayload.trace, false);
      }
      setBusy(false);
      setText('operation-notice', `${loadedDataset.length} Farbbilder gelernt. Die letzte kausale RGB-Spur wird wiedergegeben …`);
      if (lastPayload) await replay();
    } catch (exception) {
      setBusy(false); $('operation-notice').className = 'notice error';
      setText('operation-notice', `Datensatz abgebrochen: ${exception.message}`); showError(exception.message);
    }
  }

  async function exportWork(archive) {
    if (busy) return;
    try {
      const title = $('concept').value || (mode === 'compose' ? $('symbol').value : '') || 'IMAGINATIO Farbbild';
      const [outputWidth, outputHeight] = String($('output-resolution')?.value || '3840x2160')
        .split('x').map(value => Number(value));
      setText('operation-notice', archive
        ? `TATARUS führt die erinnerte Motorikspur direkt auf einer neuen ${outputWidth} × ${outputHeight}-Leinwand aus und synthetisiert dort Pigmentdetails; anschließend wird der Zustand archiviert …`
        : `TATARUS malt direkt aus seiner Motorikspur auf eine neue ${outputWidth} × ${outputHeight}-Leinwand; kein 512er Bild wird interpoliert …`);
      const payload = await postJson('/api/imaginatio/export', {
        title, archive, target_space_render: true, output_width: outputWidth, output_height: outputHeight,
      });
      setText('operation-notice', archive
        ? `Direkt gemaltes ${outputWidth} × ${outputHeight}-Werk vollständig archiviert.`
        : `${outputWidth} × ${outputHeight}-PNG wurde direkt auf der Zielleinwand neu gemalt und gespeichert.`);
      await refreshGallery(); openArtifact(payload.artifact);
    } catch (exception) { showError(exception.message); }
  }

  async function refreshGallery() {
    const payload = await getJson('/api/imaginatio/gallery'), box = $('gallery'); box.innerHTML = '';
    if (!payload.artifacts.length) { box.innerHTML = '<div class="empty">Noch keine Werke gespeichert.</div>'; return; }
    payload.artifacts.forEach(artifact => {
      const card = document.createElement('article'); card.className = 'panel art'; card.tabIndex = 0;
      const image = document.createElement('img'); image.src = artifact.files.png; image.alt = artifact.title;
      const title = document.createElement('h3'); title.textContent = artifact.title;
      const metadata = document.createElement('div'); metadata.className = 'art-meta';
      const renderMethod = artifact.canvas?.render_method;
      const legacyRaster = renderMethod === 'tatarus_motor_trace_native_raster';
      const oldInterpolated = renderMethod === 'tatarus_motor_trace_edge_aware_surface_reconstruction';
      const targetSpace = renderMethod === 'tatarus_normalized_motor_target_space_pigment_synthesis_v1';
      const stage = document.createElement('span');
      stage.className = legacyRaster || oldInterpolated ? 'legacy-raster-mark' : '';
      stage.textContent = legacyRaster ? 'ALT · RASTER'
        : oldInterpolated ? 'ALT · INTERPOLIERT'
        : targetSpace ? 'DIREKT GEMALT'
        : String(artifact.canvas?.color_space || artifact.stage || '').toUpperCase();
      const snapshot = document.createElement('span'); snapshot.className = artifact.snapshot_attached ? 'snapshot-mark' : '';
      snapshot.textContent = artifact.snapshot_attached ? '● SNAPSHOT' : `${artifact.result?.actions || 0} Aktionen`;
      metadata.append(stage, snapshot); card.append(image, title, metadata);
      card.addEventListener('click', () => openArtifact(artifact));
      card.addEventListener('keydown', event => { if (event.key === 'Enter') openArtifact(artifact); }); box.appendChild(card);
    });
  }

  function openArtifact(artifact) {
    selectedArtifact = artifact; setText('artifact-title', artifact.title); $('artifact-image').src = artifact.files.png;
    $('artifact-png').href = artifact.files.png; $('artifact-pgm').href = artifact.files.pgm;
    $('artifact-ppm').hidden = !artifact.files.ppm; $('artifact-ppm').href = artifact.files.ppm || '#';
    $('artifact-meta').href = artifact.files.metadata; $('artifact-trace').hidden = !artifact.files.trace; $('artifact-trace').href = artifact.files.trace || '#';
    $('artifact-load').hidden = !artifact.snapshot_attached;
    const renderMethod = artifact.canvas?.render_method;
    const legacyRaster = renderMethod === 'tatarus_motor_trace_native_raster';
    const oldInterpolated = renderMethod === 'tatarus_motor_trace_edge_aware_surface_reconstruction';
    const targetSpace = renderMethod === 'tatarus_normalized_motor_target_space_pigment_synthesis_v1';
    const informationWidth = artifact.canvas?.information_width || artifact.canvas?.working_width || 0;
    const informationHeight = artifact.canvas?.information_height || artifact.canvas?.working_height || 0;
    const renderLabel = legacyRaster ? 'VERALTETER RASTER-EXPORT · neu erzeugen'
      : oldInterpolated ? 'VERALTET · 512er Rekonstruktion wurde interpoliert'
      : targetSpace ? 'Motorikspur direkt auf der Ziel-Leinwand ausgeführt'
      : 'Arbeitscanvas';
    const rows = [
      ['Modus', artifact.stage], ['Farbraum', artifact.canvas?.color_space || 'Graustufen'], ['Kanäle', artifact.canvas?.channels || 1],
      ['Ausgabe', `${artifact.canvas?.width || 0} × ${artifact.canvas?.height || 0}`],
      ['Render', renderLabel],
      ['Informationsbasis', informationWidth && informationHeight ? `${informationWidth} × ${informationHeight}` : '—'],
      ['Raster-Interpolation', targetSpace ? 'NEIN' : (legacyRaster || oldInterpolated ? 'JA · veralteter Export' : '—')],
      ['Neue Feinstruktur', targetSpace && artifact.canvas?.synthesized_high_frequency_detail
        ? 'JA · aus gelernten Pigment-/Kantenmerkmalen synthetisiert'
        : (legacyRaster || oldInterpolated ? 'NEIN' : '—')],
      ['Detailwahrheit', targetSpace ? 'Inferenz · keine unbekannte Originalinformation' : '—'],
      ['Entstanden aus', (artifact.symbols || []).join(' + ') || artifact.operation],
      ['Engramme', (artifact.memory?.recalled_engrams || []).map(value => `#${value}`).join(' + ') || '—'],
      ['Aktionen', artifact.result?.actions || 0], ['Striche', artifact.result?.strokes || 0],
      ['RGB-Ähnlichkeit', pct(artifact.result?.similarity || 0)], ['Novelty', pct(artifact.result?.novelty || 0)],
      ['Aktives Assembly', artifact.memory?.active_assembly || '—'], ['ATP', pct(artifact.organism?.atp || 0)],
      ['CREB', fmt(artifact.organism?.creb || 0, 3)], ['Dendritenspikes', Number(artifact.organism?.dendritic_spikes || 0).toLocaleString('de-DE')],
      ['Snapshot', artifact.snapshot_attached ? 'JA' : 'NEIN'],
    ];
    const details = $('artifact-details'); details.innerHTML = '';
    rows.forEach(([label, value]) => {
      const key = document.createElement('span'); key.textContent = label;
      const entry = document.createElement('strong'); entry.textContent = value; details.append(key, entry);
    });
    $('artifact-dialog').showModal();
  }

  async function artifactAsTemplate() {
    if (!selectedArtifact) return;
    const response = await fetch(selectedArtifact.files.png);
    const blob = await response.blob();
    loadedDataset = [new File([blob], `${selectedArtifact.title}.png`, {type: 'image/png'})];
    await loadDatasetImage(0); $('artifact-dialog').close(); setMode('learn');
    setText('operation-notice', 'Gespeichertes sRGB-Werk liegt jetzt nur als neue Vorlage bereit. Erst das Lernen verändert die Nervensystemhistorie.');
  }

  async function periodic() {
    try {
      const [nextState, nextTrace, live, nextStatus, nextCortex] = await Promise.all([
        getJson('/api/imaginatio'), getJson('/api/imaginatio/trace'), getJson('/api/frame'), getJson('/api/status'),
        getJson('/api/cortex').catch(() => ({available: false, configured: false})),
      ]);
      statusData = nextStatus; updateLive(live); updateCortex(nextCortex); if (!busy && !replaying) updateState(nextState, nextTrace, true);
      $('mode-toggle').textContent = statusData.evaluation_mode ? 'Lernmodus fortsetzen' : 'Testmodus starten';
      const status = $('system-status'); if (!busy) { status.className = 'status live'; status.querySelector('span').textContent = 'RGB-System aktiv'; }
    } catch (exception) {
      const status = $('system-status'); status.className = 'status error'; status.querySelector('span').textContent = 'Verbindung getrennt';
    } finally { setTimeout(periodic, 1200); }
  }

  fillGrid('retina-reference', 4); fillGrid('retina-canvas', 4); fillGrid('local-field', 3);
  Object.keys(actionLabels).forEach(action => {
    const cell = document.createElement('div'); cell.className = 'vocab'; cell.dataset.action = action;
    cell.textContent = actionLabels[action]; $('vocabulary').appendChild(cell);
  });
  loadPreset('circle'); setMode('learn'); render(resultCtx, output); cursorAt(256, 256); updateSensors();

  document.querySelectorAll('.mode').forEach(button => button.addEventListener('click', () => setMode(button.dataset.mode)));
  $('preset').addEventListener('change', event => loadPreset(event.target.value));
  $('cue-clear').addEventListener('click', () => { target.fill(0); loadedDataset = []; loadedDatasetIndex = -1; $('preset').value = 'blank'; render(cueCtx, target); updateSensors(); updateDatasetStatus(); });
  $('paint-color').addEventListener('input', event => setText('color-readout', `${event.target.value.toUpperCase()} · sRGB`));

  function paintCue(event) {
    const rectangle = cue.getBoundingClientRect();
    const x = Math.floor((event.clientX - rectangle.left) / rectangle.width * side);
    const y = Math.floor((event.clientY - rectangle.top) / rectangle.height * side);
    setRgbBrush(target, x, y, event.buttons === 2 ? [0, 0, 0] : colorFromHex($('paint-color').value), 4);
    render(cueCtx, target); updateSensors();
  }
  cue.addEventListener('contextmenu', event => event.preventDefault());
  cue.addEventListener('pointerdown', event => { pointerDown = true; cue.setPointerCapture(event.pointerId); paintCue(event); });
  cue.addEventListener('pointermove', event => { if (pointerDown) paintCue(event); });
  cue.addEventListener('pointerup', () => pointerDown = false); cue.addEventListener('pointercancel', () => pointerDown = false);

  $('image-load').addEventListener('click', event => { event.preventDefault(); $('image-file').click(); });
  $('image-file').addEventListener('change', async event => {
    try {
      loadedDataset = Array.from(event.target.files || []).filter(file => file.type.startsWith('image/'));
      if (!loadedDataset.length) return;
      await loadDatasetImage(0); setMode('learn'); showError('');
    } catch (exception) { showError(exception.message); }
  });
  ['image-fit', 'import-profile'].forEach(id => $(id).addEventListener('change', async () => {
    try { if (loadedDatasetIndex >= 0) await loadDatasetImage(loadedDatasetIndex); } catch (exception) { showError(exception.message); }
  }));

  $('primary-action').addEventListener('click', () => run(actionForMode()));
  $('secondary-action').addEventListener('click', () => run(secondaryActionForMode()));
  $('recognize-current').addEventListener('click', () => run('recognize'));
  $('learn-pose').addEventListener('click', () => run('learn_pose'));
  $('batch-train').addEventListener('click', trainDataset);
  $('canvas-reset').addEventListener('click', () => run('reset_canvas'));
  $('save-image').addEventListener('click', () => exportWork(false));
  $('archive-image').addEventListener('click', () => exportWork(true));
  $('gallery-refresh').addEventListener('click', refreshGallery);
  $('artifact-close').addEventListener('click', () => $('artifact-dialog').close());
  $('artifact-template').addEventListener('click', () => artifactAsTemplate().catch(exception => showError(exception.message)));
  $('artifact-load').addEventListener('click', async () => {
    if (!selectedArtifact) return;
    try {
      const payload = await postJson('/api/imaginatio/gallery/control', {action: 'load_snapshot', id: selectedArtifact.id});
      updateState(payload.imaginatio, payload.trace, true); $('artifact-dialog').close();
      setText('operation-notice', 'Der archivierte Nervensystem- und RGB-IMAGINATIO-Zustand wurde geladen.');
    } catch (exception) { showError(exception.message); }
  });
  $('artifact-delete').addEventListener('click', async () => {
    if (!selectedArtifact || !confirm(`Werk „${selectedArtifact.title}“ unwiderruflich aus der Galerie löschen?`)) return;
    try {
      await postJson('/api/imaginatio/gallery/control', {action: 'delete', id: selectedArtifact.id});
      $('artifact-dialog').close(); await refreshGallery();
    } catch (exception) { showError(exception.message); }
  });
  $('cortex-priority').addEventListener('input', event => setText('cortex-priority-value', fmt(event.target.value, 2)));
  $('cortex-probe').addEventListener('click', () => cortexAction('probe').catch(() => {}));
  $('cortex-configure').addEventListener('click', () => cortexAction('configure').then(() => cortexAction('probe', {}, true)).catch(() => {}));
  $('cortex-add-goal').addEventListener('click', async () => {
    const text = cortexGoalText();
    if (!text) { setCortexNotice('Bitte zuerst ein Ziel oder einen Gedanken eingeben.', 'error'); return; }
    try { await cortexAction('add_goal', {text, priority: Number($('cortex-priority').value || .9)}); }
    catch (_) {}
  });
  $('cortex-analyze').addEventListener('click', () => submitCortex('analyze').catch(() => {}));
  $('cortex-imagine').addEventListener('click', () => submitCortex('imagine').catch(() => {}));
  $('cortex-hybrid').addEventListener('click', () => submitCortex('hybrid').catch(() => {}));
  $('cortex-plan').addEventListener('click', () => submitCortex('plan').catch(() => {}));
  $('cortex-autonomous').addEventListener('click', async () => {
    try {
      if (cortexState?.autonomous) await cortexAction('autonomous_stop');
      else { await ensureExecutiveGoal(); await cortexAction('autonomous_start'); }
    } catch (_) {}
  });
  $('cortex-execute').addEventListener('click', () => executeCortexImagination(false).catch(() => {}));
  $('cortex-goal-complete').addEventListener('click', async () => {
    const goal = cortexActiveGoal(); if (!goal) return;
    try { await cortexAction('complete_goal', {goal_id: Number(goal.id)}); } catch (_) {}
  });
  $('cortex-goal-cancel').addEventListener('click', async () => {
    const goal = cortexActiveGoal(); if (!goal) return;
    try { await cortexAction('cancel_goal', {goal_id: Number(goal.id)}); } catch (_) {}
  });
  $('cortex-remember').addEventListener('click', async () => {
    const key = String($('cortex-memory-key').value || '').trim();
    const value = String($('cortex-memory-value').value || '').trim();
    if (!key || !value) { setCortexNotice('Working Memory braucht Schlüssel und Inhalt.', 'error'); return; }
    try {
      await cortexAction('remember', {key, value, salience: .85});
      $('cortex-memory-key').value = ''; $('cortex-memory-value').value = '';
    } catch (_) {}
  });
  $('cortex-goal').addEventListener('keydown', event => {
    if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') {
      event.preventDefault(); submitCortex('imagine').catch(() => {});
    }
  });

  $('mode-toggle').addEventListener('click', async () => {
    try {
      statusData = await postJson('/api/control', {action: 'evaluation_mode', enabled: !statusData.evaluation_mode});
      $('mode-toggle').textContent = statusData.evaluation_mode ? 'Lernmodus fortsetzen' : 'Testmodus starten';
    } catch (exception) { showError(exception.message); }
  });

  (async () => {
    try {
      const [initialState, initialTrace, live, initialCortex] = await Promise.all([
        getJson('/api/imaginatio'), getJson('/api/imaginatio/trace'), getJson('/api/frame'),
        getJson('/api/cortex').catch(() => ({available: false, configured: false})),
      ]);
      updateState(initialState, initialTrace, true); updateLive(live); updateCortex(initialCortex); await refreshGallery(); periodic();
      if (initialCortex.available && initialCortex.configured) cortexAction('probe', {}, true).catch(() => {});
    } catch (exception) { showError(`Farblabor konnte nicht gestartet werden: ${exception.message}`); }
  })();
})();

// Initial State
let isRunning = true;
let timeScale = 0.25; // Default: slow-motion to see aging
let numWorkers = 4;
let rates = {
    INPUT: 1.2,   // HIGH priority (Events/Sec)
    PHYSICS: 1.0, // MED priority
    AI: 0.4       // LOW priority
};

// Simulated State Storage
let queues = {
    high: [],
    med: [],
    low: []
};
const MAX_CAPACITY = 15; // Small capacity for visual representation
let workers = [];
let nextEventId = 1;
let lastTickTime = performance.now();
let simTime = 0; // Simulated time in ms

// Stats counters
let stats = {
    processed: 0,
    totalLatency: 0,
    missed: 0,
    dropped: 0
};

// Fault injection states
let faultException = false;
let faultWriterLock = false;

// Producers scheduling states (next run time in sim ms)
let nextProducerRun = {
    INPUT: 0,
    PHYSICS: 0,
    AI: 0
};

// Chart.js Setup
let telemetryChart;
let chartLabels = [];
let chartHighData = [];
let chartMedData = [];
let chartLowData = [];
let chartLatencyData = [];
let lastChartUpdateSimTime = 0;
const CHART_SAMPLE_INTERVAL = 1000; // Sample every 1000ms of sim time

// DOM Elements
const btnPlayPause = document.getElementById('btn-play-pause');
const playPauseIcon = document.getElementById('play-pause-icon');
const playPauseText = document.getElementById('play-pause-text');
const btnReset = document.getElementById('btn-reset');
const speedSlider = document.getElementById('speed-slider');
const speedVal = document.getElementById('speed-val');
const workerSlider = document.getElementById('worker-slider');
const workerVal = document.getElementById('worker-val');
const rateInput = document.getElementById('rate-input');
const rateInputVal = document.getElementById('rate-input-val');
const ratePhysics = document.getElementById('rate-physics');
const ratePhysicsVal = document.getElementById('rate-physics-val');
const rateAi = document.getElementById('rate-ai');
const rateAiVal = document.getElementById('rate-ai-val');
const simStateText = document.getElementById('sim-state-text');

const countHigh = document.getElementById('count-high');
const countMed = document.getElementById('count-med');
const countLow = document.getElementById('count-low');

const trackHigh = document.getElementById('track-high');
const trackMed = document.getElementById('track-med');
const trackLow = document.getElementById('track-low');

const workersContainer = document.getElementById('workers-container');
const poolActiveCount = document.getElementById('pool-active-count');
const busLoadVal = document.getElementById('bus-load-val');
const busCapacityBar = document.getElementById('bus-capacity-bar');
const regItemInput = document.getElementById('reg-item-input');
const regItemPhysics = document.getElementById('reg-item-physics');
const regItemAi = document.getElementById('reg-item-ai');

// Fault Elements
const toggleException = document.getElementById('toggle-exception');
const toggleWriterLock = document.getElementById('toggle-writer-lock');
const lockStatusIndicator = document.getElementById('lock-status-indicator');
const lockIcon = document.getElementById('lock-icon');
const lockText = document.getElementById('lock-text');

// Logs Elements
const systemLogsContainer = document.getElementById('system-logs-container');
const btnClearLogs = document.getElementById('btn-clear-logs');

// Tab Selection
const tabButtons = document.querySelectorAll('.tab-btn');
const tabPanes = document.querySelectorAll('.tab-pane');
const activeElements = document.querySelectorAll('.active-element');
const codeExplorerCard = document.getElementById('code-explorer-card');

// Real deadlines in simulated ms
const DEADLINES = {
    INPUT: 1500,     // 1.5s
    PHYSICS: 2500,   // 2.5s
    AI: 8000         // 8.0s
};

// Aging threshold: 5.0 seconds in sim time
const AGING_THRESHOLD = 5000;

// Initialize Visualizer
function init() {
    initChart();
    setupEventListeners();
    rebuildWorkers();
    logSystem('Simulation Initialized. Time Scale: ' + (timeScale * 100) + '%', 'success');
    logSystem('Workers Spawned. Sized to match CPU cores.', 'info');
    
    // Start animation loop
    requestAnimationFrame(animationTick);
}

// Setup Chart.js
function initChart() {
    const ctx = document.getElementById('telemetryChart').getContext('2d');
    telemetryChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: chartLabels,
            datasets: [
                {
                    label: 'HIGH Queue',
                    borderColor: '#f43f5e',
                    backgroundColor: 'rgba(244, 63, 94, 0.1)',
                    borderWidth: 1.5,
                    data: chartHighData,
                    pointRadius: 0,
                    fill: false,
                },
                {
                    label: 'MED Queue',
                    borderColor: '#f59e0b',
                    backgroundColor: 'rgba(245, 158, 11, 0.1)',
                    borderWidth: 1.5,
                    data: chartMedData,
                    pointRadius: 0,
                    fill: false,
                },
                {
                    label: 'LOW Queue',
                    borderColor: '#10b981',
                    backgroundColor: 'rgba(16, 185, 129, 0.1)',
                    borderWidth: 1.5,
                    data: chartLowData,
                    pointRadius: 0,
                    fill: false,
                },
                {
                    label: 'Avg Latency (x100 ms)',
                    borderColor: '#8b5cf6',
                    backgroundColor: 'rgba(139, 92, 246, 0.1)',
                    borderWidth: 2,
                    data: chartLatencyData,
                    pointRadius: 0,
                    fill: false,
                    yAxisID: 'yLatency'
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    grid: { color: 'rgba(255,255,255,0.02)' },
                    ticks: { color: '#64748b', font: { size: 9 } }
                },
                y: {
                    title: { display: true, text: 'Queue Depth', color: '#64748b', font: { size: 9 } },
                    grid: { color: 'rgba(255,255,255,0.02)' },
                    ticks: { color: '#64748b', font: { size: 9 }, stepSize: 2 },
                    min: 0,
                    max: 16
                },
                yLatency: {
                    position: 'right',
                    title: { display: true, text: 'Latency (s)', color: '#64748b', font: { size: 9 } },
                    grid: { display: false },
                    ticks: { color: '#64748b', font: { size: 9 } },
                    min: 0
                }
            },
            plugins: {
                legend: {
                    labels: { color: '#94a3b8', font: { size: 8 } }
                }
            }
        }
    });
}

// Log message to logs console
function logSystem(msg, type = 'info') {
    const row = document.createElement('div');
    row.className = `log-row ${type}`;
    
    const timeSpan = document.createElement('span');
    timeSpan.className = 'log-time';
    timeSpan.innerText = `[${(simTime / 1000).toFixed(2)}s]`;
    
    const msgSpan = document.createElement('span');
    msgSpan.className = 'log-msg';
    msgSpan.innerText = msg;
    
    row.appendChild(timeSpan);
    row.appendChild(msgSpan);
    
    systemLogsContainer.appendChild(row);
    systemLogsContainer.scrollTop = systemLogsContainer.scrollHeight;
    
    // Cap log lines
    while (systemLogsContainer.children.length > 50) {
        systemLogsContainer.removeChild(systemLogsContainer.firstChild);
    }
}

// Event generation helpers
function pushEvent(type, priority) {
    const queueSize = getQueueSize();
    
    // Backpressure handling
    if (queueSize >= MAX_CAPACITY) {
        // Drop LOW event if available
        if (queues.low.length > 0) {
            const dropped = queues.low.shift(); // Drop oldest LOW (FIFO front)
            stats.dropped++;
            document.getElementById('stat-dropped').innerText = stats.dropped;
            logSystem(`[EventBus Overflow] Queues full. Dropped oldest LOW event ID: ${dropped.id} to make room!`, 'error');
            
            // Visual feedback on LOW lane
            document.getElementById('lane-low').classList.add('dropping');
            setTimeout(() => document.getElementById('lane-low').classList.remove('dropping'), 400);
        } else {
            // No LOW event to drop: Block producers
            logSystem(`[EventBus Backpressure] Bounded capacity reached. Producers blocked.`, 'warn');
            return false; // Dropped / Blocked
        }
    }
    
    const id = nextEventId++;
    const ev = {
        id: id,
        type: type,
        priority: priority,
        createdAt: simTime,
        lastPromotedAt: simTime,
        deadline: simTime + DEADLINES[type],
        duration: 1000 + Math.random() * 800 // Exec time: 1.0s to 1.8s
    };
    
    if (priority === 'HIGH') {
        queues.high.push(ev);
    } else if (priority === 'MEDIUM') {
        queues.med.push(ev);
    } else {
        queues.low.push(ev);
    }
    
    logSystem(`[TickSource] Pushed event ID: ${id} (${type}, Priority: ${priority})`, 'info');
    return true;
}

// Set up UI interactions
function setupEventListeners() {
    btnPlayPause.addEventListener('click', () => {
        isRunning = !isRunning;
        if (isRunning) {
            playPauseIcon.setAttribute('data-lucide', 'pause');
            playPauseText.innerText = 'Pause';
            simStateText.innerText = 'Running';
            simStateText.className = 'status-badge running';
            logSystem('Simulation resumed', 'success');
        } else {
            playPauseIcon.setAttribute('data-lucide', 'play');
            playPauseText.innerText = 'Play';
            simStateText.innerText = 'Paused';
            simStateText.className = 'status-badge paused';
            logSystem('Simulation paused', 'warn');
        }
        lucide.createImages();
    });
    
    btnReset.addEventListener('click', () => {
        queues = { high: [], med: [], low: [] };
        stats = { processed: 0, totalLatency: 0, missed: 0, dropped: 0 };
        simTime = 0;
        nextEventId = 1;
        rebuildWorkers();
        
        // Reset statistics indicators
        document.getElementById('stat-processed').innerText = '0';
        document.getElementById('stat-latency').innerText = '0.00 ms';
        document.getElementById('stat-missed').innerText = '0';
        document.getElementById('stat-dropped').innerText = '0';
        
        // Reset Telemetry Chart data
        chartLabels.length = 0;
        chartHighData.length = 0;
        chartMedData.length = 0;
        chartLowData.length = 0;
        chartLatencyData.length = 0;
        lastChartUpdateSimTime = 0;
        telemetryChart.update();
        
        systemLogsContainer.innerHTML = '';
        logSystem('Simulation Reset Successfully.', 'success');
    });
    
    speedSlider.addEventListener('input', (e) => {
        timeScale = parseFloat(e.target.value);
        speedVal.innerText = timeScale.toFixed(2) + 'x';
    });
    
    workerSlider.addEventListener('input', (e) => {
        numWorkers = parseInt(e.target.value);
        workerVal.innerText = numWorkers + ' Workers';
        rebuildWorkers();
        logSystem(`ThreadPool resized to ${numWorkers} worker threads.`, 'info');
    });
    
    rateInput.addEventListener('input', (e) => {
        rates.INPUT = parseFloat(e.target.value);
        rateInputVal.innerText = rates.INPUT.toFixed(1) + ' Hz';
    });
    
    ratePhysics.addEventListener('input', (e) => {
        rates.PHYSICS = parseFloat(e.target.value);
        ratePhysicsVal.innerText = rates.PHYSICS.toFixed(1) + ' Hz';
    });
    
    rateAi.addEventListener('input', (e) => {
        rates.AI = parseFloat(e.target.value);
        rateAiVal.innerText = rates.AI.toFixed(1) + ' Hz';
    });
    
    // Inject Buttons
    document.getElementById('inject-high').addEventListener('click', () => pushEvent('INPUT', 'HIGH'));
    document.getElementById('inject-med').addEventListener('click', () => pushEvent('PHYSICS', 'MEDIUM'));
    document.getElementById('inject-low').addEventListener('click', () => pushEvent('AI', 'LOW'));
    
    document.getElementById('btn-stress-backpressure').addEventListener('click', () => {
        logSystem('Flooding queues to visual capacity (Backpressure Stress)...', 'warn');
        for (let i = 0; i < 15; ++i) {
            pushEvent(
                Math.random() > 0.5 ? 'AI' : 'PHYSICS',
                Math.random() > 0.5 ? 'LOW' : 'MEDIUM'
            );
        }
    });
    
    btnClearLogs.addEventListener('click', () => {
        systemLogsContainer.innerHTML = '';
    });
    
    // Fault Toggles
    toggleException.addEventListener('change', (e) => {
        faultException = e.target.checked;
        logSystem(`Fault injection: Exception in callback is ${faultException ? 'ENABLED' : 'DISABLED'}`, faultException ? 'warn' : 'info');
    });
    
    toggleWriterLock.addEventListener('change', (e) => {
        faultWriterLock = e.target.checked;
        if (faultWriterLock) {
            lockIcon.setAttribute('data-lucide', 'lock');
            lockIcon.className = 'icon-red';
            lockText.innerText = 'Writer Exclusive Lock (Blocks Workers)';
            lockStatusIndicator.className = 'lock-status blocked';
            logSystem('Fault injection: Writer lock acquired in registry. Readers (workers) will block.', 'error');
        } else {
            lockIcon.setAttribute('data-lucide', 'unlock');
            lockIcon.className = 'icon-green';
            lockText.innerText = 'Shared Read Lock (Active)';
            lockStatusIndicator.className = 'lock-status';
            logSystem('Registry writer lock released. Workers unblocked.', 'success');
        }
        lucide.createImages();
    });
    
    // Tabs clicking
    tabButtons.forEach(btn => {
        btn.addEventListener('click', () => {
            switchTab(btn.getAttribute('data-tab'));
        });
    });
    
    // Clicking visual panels highlights corresponding code explanation
    activeElements.forEach(elem => {
        elem.addEventListener('click', () => {
            const target = elem.getAttribute('id-target');
            codeExplorerCard.classList.remove('highlight-flash');
            void codeExplorerCard.offsetWidth; // trigger reflow
            codeExplorerCard.classList.add('highlight-flash');
            
            if (target === 'bus-section') {
                switchTab('tab-aging');
            } else if (target === 'pool-section') {
                switchTab('tab-exception');
            } else if (target === 'registry-section') {
                switchTab('tab-locks');
            }
        });
    });
}

function switchTab(tabId) {
    tabButtons.forEach(b => b.classList.remove('active'));
    tabPanes.forEach(p => p.classList.remove('active'));
    
    document.querySelector(`.tab-btn[data-tab="${tabId}"]`).classList.add('active');
    document.getElementById(tabId).classList.add('active');
}

// Rebuild workers pool visually and internally
function rebuildWorkers() {
    workersContainer.innerHTML = '';
    workers = [];
    
    for (let i = 0; i < numWorkers; i++) {
        const worker = {
            id: i + 1,
            state: 'IDLE',
            event: null,
            progress: 0,
            duration: 0,
            elapsed: 0
        };
        workers.push(worker);
        
        const card = document.createElement('div');
        card.id = `worker-${worker.id}`;
        card.className = 'worker-card idle';
        
        card.innerHTML = `
            <div class="worker-meta">
                <span class="worker-name">Worker Thread #${worker.id}</span>
                <span class="worker-status-indicator">
                    <span class="status-dot"></span>
                    <span class="status-text" id="w-status-${worker.id}">IDLE</span>
                </span>
            </div>
            <div class="worker-event-box" id="w-box-${worker.id}" style="display: none;">
                <div class="worker-event-info">
                    <span id="w-ev-id-${worker.id}">Event #0</span>
                    <span id="w-ev-type-${worker.id}">INPUT</span>
                </div>
                <div class="worker-progress-container">
                    <div class="worker-progress" id="w-progress-${worker.id}"></div>
                </div>
            </div>
        `;
        workersContainer.appendChild(card);
    }
    poolActiveCount.innerText = `0 / ${numWorkers} active`;
}

// Get total queue size
function getQueueSize() {
    return queues.high.length + queues.med.length + queues.low.length;
}

// Tick loop (Runs every frame)
function animationTick(timestamp) {
    let delta = timestamp - lastTickTime;
    lastTickTime = timestamp;
    
    if (isRunning) {
        // Scale simulation step by timeScale
        let simDelta = delta * timeScale;
        simTime += simDelta;
        
        // 1. Run simulated producers
        runProducers(simDelta);
        
        // 2. Run event aging promotions
        applyAging(simDelta);
        
        // 3. Update active workers processing callbacks
        updateWorkers(simDelta);
        
        // 4. Update UI elements
        updateUI();
        
        // 5. Sample metrics for statsSampler
        updateTelemetry();
    } else {
        // Update just current timestamps to prevent jumps
        updateUIAgesOnly();
    }
    
    requestAnimationFrame(animationTick);
}

// Run scheduling producers (TickSource simulation)
function runProducers(simDelta) {
    const keys = Object.keys(rates);
    for (let type of keys) {
        const rate = rates[type];
        if (rate === 0) continue;
        
        // Event interval in ms
        const interval = 1000 / rate;
        
        if (simTime >= nextProducerRun[type]) {
            const priority = (type === 'INPUT') ? 'HIGH' : (type === 'PHYSICS' ? 'MEDIUM' : 'LOW');
            const success = pushEvent(type, priority);
            
            if (success) {
                // Drift-free: add fixed interval to target schedule instead of "now"
                nextProducerRun[type] = nextProducerRun[type] + interval;
                
                // If it fell way behind, snap it to current simTime to prevent queue flood loops
                if (nextProducerRun[type] < simTime - interval * 2) {
                    nextProducerRun[type] = simTime + interval;
                }
            } else {
                // If blocked by backpressure, delay it slightly until next frame tries again
                nextProducerRun[type] = simTime + 100;
            }
        }
    }
}

// Apply Aging (promotes stale events LOW -> MED -> HIGH)
function applyAging(simDelta) {
    // 1. Promote MED events to HIGH
    // Since MED is FIFO, check front elements (index 0)
    while (queues.med.length > 0) {
        const ev = queues.med[0];
        const age = simTime - ev.lastPromotedAt;
        if (age >= AGING_THRESHOLD) {
            queues.med.shift();
            ev.priority = 'HIGH';
            ev.lastPromotedAt = simTime;
            queues.high.push(ev);
            logSystem(`[EventBus Aging] Promoted event ID: ${ev.id} (PHYSICS) to HIGH priority!`, 'warn');
            
            // Visual trigger helper for animation
            ev.isPromoting = true;
        } else {
            break; // Rest are newer
        }
    }
    
    // 2. Promote LOW events to MED
    while (queues.low.length > 0) {
        const ev = queues.low[0];
        const age = simTime - ev.lastPromotedAt;
        if (age >= AGING_THRESHOLD) {
            queues.low.shift();
            ev.priority = 'MEDIUM';
            ev.lastPromotedAt = simTime;
            queues.med.push(ev);
            logSystem(`[EventBus Aging] Promoted event ID: ${ev.id} (AI) to MEDIUM priority!`, 'warn');
            
            ev.isPromoting = true;
        } else {
            break;
        }
    }
}

// Workers simulation (ThreadPool dispatch workers)
function updateWorkers(simDelta) {
    let activeCount = 0;
    
    for (let worker of workers) {
        if (worker.state === 'WORKING') {
            activeCount++;
            
            // If writer lock is active, worker blocks and stops progressing callback
            if (faultWriterLock) {
                continue; 
            }
            
            worker.elapsed += simDelta;
            worker.progress = Math.min((worker.elapsed / worker.duration) * 100, 100);
            
            if (worker.elapsed >= worker.duration) {
                // Completed processing event!
                const ev = worker.event;
                stats.processed++;
                const latency = simTime - ev.createdAt;
                stats.totalLatency += latency;
                
                // Update stats UI counters
                document.getElementById('stat-processed').innerText = stats.processed;
                document.getElementById('stat-latency').innerText = `${(stats.totalLatency / stats.processed / 1000).toFixed(2)}s`;
                
                const typeText = ev.type;
                const lightElement = document.getElementById(`light-${typeText.toLowerCase()}`);
                
                // Fault handling: simulated callback exception
                if (faultException && Math.random() < 0.25) {
                    stats.missed++; // counted as failed/missed
                    document.getElementById('stat-missed').innerText = stats.missed;
                    logSystem(`[ThreadPool Exception] Unhandled callback exception in Worker #${worker.id} for Event ID: ${ev.id}! Thread isolated and safe.`, 'error');
                } else {
                    // Check deadline miss
                    if (simTime > ev.deadline) {
                        stats.missed++;
                        document.getElementById('stat-missed').innerText = stats.missed;
                        const delay = ((simTime - ev.deadline) / 1000).toFixed(2);
                        logSystem(`[QoS Violation] Event ID: ${ev.id} (${ev.type}) processed in Worker #${worker.id} but MISSED deadline by ${delay}s!`, 'warn');
                    } else {
                        logSystem(`[ThreadPool Worker #${worker.id}] Dispatched event ID: ${ev.id} (${ev.type}) successfully in ${(worker.duration / 1000).toFixed(2)}s`, 'success');
                    }
                    
                    // Flash Registry Handler Callback indicator light
                    if (lightElement) {
                        lightElement.classList.add('flash');
                        setTimeout(() => lightElement.classList.remove('flash'), 200);
                    }
                }
                
                // Return worker to idle state
                worker.state = 'IDLE';
                worker.event = null;
            }
        }
        
        // If worker is IDLE, attempt to pop next event (blocks if all queues empty)
        if (worker.state === 'IDLE') {
            // Check if blocked by writer exclusive locks in Registry
            if (faultWriterLock) {
                continue; 
            }
            
            let ev = null;
            if (queues.high.length > 0) {
                ev = queues.high.shift();
            } else if (queues.med.length > 0) {
                ev = queues.med.shift();
            } else if (queues.low.length > 0) {
                ev = queues.low.shift();
            }
            
            if (ev) {
                worker.state = 'WORKING';
                worker.event = ev;
                worker.elapsed = 0;
                worker.duration = ev.duration; // callback exec duration
                worker.progress = 0;
                activeCount++;
                
                logSystem(`[ThreadPool Worker #${worker.id}] Popped event ID: ${ev.id} from queue. Dispatching handlers...`, 'info');
            }
        }
    }
    
    poolActiveCount.innerText = `${activeCount} / ${numWorkers} active`;
}

// Render the visual Event Card pills in tracks
function renderLane(laneId, queue) {
    const track = document.getElementById(`track-${laneId}`);
    
    // Clear and redraw cards
    track.innerHTML = '';
    
    for (let ev of queue) {
        const card = document.createElement('div');
        card.className = `event-card ${laneId}`;
        if (ev.isPromoting) {
            card.classList.add('promoting');
            // Remove promotion flag so it doesn't loop anim
            ev.isPromoting = false;
        }
        
        const ageSec = ((simTime - ev.createdAt) / 1000).toFixed(1);
        const deadlineSec = ((ev.deadline - simTime) / 1000);
        let deadlineHTML = '';
        if (deadlineSec < 0) {
            deadlineHTML = `<span class="event-deadline danger">Expired</span>`;
        } else {
            deadlineHTML = `<span class="event-deadline">DL: ${deadlineSec.toFixed(1)}s</span>`;
        }
        
        card.innerHTML = `
            <div class="event-card-header">
                <span class="event-id">#${ev.id}</span>
                <span class="event-type-label">${ev.type}</span>
            </div>
            <div class="event-card-body">
                <span class="event-age">${ageSec}s age</span>
                ${deadlineHTML}
            </div>
        `;
        track.appendChild(card);
    }
}

// Update the full simulation UI
function updateUI() {
    // Render Queue Tracks
    renderLane('high', queues.high);
    renderLane('med', queues.med);
    renderLane('low', queues.low);
    
    // Update Counts Labels
    countHigh.innerText = `${queues.high.length} events`;
    countMed.innerText = `${queues.med.length} events`;
    countLow.innerText = `${queues.low.length} events`;
    
    // Capacity Gauges
    const total = getQueueSize();
    busLoadVal.innerText = `${total} / ${MAX_CAPACITY}`;
    const pct = (total / MAX_CAPACITY) * 100;
    busCapacityBar.style.width = `${pct}%`;
    if (pct > 80) {
        busCapacityBar.style.backgroundColor = '#f43f5e'; // Red capacity bar
    } else if (pct > 50) {
        busCapacityBar.style.backgroundColor = '#f59e0b'; // Orange
    } else {
        busCapacityBar.style.backgroundColor = '#8b5cf6'; // Violet standard
    }
    
    // Render Worker Cards states
    for (let worker of workers) {
        const card = document.getElementById(`worker-${worker.id}`);
        const statusText = document.getElementById(`w-status-${worker.id}`);
        const box = document.getElementById(`w-box-${worker.id}`);
        
        if (worker.state === 'WORKING') {
            card.className = 'worker-card working';
            statusText.innerText = faultWriterLock ? 'BLOCKED ON LOCK' : 'WORKING';
            box.style.display = 'flex';
            
            document.getElementById(`w-ev-id-${worker.id}`).innerText = `Event #${worker.event.id}`;
            document.getElementById(`w-ev-type-${worker.id}`).innerText = worker.event.type;
            
            const badgeClass = worker.event.priority === 'HIGH' ? 'high' : (worker.event.priority === 'MEDIUM' ? 'med' : 'low');
            box.className = `worker-event-box ${badgeClass}`;
            
            document.getElementById(`w-progress-${worker.id}`).style.width = `${worker.progress}%`;
        } else {
            card.className = 'worker-card idle';
            statusText.innerText = 'IDLE';
            box.style.display = 'none';
        }
    }
}

// Fallback update to handle UI ages when simulation is paused
function updateUIAgesOnly() {
    // This allows age values to not flash or jump strangely when paused
    updateUI();
}

// Telemetry collection loop (simulating StatsSampler and updating Chart.js)
function updateTelemetry() {
    if (simTime - lastChartUpdateSimTime >= CHART_SAMPLE_INTERVAL) {
        lastChartUpdateSimTime = simTime;
        
        const label = `${(simTime / 1000).toFixed(0)}s`;
        chartLabels.push(label);
        chartHighData.push(queues.high.length);
        chartMedData.push(queues.med.length);
        chartLowData.push(queues.low.length);
        
        // Calculate average latency in seconds (and scale for visual comparison on secondary Y axis)
        let avgLatSec = 0;
        if (stats.processed > 0) {
            avgLatSec = stats.totalLatency / stats.processed / 1000;
        }
        chartLatencyData.push(avgLatSec);
        
        // Truncate historical labels to keep graph clean
        if (chartLabels.length > 20) {
            chartLabels.shift();
            chartHighData.shift();
            chartMedData.shift();
            chartLowData.shift();
            chartLatencyData.shift();
        }
        
        telemetryChart.update('none'); // Update without full animation redraws to save CPU
    }
}

// Start visualizer on document load
window.onload = init;

import logging
import os
import glob
import subprocess
import time
from flask import Flask, render_template_string, jsonify, request, Response

log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

app = Flask(__name__)

SERVO_PATH = "/sys/class/pico-servo/"
HWMON_PATH = "/sys/class/hwmon/"
PICOLINK_DEV = "/dev/picolink"

servo_cache = {} 

HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>Pico Control Panel PRO</title>
    <style>
        body { font-family: 'Segoe UI', sans-serif; background: #121212; color: #e0e0e0; display: flex; gap: 20px; padding: 20px; margin: 0; height: 100vh; box-sizing: border-box; }
        .column { flex: 1; background: #1e1e1e; padding: 15px; border-radius: 12px; border: 1px solid #333; display: flex; flex-direction: column; overflow: hidden; }
        h2 { border-bottom: 2px solid #007bff; padding-bottom: 10px; margin: 0 0 15px 0; font-size: 1.2em; }
        .scroll-area { flex-grow: 1; overflow-y: auto; padding-right: 5px; }
        .item { background: #252525; margin-bottom: 10px; padding: 12px; border-radius: 8px; border-left: 4px solid #007bff; }
        .servo-grid { display: grid; grid-template-columns: 50px 1fr 60px 45px; gap: 8px; align-items: center; }
        .group-control { background: #1a3a5a; padding: 15px; border-radius: 8px; margin-top: 10px; }
        .hw-item { background: #2a2a2a; padding: 10px; margin-bottom: 8px; border-radius: 6px; }
        .hw-row { display: flex; justify-content: space-between; margin-bottom: 5px; }
        .formula-input { width: 100%; background: #121212; color: #00ff00; border: 1px solid #444; font-family: monospace; padding: 3px; font-size: 0.8em; }
        .raw-val { color: #666; font-size: 0.7em; }
        input[type="range"] { width: 100%; cursor: pointer; }
        input[type="number"], input[type="text"] { background: #333; color: #fff; border: 1px solid #444; border-radius: 4px; padding: 4px; width: 100%; box-sizing: border-box; }
        button { cursor: pointer; background: #007bff; color: white; border: none; padding: 6px 12px; border-radius: 4px; }
        button.set-btn { padding: 4px 2px; font-size: 0.8em; background: #28a745; }
        .value { font-family: monospace; color: #00ff00; font-weight: bold; }
        
        #dmesg-log { 
            flex-grow: 1; 
            background: #000; 
            color: #ccc; 
            font-family: 'Consolas', 'Monaco', monospace; 
            font-size: 11px; 
            padding: 10px; 
            border-radius: 8px; 
            overflow-y: auto; 
            white-space: pre-wrap;
            border: 1px solid #444;
            margin-top: 10px;
            scroll-behavior: smooth;
        }
        .log-line { display: block; line-height: 1.4; border-bottom: 1px solid #111; }
    </style>
</head>
<body>

<div class="column">
    <h2>Servos</h2>
    <div class="scroll-area" id="servos-list"></div>
    <div class="group-control">
        <div style="display: flex; justify-content: space-between;">
            <span class="label">GROUP SYNC</span>
            <span class="value" id="group-val">90°</span>
        </div>
        <input type="range" min="0" max="180" value="90" oninput="applyGroup(this.value)">
    </div>
</div>

<div class="column">
    <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #007bff; margin-bottom: 15px;">
        <h2 style="border:none; margin:0;">Health</h2>
        <div style="font-size: 0.8em;">
            Update: <input type="number" id="update-rate" value="2" style="width: 40px;">s
        </div>
    </div>
    <div class="scroll-area" id="hwmon-list"></div>
</div>

<div class="column" style="max-width: 400px;">
    <h2>PicoLink</h2>
    <div id="picolink-status" style="margin-bottom: 10px;"></div>
    <input type="text" id="cmd-input" placeholder="Command..." style="width: 100%; box-sizing: border-box;">
    <button onclick="sendCmd()" style="width: 100%; margin-top: 10px;">Send</button>
    
    <h2 style="margin-top: 20px;">System Logs</h2>
    <div id="dmesg-log">Waiting for logs...</div>
</div>

<script>
let lastRequestTime = 0;
const THROTTLE_MS = 30; 
const hwConfigs = {};

// --- ЛОГИ ---
const logContainer = document.getElementById('dmesg-log');
let logBuffer = [];

const evtSource = new EventSource("/api/logs");
evtSource.onmessage = function(e) {
    if (logContainer.innerText === "Waiting for logs...") logContainer.innerText = "";
    logBuffer.push(e.data);
};

// Отрисовка буфера логов (раз в 100мс)
setInterval(() => {
    if (logBuffer.length === 0) return;

    // Определяем, был ли пользователь внизу (с допуском 50px)
    const isAtBottom = (logContainer.scrollHeight - logContainer.scrollTop - logContainer.clientHeight) < 50;

    const chunk = logBuffer.splice(0, 500); // Берем пачку
    const fragment = document.createDocumentFragment();
    
    chunk.forEach(msg => {
        const span = document.createElement('span');
        span.className = 'log-line';
        span.innerHTML = msg;
        fragment.appendChild(span);
    });
    
    logContainer.appendChild(fragment);
    
    // Чистим старое
    if (logContainer.childNodes.length > 2000) {
        // Удаляем первые 500 элементов за раз для скорости
        for(let i=0; i<500; i++) {
            if(logContainer.firstChild) logContainer.removeChild(logContainer.firstChild);
        }
    }
    
    // Автопрокрутка: если были внизу, остаемся внизу
    if (isAtBottom) {
        logContainer.scrollTop = logContainer.scrollHeight;
    }
}, 100);

// --- ДАННЫЕ ---
function calculate(formula, x) {
    try { return eval(formula.replace(/x/g, x)).toFixed(2); } catch (e) { return "Err"; }
}

async function updateData() {
    const rate = document.getElementById('update-rate').value || 2;
    try {
        const res = await fetch('/api/data');
        const data = await res.json();
        
        // --- SERVOS ---
        const servoList = document.getElementById('servos-list');
        data.servos.forEach(s => {
            let item = document.getElementById(`servo-container-${s.id}`);
            if (!item) {
                const div = document.createElement('div');
                div.id = `servo-container-${s.id}`;
                div.className = 'item';
                div.innerHTML = `
                    <div style="display:flex; justify-content:space-between; margin-bottom:5px;">
                        <b style="color:#007bff">${s.id.toUpperCase()}</b>
                        <label style="font-size:0.7em"><input type="checkbox" class="servo-sync" data-id="${s.id}"> Sync</label>
                    </div>
                    <div class="servo-grid">
                        <span class="value" id="val-${s.id}">${s.target}°</span>
                        <input type="range" id="range-${s.id}" min="0" max="180" value="${s.target}" oninput="setAngle('${s.id}', this.value)">
                        <input type="number" id="inp-${s.id}" value="${s.target}" min="0" max="180">
                        <button class="set-btn" onclick="setAngle('${s.id}', document.getElementById('inp-${s.id}').value)">SET</button>
                    </div>`;
                servoList.appendChild(div);
            } else {
                const r = document.getElementById(`range-${s.id}`);
                const n = document.getElementById(`inp-${s.id}`);
                if (document.activeElement !== r && document.activeElement !== n) {
                     document.getElementById(`val-${s.id}`).innerText = s.target + "°";
                     r.value = s.target;
                     n.value = s.target;
                }
            }
        });

        // --- HWMON ---
        const hwList = document.getElementById('hwmon-list');
        data.hwmon.forEach(h => {
            // h.id приходит с сервера
            const safeId = h.id;
            
            if (!hwConfigs[safeId]) {
                let def = "x";
                if (h.name.toLowerCase().includes("thermal")) def = "x / 1000";
                else if (parseFloat(h.value) > 1000) def = "x / 1000";
                hwConfigs[safeId] = def;
            }
            
            let item = document.getElementById(`hw-container-${safeId}`);
            const calculatedValue = calculate(hwConfigs[safeId], h.value);
            
            if (!item) {
                const div = document.createElement('div');
                div.id = `hw-container-${safeId}`;
                div.className = 'hw-item';
                div.innerHTML = `
                    <div class="hw-row">
                        <small>${h.name}</small>
                        <span class="value" id="calc-${safeId}">${calculatedValue}</span>
                    </div>
                    <div class="hw-row">
                        <span class="raw-val">Raw: ${h.value}</span>
                        <input type="text" class="formula-input" value="${hwConfigs[safeId]}" oninput="hwConfigs['${safeId}']=this.value" placeholder="Formula">
                    </div>`;
                hwList.appendChild(div);
            } else {
                document.getElementById(`calc-${safeId}`).innerText = calculatedValue;
                item.querySelector('.raw-val').innerText = "Raw: " + h.value;
            }
        });

        document.getElementById('picolink-status').innerHTML = data.picolink ? '<span style="color:green">● Picolink Online</span>' : '<span style="color:red">○ Picolink Offline</span>';
    } catch (e) { console.log(e); }
    
    setTimeout(updateData, rate * 1000);
}

async function setAngle(id, angle) {
    const now = Date.now();
    document.getElementById(`val-${id}`).innerText = angle + "°";
    if (now - lastRequestTime < THROTTLE_MS) return;
    lastRequestTime = now;
    fetch('/api/set_servo', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({id, angle})
    }).catch(e => {});
}

function applyGroup(val) {
    document.getElementById('group-val').innerText = val + "°";
    document.querySelectorAll('.servo-sync:checked').forEach(cb => { setAngle(cb.dataset.id, val); });
}

async function sendCmd() {
    const cmd = document.getElementById('cmd-input').value;
    if(!cmd) return;
    await fetch('/api/picolink', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({cmd})
    });
    document.getElementById('cmd-input').value = '';
}

// Запуск
updateData();
</script>
</body>
</html>
"""

def get_servos():
    global servo_cache
    current_servos = []
    # Вернулись к простому listdir, он надежнее для симлинков в sysfs
    if not os.path.exists(SERVO_PATH): return []
    try:
        found_dirs = sorted([d for d in os.listdir(SERVO_PATH) if d.startswith("servo")])
        
        for name in found_dirs:
            if name not in servo_cache: servo_cache[name] = "N/A" 
            current_servos.append({"id": name, "target": servo_cache[name]})
        
        # Чистим кэш
        current_set = set(found_dirs)
        servo_cache = {k: v for k, v in servo_cache.items() if k in current_set}
    except:
        pass
    return current_servos

def get_hwmon():
    sensors = []
    if not os.path.exists(HWMON_PATH): return []
    
    # Вернулись к glob, убрав сложное кэширование путей
    hwmon_dirs = sorted(glob.glob(os.path.join(HWMON_PATH, "hwmon*")))
    
    for d in hwmon_dirs:
        try:
            name_file = os.path.join(d, "name")
            hw_name = "unknown"
            if os.path.exists(name_file):
                with open(name_file, 'r') as f: hw_name = f.read().strip()
            
            v_files = glob.glob(os.path.join(d, "*_input")) + glob.glob(os.path.join(d, "value"))
            for vf in v_files:
                try:
                    with open(vf, 'r') as f: val = f.read().strip()
                    
                    base_name = os.path.basename(vf)
                    # Генерируем ID здесь, в Python, чтобы он был стабильным
                    # Заменяем пробелы и точки на подчеркивания для HTML ID
                    uid = f"{hw_name}_{base_name}".replace(' ', '_').replace('.', '_').replace(':', '')
                    
                    sensors.append({
                        "name": f"{hw_name} ({base_name})", 
                        "value": val,
                        "id": uid  # Важно: передаем ID фронтенду
                    })
                except: continue
        except: continue
    return sensors

@app.route('/')
def index():
    return render_template_string(HTML_TEMPLATE)

@app.route('/api/data')
def api_data():
    return jsonify({
        "servos": get_servos(),
        "hwmon": get_hwmon(),
        "picolink": os.path.exists(PICOLINK_DEV)
    })

@app.route('/api/logs')
def stream_logs():
    def generate():
        cmd = ["dmesg", "-w", "--color=always"]
        # bufsize=1 (строковый буфер) помогает с мгновенной отдачей
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        
        try:
            while True:
                line = process.stdout.readline()
                if not line: break
                
                line = line.strip()
                if not line: continue
                
                # Парсинг цветов
                safe_line = line.replace('\x1b[31m', '<span style="color:#ff5555">') \
                                .replace('\x1b[32m', '<span style="color:#55ff55">') \
                                .replace('\x1b[33m', '<span style="color:#ffff55">') \
                                .replace('\x1b[34m', '<span style="color:#5555ff">') \
                                .replace('\x1b[0m', '</span>') \
                                .replace('<', '&lt;').replace('>', '&gt;') # Экранирование HTML тегов в самом логе
                
                # Возвращаем экранированные теги цвета обратно (хак, но работает для dmesg)
                safe_line = safe_line.replace('&lt;span style="color:', '<span style="color:') \
                                     .replace('&lt;/span&gt;', '</span>') \
                                     .replace('"&gt;', '">')

                yield f"data: {safe_line}\n\n"
        except GeneratorExit:
            process.terminate()
        except Exception:
            process.terminate()

    return Response(generate(), mimetype='text/event-stream')

@app.route('/api/set_servo', methods=['POST'])
def set_servo():
    data = request.json
    path = os.path.join(SERVO_PATH, data['id'], "angle")
    try:
        with open(path, 'w') as f: f.write(str(data['angle']))
        servo_cache[data['id']] = data['angle']
        return jsonify({"status": "ok"})
    except Exception as e: return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/picolink', methods=['POST'])
def send_picolink():
    cmd = request.json.get('cmd', '')
    if not os.path.exists(PICOLINK_DEV): return jsonify({"status": "error", "message": "Device not found"}), 404
    try:
        with open(PICOLINK_DEV, 'w') as f: f.write(cmd + "\n")
        return jsonify({"status": "ok"})
    except Exception as e: return jsonify({"status": "error", "message": str(e)}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
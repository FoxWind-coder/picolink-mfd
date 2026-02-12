HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>Pico Control Panel</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #121212; color: #e0e0e0; display: flex; gap: 20px; padding: 20px; margin: 0; }
        .column { flex: 1; background: #1e1e1e; padding: 20px; border-radius: 12px; border: 1px solid #333; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
        h2 { border-bottom: 2px solid #007bff; padding-bottom: 10px; margin-top: 0; font-weight: 300; }
        .item { background: #252525; margin: 12px 0; padding: 15px; border-radius: 8px; border-left: 4px solid #007bff; }
        .servo-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; align-items: center; }
        .label { font-weight: bold; color: #aaa; font-size: 0.9em; }
        .value { font-family: monospace; color: #00ff00; font-size: 1.1em; }
        
        input[type="number"], input[type="text"] { background: #333; color: #fff; border: 1px solid #444; padding: 8px; border-radius: 4px; width: 60px; }
        input[type="text"] { width: 100%; box-sizing: border-box; }
        
        button { cursor: pointer; background: #007bff; color: white; border: none; padding: 8px 15px; border-radius: 4px; transition: 0.2s; }
        button:hover { background: #0056b3; }
        
        .hw-item { display: flex; justify-content: space-between; border-bottom: 1px solid #333; padding: 8px 0; }
        .terminal-box { margin-top: 20px; }
        .status-dot { height: 10px; width: 10px; border-radius: 50%; display: inline-block; margin-right: 5px; }
    </style>
</head>
<body>

<div class="column">
    <h2>Servos Control</h2>
    <div id="servos-list"></div>
</div>

<div class="column">
    <h2>System Health (hwmon)</h2>
    <div id="hwmon-list"></div>
</div>

<div class="column">
    <h2>PicoLink Terminal</h2>
    <div id="picolink-status" style="margin-bottom: 15px;"></div>
    <div class="terminal-box">
        <input type="text" id="cmd-input" placeholder="Type command here...">
        <button onclick="sendCmd()" style="width: 100%; margin-top: 10px;">Send to /dev/picolink</button>
    </div>
</div>

<script>
async function updateData() {
    try {
        const res = await fetch('/api/data');
        const data = await res.json();

        const servoList = document.getElementById('servos-list');

        data.servos.forEach(s => {
            let item = document.getElementById(`servo-container-${s.id}`);
            
            // Если такого серво еще нет на странице — создаем структуру целиком
            if (!item) {
                const newElem = document.createElement('div');
                newElem.id = `servo-container-${s.id}`;
                newElem.className = 'item';
                newElem.innerHTML = `
                    <div style="margin-bottom: 10px; font-weight: bold; color: #007bff;">${s.id.toUpperCase()}</div>
                    <div class="servo-grid">
                        <div>
                            <span class="label">LAST SENT:</span><br>
                            <span class="value" id="val-${s.id}">${s.target}°</span>
                        </div>
                        <div>
                            <span class="label">SET NEW:</span><br>
                            <input type="number" id="inp-${s.id}" placeholder="0-180">
                            <button onclick="setAngle('${s.id}')">GO</button>
                        </div>
                    </div>
                `;
                servoList.appendChild(newElem);
            } else {
                // Если серво уже есть — обновляем ТОЛЬКО значение угла
                const valSpan = document.getElementById(`val-${s.id}`);
                if (valSpan && valSpan.innerText !== s.target + "°") {
                    valSpan.innerText = s.target + "°";
                }
            }
        });

        // Удаление пропавших сервоприводов
        const activeIds = data.servos.map(s => `servo-container-${s.id}`);
        Array.from(servoList.children).forEach(child => {
            if (!activeIds.includes(child.id)) {
                child.remove();
            }
        });

        // Обновление Датчиков (их можно обновлять целиком, там нет инпутов)
        const hwDiv = document.getElementById('hwmon-list');
        hwDiv.innerHTML = data.hwmon.map(h => `
            <div class="hw-item">
                <span>${h.name}</span>
                <span class="value">${h.value}</span>
            </div>
        `).join('');

        // Статус Picolink
        const pStatus = document.getElementById('picolink-status');
        pStatus.innerHTML = data.picolink 
            ? '<span class="status-dot" style="background: #28a745;"></span> <span style="color: #28a745;">/dev/picolink Online</span>'
            : '<span class="status-dot" style="background: #dc3545;"></span> <span style="color: #dc3545;">/dev/picolink Offline</span>';

    } catch (e) { console.error("Update error:", e); }
}

async function setAngle(id) {
    const input = document.getElementById('inp-' + id);
    const angle = input.value;
    if (angle === "") return;

    const res = await fetch('/api/set_servo', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({id, angle})
    });
    
    if (res.ok) {
        input.value = ""; // Очищаем поле ввода после успешной отправки
        updateData();
    } else {
        alert("Error setting angle!");
    }
}

async function sendCmd() {
    const cmdInput = document.getElementById('cmd-input');
    const cmd = cmdInput.value;
    if (!cmd) return;

    const res = await fetch('/api/picolink', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({cmd})
    });

    if (res.ok) {
        cmdInput.value = '';
    } else {
        alert("Error sending to picolink");
    }
}

setInterval(updateData, 2000);
updateData();
</script>
</body>
</html>
"""
import os
import glob
from flask import Flask, render_template_string, jsonify, request

app = Flask(__name__)

SERVO_PATH = "/sys/class/pico-servo/"
HWMON_PATH = "/sys/class/hwmon/"
PICOLINK_DEV = "/dev/picolink"

# Глобальный словарь для хранения последних заданных углов
servo_cache = {} 

def get_servos():
    global servo_cache
    current_servos = []
    
    if not os.path.exists(SERVO_PATH):
        return []

    found_dirs = [d for d in os.listdir(SERVO_PATH) if d.startswith("servo")]
    
    for name in sorted(found_dirs):
        # Если мотор новый — пусть будет "N/A" до первой команды
        if name not in servo_cache:
            servo_cache[name] = "N/A" 
        
        current_servos.append({
            "id": name, 
            "target": servo_cache[name] # Это наше состояние в памяти
        })
        
    # Чистка кеша
    active_names = set(found_dirs)
    servo_cache = {k: v for k, v in servo_cache.items() if k in active_names}
    
    return current_servos

def get_hwmon():
    sensors = []
    base_path = "/sys/class/hwmon/"
    
    if not os.path.exists(base_path):
        print("DEBUG HWMON: /sys/class/hwmon/ not found!")
        return []

    # Перебираем все папки hwmon*
    hwmon_dirs = sorted(glob.glob(os.path.join(base_path, "hwmon*")))
    
    for d in hwmon_dirs:
        try:
            # 1. Пытаемся прочитать имя устройства
            name_file = os.path.join(d, "name")
            hw_name = "unknown"
            if os.path.exists(name_file):
                with open(name_file, 'r') as f:
                    hw_name = f.read().strip()
            
            # 2. Ищем файлы значений (как в вашем shell скрипте)
            # Приоритет: in0_input -> value -> любые другие *_input
            value_files = []
            
            # Проверяем конкретные файлы из вашего скрипта
            for candidate in ["in0_input", "value"]:
                path = os.path.join(d, candidate)
                if os.path.exists(path):
                    value_files.append(path)
            
            # Добавляем все остальные файлы, заканчивающиеся на _input (если они еще не добавлены)
            all_inputs = glob.glob(os.path.join(d, "*_input"))
            for inp in all_inputs:
                if inp not in value_files:
                    value_files.append(inp)

            if not value_files:
                print(f"DEBUG HWMON: Found dir {d} ({hw_name}), but NO value files found.")
                continue

            for vf in value_files:
                label = os.path.basename(vf)
                try:
                    with open(vf, 'r') as f:
                        val = f.read().strip()
                    
                    sensors.append({
                        "name": f"{hw_name} ({label})",
                        "value": val
                    })
                    # print(f"DEBUG HWMON: Read {hw_name}/{label} = {val}") # Раскомментируйте для полного спама
                except Exception as e:
                    print(f"DEBUG HWMON: Could not read file {vf}: {e}")

        except Exception as e:
            print(f"DEBUG HWMON: Error processing directory {d}: {e}")
            
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

@app.route('/api/set_servo', methods=['POST'])
def set_servo():
    data = request.json
    servo_id = data.get('id')
    angle = str(data.get('angle'))
    path = os.path.join(SERVO_PATH, servo_id, "angle")
    
    try:
        # Пишем в драйвер
        with open(path, 'w') as f:
            f.write(angle)
        # Сохраняем в кеш, так как прочитать из драйвера нельзя
        servo_cache[servo_id] = angle
        return jsonify({"status": "ok"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/picolink', methods=['POST'])
def send_picolink():
    cmd = request.json.get('cmd', '')
    if not os.path.exists(PICOLINK_DEV):
        return jsonify({"status": "error", "message": "Device not found"}), 404
    try:
        with open(PICOLINK_DEV, 'w') as f:
            f.write(cmd + "\n")
        return jsonify({"status": "ok"})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
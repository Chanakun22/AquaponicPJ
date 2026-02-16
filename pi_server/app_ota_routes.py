# =============================================================================
# OTA Upload Routes — เพิ่มใน app.py ก่อน if __name__ == '__main__':
# =============================================================================
# วิธีใช้: Copy code นี้ไปวางใน app.py (ก่อน main block)
#         แล้ว restart Flask server
# =============================================================================

import subprocess
import uuid
import tempfile

# Store OTA task status
ota_tasks = {}

@app.route('/ota')
def ota_page():
    return send_file('ota.html')

@app.route('/api/ota/upload', methods=['POST'])
def ota_upload():
    """Receive firmware.bin from browser, then flash to ESP32 via espota.py"""
    try:
        if 'firmware' not in request.files:
            return jsonify({"status": "error", "message": "ไม่มีไฟล์ firmware"}), 400

        firmware = request.files['firmware']
        if not firmware.filename.endswith('.bin'):
            return jsonify({"status": "error", "message": "กรุณาใช้ไฟล์ .bin เท่านั้น"}), 400

        # Save firmware to temp file
        firmware_path = os.path.join(tempfile.gettempdir(), 'firmware_ota.bin')
        firmware.save(firmware_path)
        file_size = os.path.getsize(firmware_path)

        print(f"📦 OTA: Received firmware {firmware.filename} ({file_size} bytes)")
        save_log(f"📦 OTA Upload: {firmware.filename} ({file_size} bytes)")

        # Generate task ID
        task_id = str(uuid.uuid4())[:8]
        ota_tasks[task_id] = {
            "status": "flashing",
            "message": "",
            "logs": [f"📦 Firmware received: {file_size} bytes",
                     "🔄 Starting espota flash to ESP32..."]
        }

        # Start OTA flash in background thread
        esp_ip = "192.168.10.100"  # ESP32 IP on AP network
        ota_password = "admin123"
        espota_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'espota.py')

        def run_ota():
            try:
                cmd = [
                    'python3', espota_script,
                    '-i', esp_ip,
                    '-p', '3232',
                    '--auth=' + ota_password,
                    '-f', firmware_path,
                    '-d', '-r'
                ]

                ota_tasks[task_id]["logs"].append(f"🚀 Flashing to {esp_ip}:3232...")
                print(f"🚀 OTA Command: {' '.join(cmd)}")

                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=120  # 2 minute timeout
                )

                if result.returncode == 0:
                    ota_tasks[task_id]["status"] = "success"
                    ota_tasks[task_id]["message"] = "Flash สำเร็จ!"
                    ota_tasks[task_id]["logs"].append("✅ Flash สำเร็จ! ESP32 กำลัง reboot...")
                    print("✅ OTA Flash successful!")
                    save_log("✅ OTA Flash successful!")
                else:
                    error_msg = result.stderr.strip() or result.stdout.strip() or "Unknown error"
                    ota_tasks[task_id]["status"] = "error"
                    ota_tasks[task_id]["message"] = error_msg
                    ota_tasks[task_id]["logs"].append(f"❌ Flash failed: {error_msg}")
                    print(f"❌ OTA Flash failed: {error_msg}")
                    save_log(f"❌ OTA Flash failed: {error_msg}")

            except subprocess.TimeoutExpired:
                ota_tasks[task_id]["status"] = "error"
                ota_tasks[task_id]["message"] = "Timeout: Flash ใช้เวลานานเกินไป"
                ota_tasks[task_id]["logs"].append("❌ Timeout: Flash ใช้เวลานานเกินไป")
                print("❌ OTA Timeout")
            except Exception as e:
                ota_tasks[task_id]["status"] = "error"
                ota_tasks[task_id]["message"] = str(e)
                ota_tasks[task_id]["logs"].append(f"❌ Error: {e}")
                print(f"❌ OTA Error: {e}")

            # Cleanup
            try:
                os.remove(firmware_path)
            except:
                pass

        thread = threading.Thread(target=run_ota)
        thread.daemon = True
        thread.start()

        return jsonify({"status": "uploading", "task_id": task_id})

    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/ota/status/<task_id>')
def ota_status(task_id):
    """Check OTA flash status"""
    task = ota_tasks.get(task_id)
    if not task:
        return jsonify({"status": "error", "message": "Task not found"}), 404
    return jsonify(task)

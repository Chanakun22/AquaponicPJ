#!/bin/bash

echo "🌿 Setting up Aquaponics System..."

# 1. Update & Install Dependencies
echo "📦 Installing system dependencies..."
sudo apt-get update
sudo apt-get install -y python3-pip libcamera-apps v4l-utils

# 2. Install Python Requirements
echo "🐍 Installing Python libraries..."
pip3 install flask paho-mqtt psutil requests --break-system-packages

# 3. Set Permissions
echo "🔑 Setting permissions..."
chmod +x start_cam.sh
chmod +x app.py

# 4. Setup Service (SKIPPED - Using existing service)
# echo "⚙️ Configuring Systemd Service..."
# sudo cp aquaponics.service /etc/systemd/system/
# sudo systemctl daemon-reload
# sudo systemctl enable aquaponics.service
# sudo systemctl restart aquaponics.service

echo "✅ Setup Complete!"
echo "--------------------------------"
echo "🌐 Web Dashboard: http://$(hostname -I | awk '{print $1}'):5000"
echo "🎥 To start Camera: cd ~/myserver && ./start_cam.sh"
echo "--------------------------------"

@echo off
"C:\Program Files\Git\cmd\git.exe" init
"C:\Program Files\Git\cmd\git.exe" config user.name "Aquaponics User"
"C:\Program Files\Git\cmd\git.exe" config user.email "user@local"
"C:\Program Files\Git\cmd\git.exe" add .
"C:\Program Files\Git\cmd\git.exe" commit -m "Initial commit with secured credentials"
"C:\Program Files\Git\cmd\git.exe" branch -M main
echo GIT SETUP COMPLETE

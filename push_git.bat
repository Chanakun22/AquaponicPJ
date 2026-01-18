@echo off
"C:\Program Files\Git\cmd\git.exe" remote add origin https://github.com/Chanakun22/AquaponicPJ.git
IF %ERRORLEVEL% NEQ 0 (
  "C:\Program Files\Git\cmd\git.exe" remote set-url origin https://github.com/Chanakun22/AquaponicPJ.git
)
"C:\Program Files\Git\cmd\git.exe" branch -M main
"C:\Program Files\Git\cmd\git.exe" push -u origin main

@echo off
REM Build + flash ACT1-ROBIN on COM17 (edit port in .vscode\settings.json if needed).
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$py='C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe';" ^
  "$idf='C:\esp\v5.5.3\esp-idf';" ^
  "$act=(& $py \"$idf\tools\activate.py\" --export | Select-Object -Last 1).Trim();" ^
  ". $act;" ^
  "Set-Location 'X:\Projects\ACT1-ROBIN';" ^
  "idf.py -p COM17 build flash"
pause

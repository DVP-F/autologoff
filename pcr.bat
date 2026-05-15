@echo off

python -m nuitka --mode=onefile --remove-output --include-windows-runtime-dlls=yes --output-dir="." --output-filename="app.exe" --deployment alo.py



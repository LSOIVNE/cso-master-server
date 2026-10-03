@echo off
chcp 65001 >nul
title CSO 個人倉庫一鍵重置工具

echo 正在執行倉庫重置...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0reset_inventory.ps1" %*

echo.
pause

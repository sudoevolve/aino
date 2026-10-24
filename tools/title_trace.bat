@echo off
rem  Run the gallery with window-title tracing enabled.
rem  After the quickstart popup appears, close the gallery and send title_trace.log.
setlocal
cd /d "%~dp0.."
if exist title_trace.log del title_trace.log
set AINO_TITLE_TRACE=%CD%\title_trace.log
echo tracing to: %AINO_TITLE_TRACE%
start "" "%CD%\build\aino_gallery.exe"
echo.
echo 1) In the gallery, click the button near the bottom of the Overview page.
echo 2) Close the small window and the gallery window.
echo 3) Send title_trace.log back.
pause

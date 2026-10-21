@echo off
rem Sends this folder to GitHub (origin). The "macOS build" workflow runs from the Actions tab or when a v* tag / the mac-build branch is pushed.
cd /d "%~dp0"
git add -A
git diff --cached --quiet && (echo Nothing new to commit.) || git commit -m "update"
git push origin HEAD
pause

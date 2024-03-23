cd /d "%~dp0\.."
set "DPG_VS_VERSION=Visual Studio 17 2022"
rmdir /S /Q cmake-build-debug 2>nul
mkdir cmake-build-debug
cd cmake-build-debug
cmake ..
cd ..
cmake --build cmake-build-debug --config Debug
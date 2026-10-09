@echo off
rem Fetches the AsaApi source (headers) that CreatureCombatRewards builds against.
setlocal
cd /d "%~dp0"

if exist "extern\AsaApi\AsaApi\Core\Public\API\ARK\Ark.h" goto have_source

where git >nul 2>nul
if errorlevel 1 goto no_git

git clone --depth 1 https://github.com/ArkServerApi/AsaApi.git "extern\AsaApi"
if errorlevel 1 goto clone_failed

:have_source
echo AsaApi source is in extern\AsaApi
if not exist "extern\AsaApi\out_lib" mkdir "extern\AsaApi\out_lib"
if exist "extern\AsaApi\out_lib\AsaApi.lib" goto have_lib

echo.
echo One step left: copy AsaApi.lib into extern\AsaApi\out_lib\
echo It is in the Lib folder of the AsaApi release zip you installed on your servers.
goto end

:have_lib
echo AsaApi.lib found. Open CreatureCombatRewards.sln and build Release / x64.
goto end

:no_git
echo git was not found on PATH.
echo Install Git for Windows, or download the AsaApi source from
echo https://github.com/ArkServerApi/AsaApi and extract it to extern\AsaApi
exit /b 1

:clone_failed
echo Could not clone AsaApi. Check your connection and try again.
exit /b 1

:end
endlocal

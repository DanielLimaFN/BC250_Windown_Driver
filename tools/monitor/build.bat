@echo off
setlocal enabledelayedexpansion

where cl.exe >nul 2>&1
if errorlevel 1 (
    for %%D in (C D E F G) do (
        for %%E in (Enterprise Professional Community BuildTools) do (
            if exist "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
                call "%%D:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
                goto :found_vs
            )
        )
    )
    echo ERRO: Visual Studio 2022 nao encontrado.
    exit /b 1
)

:found_vs
set CFLAGS=/nologo /W3 /WX- /Od /D "WIN32" /D "_CONSOLE" /Gm- /EHsc /RTC1 /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /std:c11

cl %CFLAGS% /Fe"bc250_monitor.exe" bc250_monitor.c 2>nul
if %ERRORLEVEL% equ 0 (
    echo [build] Sucesso! Executavel gerado em tools\monitor\bc250_monitor.exe
) else (
    cl %CFLAGS% /Fe"bc250_monitor_v2.exe" bc250_monitor.c
    if %ERRORLEVEL% equ 0 echo [build] Sucesso! Executavel gerado em tools\monitor\bc250_monitor_v2.exe
)

cl %CFLAGS% /Fe"bc250_silicon_runner.exe" bc250_silicon_runner.c
if %ERRORLEVEL% equ 0 (
    echo [build] Sucesso! Executavel gerado em tools\monitor\bc250_silicon_runner.exe
) else (
    echo [build] Falha na compilacao de bc250_silicon_runner.exe
)

:done
endlocal

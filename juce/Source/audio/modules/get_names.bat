@echo off
setlocal enabledelayedexpansion

:: --- USER INPUT ---
echo.
echo Please enter the exact path prefix you want to add.
echo Example: juce/Source/audio/modules/
echo.
set /p "pathPrefix=Enter Prefix: "
echo.

:: --- SCRIPT CONFIGURATION ---
set "outputFile=file_list_with_prefix.txt"
set "scriptName=%~nx0"
set "rootPath=%~dp0"

:: --- VALIDATE & CLEAN USER INPUT ---
:: Make sure the prefix is not empty
if not defined pathPrefix (
    echo No prefix entered. Exiting.
    pause
    exit /b
)
:: Replace any backslashes from the user's input
set "pathPrefix=!pathPrefix:\=/!"
:: Get the last character of the prefix
set "lastChar=!pathPrefix:~-1!"
:: Add a trailing slash if the user forgot one
if not "!lastChar!"=="/" (
    set "pathPrefix=!pathPrefix!/"
)

echo Generating file list with prefix...

:: --- MAIN LOGIC ---
(for /r "%rootPath%" %%F in (*) do (
    set "fileName=%%~nxF"

    :: Check that the file is not this script or the output file.
    if /i not "!fileName!"=="%scriptName%" if /i not "!fileName!"=="%outputFile%" (
        
        :: Print the final formatted string with the user's prefix and the filename.
        echo "!pathPrefix!!fileName!",
    )
)) > "%outputFile%"

endlocal

echo.
echo Done! Your list has been saved to: %outputFile%
echo.
pause
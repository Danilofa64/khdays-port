@echo off
setlocal
pushd "%~dp0..\.."
set "KHDAYS_TEST_VS="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "KHDAYS_TEST_VS=%%i"
if not defined KHDAYS_TEST_VS exit /b 1
call "%KHDAYS_TEST_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if not "%errorlevel%"=="0" exit /b %errorlevel%
if not exist build\tests mkdir build\tests
set "KHDAYS_TEST_FLAGS=/Od /Zi /utf-8"
if "%~1"=="asan" set "KHDAYS_TEST_FLAGS=/Od /Zi /utf-8 /fsanitize=address"
cl /nologo /W1 /std:c17 /Gy /Gw %KHDAYS_TEST_FLAGS% /c port\runtime\menus\camp.c /Fobuild\tests\camp_font_scope_impl.obj /Fdbuild\tests\camp_font_scope_impl.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /W4 /WX /std:c17 %KHDAYS_TEST_FLAGS% port\tests\camp_font_scope_test.c build\tests\camp_font_scope_impl.obj /Febuild\tests\camp_font_scope_test.exe /Fobuild\tests\camp_font_scope_test.obj /Fdbuild\tests\camp_font_scope_test.pdb /link /OPT:REF
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\camp_font_scope_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /W4 /WX /std:c17 %KHDAYS_TEST_FLAGS% /D_CRT_SECURE_NO_WARNINGS port\tests\hd_text_test.c /Fobuild\tests\hd_text_test.obj /Febuild\tests\hd_text_test.exe /Fdbuild\tests\hd_text_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\hd_text_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
setlocal
set "KHDAYS_HD_FONT_DISABLE=1"
build\tests\hd_text_test.exe --graphics-only
if not "%errorlevel%"=="0" exit /b %errorlevel%
endlocal
cl /nologo /W4 /WX /std:c17 %KHDAYS_TEST_FLAGS% /D_CRT_SECURE_NO_WARNINGS port\tests\ui_follow_test.c /Fobuild\tests\ui_follow_test.obj /Febuild\tests\ui_follow_test.exe /Fdbuild\tests\ui_follow_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\ui_follow_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /EHsc /W4 /WX /std:c++20 %KHDAYS_TEST_FLAGS% /D_CRT_SECURE_NO_WARNINGS port\tests\font_paths_test.cpp port\runtime\mods\font_raster.cpp /Fobuild\tests\ /Febuild\tests\font_paths_test.exe /Fdbuild\tests\font_paths_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\font_paths_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /EHsc /W4 /WX /std:c++20 %KHDAYS_TEST_FLAGS% /D_CRT_SECURE_NO_WARNINGS port\tests\font_secondary_test.cpp port\runtime\mods\font_raster.cpp /Fobuild\tests\ /Febuild\tests\font_secondary_test.exe /Fdbuild\tests\font_secondary_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\font_secondary_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /EHsc /W4 /WX /std:c++20 %KHDAYS_TEST_FLAGS% port\tests\font_raster_test.cpp port\runtime\mods\font_raster.cpp /Fobuild\tests\ /Febuild\tests\font_raster_test.exe /Fdbuild\tests\font_raster_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\font_raster_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /EHsc /W4 /WX /std:c++20 %KHDAYS_TEST_FLAGS% port\tests\font_raster_cache_test.cpp /Fobuild\tests\font_raster_cache_test.obj /Febuild\tests\font_raster_cache_test.exe /Fdbuild\tests\font_raster_cache_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\font_raster_cache_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /EHsc /W4 /WX /std:c++20 %KHDAYS_TEST_FLAGS% /Iassets\include assets\tests\test_font.cpp assets\src\font.cpp assets\src\message.cpp /Fobuild\tests\ /Febuild\tests\font_nftr_test.exe /Fdbuild\tests\font_nftr_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\font_nftr_test.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
set "KHDAYS_LAYOUT_GEN=build\port\gen-x64\rewritten\libs\nns\g2d\calls"
cl /nologo /W1 /std:c17 %KHDAYS_TEST_FLAGS% /Iport\compat /Ibuild\port\gen-x64 /c build\port\gen-x64\rewritten\src\engine\Text_DrawWithShadow.c build\port\gen-x64\rewritten\src\engine\Text_DrawDirectional_2.c /Fobuild\tests\ /Fdbuild\tests\font_fit_gen.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /W1 /std:c17 %KHDAYS_TEST_FLAGS% /Iport\compat /Ibuild\port\gen-x64 /c build\port\gen-x64\rewritten\src\engine\Obj_ForwardInnerPayload.c /Fobuild\tests\Obj_ForwardInnerPayload.obj /Fdbuild\tests\font_numbers_gen.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /W1 /std:c17 %KHDAYS_TEST_FLAGS% /Iport\compat /Ibuild\port\gen-x64 /c "%KHDAYS_LAYOUT_GEN%\Text_DrawGlyph.c" "%KHDAYS_LAYOUT_GEN%\Text_DrawLine.c" "%KHDAYS_LAYOUT_GEN%\Text_DrawAlignedLines.c" "%KHDAYS_LAYOUT_GEN%\Text_AlignAnchor.c" "%KHDAYS_LAYOUT_GEN%\NNSi_G2dFontGetStringWidth.c" "%KHDAYS_LAYOUT_GEN%\NNSi_G2dFontGetTextRect.c" /Fobuild\tests\ /Fdbuild\tests\font_layout_gen.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /W4 /WX /std:c17 %KHDAYS_TEST_FLAGS% /Iport\compat /Ibuild\port\gen-x64 port\tests\font_layout_test.c build\tests\Text_DrawGlyph.obj build\tests\Text_DrawLine.obj build\tests\Text_DrawAlignedLines.obj build\tests\Text_AlignAnchor.obj build\tests\NNSi_G2dFontGetStringWidth.obj build\tests\NNSi_G2dFontGetTextRect.obj build\tests\Obj_ForwardInnerPayload.obj build\tests\Text_DrawWithShadow.obj build\tests\Text_DrawDirectional_2.obj /Fobuild\tests\font_layout_test.obj /Febuild\tests\font_layout_test.exe /Fdbuild\tests\font_layout_test.pdb
if not "%errorlevel%"=="0" exit /b %errorlevel%
build\tests\font_layout_test.exe
exit /b %errorlevel%

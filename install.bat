@echo off
rem Copies the built VST3 into the system VST3 folder (needs admin).
set SRC=%~dp0build\vs\TxikiAlterboy_artefacts\Release\VST3\Txiki AlterBoy.vst3
xcopy /E /I /Y "%SRC%" "C:\Program Files\Common Files\VST3\Txiki AlterBoy.vst3" >nul

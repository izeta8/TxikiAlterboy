# Txiki AlterBoy

Clon funcional de Little AlterBoy (VST3, Windows x64). JUCE 8.0.4 en C:\SDKs\JUCE.

- `build.bat`       compila VST3 + Standalone y ejecuta los tests del procesador (presets/estado)
- `build_test.bat`  tests del DSP (pitch, formantes, aliasing, stereo, latencia, CPU, voces TTS)
- `install.bat`     copia el VST3 a C:\Program Files\Common Files\VST3 (ejecutar como admin)

Controles: Pitch ±12 st, Formant ±12 st (saltos de semitono, Shift = ajuste fino), Link,
Mode (Transpose / Quantize / Robot), Drive (saturación a válvula, 2x oversampling), Mix,
MIDI (C3 = pitch 0; en Robot la nota fija el tono). Robot con Pitch 0 = C4.

Interfaz: redimensionable (75%–200%, se recuerda), doble clic en los displays para escribir
valores, presets de fábrica + presets de usuario (SAVE / DEL) en %APPDATA%\TxikiAlterboy\Presets.

Latencia ~33 ms a 48 kHz (reportada al DAW).

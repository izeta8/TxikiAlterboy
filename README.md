# Txiki AlterBoy

Clon funcional de Little AlterBoy (VST3, Windows x64). JUCE 8.0.4 en C:\SDKs\JUCE.

- `build.bat`     compila VST3 + Standalone (build\vs\...\Release)
- `install.bat`   copia el VST3 a C:\Program Files\Common Files\VST3 (ejecutar como admin)
- `build_test.bat <carpeta>`  test offline del DSP + WAVs de demo

Controles: Pitch ±12 st, Formant ±12 st, Link, Mode (Transpose / Quantize / Robot),
Drive (saturación a válvula, 2x oversampling), Mix, MIDI (C3 = pitch 0; en Robot la nota fija el tono).
Robot con Pitch 0 = C4 (una octava sobre el Do central). Latencia ~33 ms a 48 kHz (reportada al DAW).

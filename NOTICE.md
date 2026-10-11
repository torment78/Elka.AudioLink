# Attribution and licenses

Elka AudioLink is licensed under GPL-2.0-or-later; see LICENSE.

The source files in `src/monitor` and the frontend test adapter originate from
VBAN Stream (`torment78/vban-stream`), snapshot
`0a39d29e72a40117346091d3fd2fc575af2c4465`. Their SPDX notices are preserved.
The monitor engine adds a bounded worker-side tunnel sink; the original repository
is not modified by this project.

OBS Studio SDK: https://github.com/obsproject/obs-studio/tree/31.1.1

Qt 6.8.3: https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.8.3
The Windows Qt SDK is the OBS dependency release `2025-07-11`.
Qt is dynamically linked. Distributed packages include the relevant license texts.

Mbed TLS 3.6.4 (linked from the same OBS dependency release) generates the local
receiver certificate and implements TLS. Its Apache-2.0 / GPL-2.0-or-later notices
are included in packages. Qt's host-dependent TLS plugins are not required.

Microsoft Visual C++ runtime binaries are redistributed under the Visual Studio
redistributable terms. Windows and VoiceMeeter are separate products and are not
included. AudioLink is not affiliated with VB-Audio or the OBS Project.

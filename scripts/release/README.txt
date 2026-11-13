OPENPCRASH
==========

OpenPCrash plays Phantom Crash (Genki, Xbox, 2002) on Windows, Mac and Linux.
It is a new program that plays the game from your own copy of it: no part of
the original game comes with this download.

OpenPCrash is not affiliated with or endorsed by Genki Co., Ltd. Phantom Crash
is a trademark of its owner.

The version you have is in VERSION.txt. Put it in any bug report.


YOU NEED YOUR OWN COPY

The first time you start OpenPCrash it asks for your Phantom Crash disc: drag
a disc image (.iso) you made from your own Xbox disc onto its window, or the
folder holding the game's files (default.xbe and Media). It reads it once,
here on your computer, which takes about a minute, and then the game starts.
Nothing is uploaded. Later starts go straight to the game.

It reads the North American (USA) release. A plain Xbox image (XISO) and a
full disc image with the video partition both work. A .zip or .7z has to be
unpacked first.


STARTING IT

Windows: run OpenPCrash.exe. Windows 10 or 11, 64-bit, with graphics
drivers that have Vulkan (most graphics cards and chips from 2016 on).

Linux: run ./OpenPCrash from this folder. 64-bit, a recent distribution
(glibc 2.38 or newer: Ubuntu 24.04, Fedora 39, Debian 13, Arch and newer),
and the Vulkan loader (libvulkan1 / vulkan-icd-loader) with Vulkan drivers
from Mesa or your GPU's maker.

Mac: open OpenPCrash.app. Apple Silicon (M1 or newer), macOS 12 or newer.
Nothing to install: Vulkan (MoltenVK) is inside the app. The app isn't
signed by Apple, so the first time macOS says it can't check it:
  - macOS 15 and newer: try to open it, then System Settings, Privacy &
    Security, scroll down, "Open Anyway" beside OpenPCrash.
  - macOS 12 to 14: right-click (or Ctrl-click) the app, Open, then Open.
  - Or, in Terminal, once:  xattr -dr com.apple.quarantine OpenPCrash.app
If macOS says the app is "damaged", that's the same thing: use the xattr
line.


CONTROLS

Keyboard and mouse: W A S D move, the mouse aims, left and right mouse
buttons fire the arm weapons, the mouse side buttons (or R and F) fire the
back weapons, Space jumps, Shift dashes, Q and E step, C camouflage, the
wheel or 3 and 4 change the view distance. Enter or Escape pauses. In menus:
arrows, Enter or Space to choose, Escape or Backspace to go back.

Pads: an Xbox-style pad works as the original's, button for button. Plug it
in any time. Several pads play split-screen VS.

Every binding can be changed in OPTIONS, PC CONFIG.


WHERE YOUR FILES ARE

Saves, settings, the game data read from your disc, and a log of each run:

Windows: %APPDATA%\OpenPCrash\OpenPCrash\
Linux:   ~/.local/share/OpenPCrash/OpenPCrash/
Mac:     ~/Library/Application Support/OpenPCrash/OpenPCrash/
         (in the Finder: Go, Go to Folder, and paste that)

The data/ and cooked/ folders there are made from your disc (about 3.3 GB).
Delete them to make OpenPCrash ask for the disc again. Your saves are in
saves/ and stay put.

Run logs are in logs/. Send the newest along with a bug report.


BUG REPORTS

The first time, OpenPCrash asks whether to send crash reports. They are off
unless you say yes. With yes, a crash, a hang or a start that fails sends a
report on its own, and REPORT A BUG (in the pause, and in OPTIONS, PC CONFIG,
GAME) sends your own note. A report holds the end of the run log with your
home folder taken out, the build, your system and GPU names, your PC CONFIG
settings and, from a fight, the fight's button presses (so it can be played
back exactly); your own report can add a small picture of the paused fight.
No names, and the server keeps no addresses. Turn it off in OPTIONS, PC
CONFIG, GAME, CRASH REPORTS.

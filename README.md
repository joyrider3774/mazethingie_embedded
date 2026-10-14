# Mazethingie Embedded Version
![DownloadCountTotal](https://img.shields.io/github/downloads/joyrider3774/mazethingie_embedded/total?label=total%20downloads&style=plastic) ![DownloadCountLatest](https://img.shields.io/github/downloads/joyrider3774/mazethingie_embedded/latest/total?style=plastic) ![LatestVersion](https://img.shields.io/github/v/tag/joyrider3774/mazethingie_embedded?label=Latest%20version&style=plastic) ![License](https://img.shields.io/github/license/joyrider3774/mazethingie_embedded?style=plastic)

Mazethingie is a maze generator / game. I initially developed it to learn how to generate mazes for the levels in Waternet.

## Screenshots
The game at twice its own 128x128:

| Title screen | In game |
| --- | --- |
| ![Mazethingie title screen](metadata/screenshots/title.png) | ![Mazethingie in game](metadata/screenshots/ingame.png) |

## Game Features:
- Every maze is generated on the spot, no two are the same
- Maze width and height can be set from 10 up to 30 rooms
- The maze size can also be changed while playing, which generates a new maze
- Saves the chosen maze size

## Playing the Game:
You are the little box, always starting in the lower right corner of the maze, and you need to try and reach the exit in the top left of the mazes you generated.

## Controls 

| Button | Action |
| ------ | ------ |
| Up / Down | Select menu, move player |
| Left / Right | Change maze width / height in the menu, move player |
| A | Confirm in menu, back to the title screen after solving a maze |
| B | Back to the title screen |
| (Hold) A + Dpad | Increase or decrease the maze width & height while playing |
| (A) + Left + Down | Show or hide the debug info |

### Buttons
The game's buttons on every device:

| Device | D-pad | A | B |
| ------ | ----- | - | - |
| ESPboy | d-pad | ACT | ESC |
| Gamebuino META | d-pad | A | B |
| Adafruit PyBadge | d-pad | A | B |
| Adafruit PyGamer | joystick | A | B |
| Pimoroni PicoSystem | d-pad | A | B |
| Pimoroni Explorer | A up, C down, B left, Y right | X | Z |
| Pimoroni Tufty 2350 | UP up, DOWN down, A left, C right | B | HOME |
| TinyCircuits Thumby Color | d-pad | A | B |
| CHGame | d-pad | A | B |
| Gamebuino AKA | d-pad | A | B |
| Playdate | d-pad | A | B |
| Libretro | d-pad | A | B |
| Game Boy Advance | d-pad | A | B |
| Nintendo DS | d-pad | A | B |
| Nintendo 3DS | d-pad or circle pad | A | B |
| Nintendo 64 | d-pad | A | B |
| PlayStation | d-pad | Cross | Circle |
| PlayStation Portable | d-pad or the analog stick | Cross | Circle |
| PlayStation Vita | d-pad or the left stick | Cross | Circle |
| Windows | arrow keys | X | C |
| MS-DOS | arrow keys | X | C |
| Browser | arrow keys | X | C |

On the Tufty 2350 a tap of HOME is B when it is let go.

On the Gamebuino META holding HOME for a second goes back to its loader.

On the CHGame holding START for 3 seconds goes back to the SD game menu.

## Devices
Every [release](https://github.com/joyrider3774/mazethingie_embedded/releases) has a build for every device. `releases/` is where a build of your own puts them, it is not part of the repository:

| Device | File | How to install |
| ------ | ---- | -------------- |
| [ESPboy](https://www.espboy.com/) | ESPboy_Mazethingie.bin | flash it, the board is a LOLIN(WEMOS) D1 mini |
| [Gamebuino META](https://gamebuino.com/gamebuino-meta) | GamebuinoMeta_Mazethingie.zip | unzip it onto the SD card, it holds a Mazethingie_embedded folder with the game, its save and the loader's images, the .hex in it is for flashing the game directly |
| [Adafruit PyBadge](https://www.adafruit.com/product/4200) | PyBadge_Mazethingie.uf2 | double press reset and copy it onto the drive that appears |
| [Adafruit PyGamer](https://www.adafruit.com/product/4242) | PyGamer_Mazethingie.uf2 | same as the PyBadge |
| [Pimoroni PicoSystem](https://shop.pimoroni.com/products/picosystem) | PicoSystem_Mazethingie.uf2 | hold X while switching on and copy it onto the drive that appears |
| [Pimoroni Explorer](https://shop.pimoroni.com/products/explorer?variant=42092697845843) | Explorer_Mazethingie.uf2 | hold BOOT while pressing RESET and copy it onto the drive that appears |
| [Pimoroni Tufty 2350](https://shop.pimoroni.com/products/tufty-2350?variant=55811986227579) | Tufty_Mazethingie.uf2 | hold HOME while pressing RESET and copy it onto the drive that appears |
| [TinyCircuits Thumby Color](https://tinycircuits.com/products/thumby-color) | ThumbyColor_Mazethingie.uf2 | put it into bootloader mode and copy it onto the RPI-RP2 drive that appears |
| [CHGame](https://github.com/bateske/CHGame) | CHGame_Mazethingie.bin | flash it over USB with the `chgame-upload` that comes with the board package: `chgame-upload -port COM6 flash CHGame_Mazethingie.bin -run` |
| [Gamebuino AKA](https://gamebuino.com/) | Aka_Mazethingie.bin | put it on the card the AKA's loader reads |
| [Playdate](https://play.date/) | Playdate_Mazethingie.pdx.zip | unzip it and sideload Mazethingie.pdx, the same pdx runs in the Playdate simulator |
| [Libretro / RetroArch](https://www.retroarch.com/) | Libretro_Mazethingie.zip | copy mazethingie_libretro.dll into RetroArch's cores folder and mazethingie_libretro.info into its info folder, then Load Core and Start Core |
| [Game Boy Advance](https://en.wikipedia.org/wiki/Game_Boy_Advance) | GBA_Mazethingie.gba | put it on a flash cart or open it in an emulator, the settings are saved in the cartridge's SRAM |
| [Nintendo DS](https://en.wikipedia.org/wiki/Nintendo_DS) | NDS_Mazethingie.nds | put it on a flash card or open it in an emulator, the settings are saved next to it in Mazethingie.sav |
| [Nintendo 3DS](https://en.wikipedia.org/wiki/Nintendo_3DS) | 3DS_Mazethingie.3dsx | copy it into /3ds/ on the SD card and start it from the Homebrew Launcher, or open it in an emulator, the settings are saved in sdmc:/3ds/Mazethingie/ |
| [Nintendo 64](https://en.wikipedia.org/wiki/Nintendo_64) | N64_Mazethingie.z64 | put it on a flash cart or open it in an emulator, the settings are saved in the cartridge EEPROM |
| [PlayStation](https://en.wikipedia.org/wiki/PlayStation_(console)) | PSX_Mazethingie.exe | open it in an emulator or send it to a console that runs unsigned code, the settings are not saved yet |
| [PlayStation Portable](https://en.wikipedia.org/wiki/PlayStation_Portable) | PSP_Mazethingie.PBP | rename it to EBOOT.PBP and put it in ms0:/PSP/GAME/Mazethingie/ on the memory stick, or open it in PPSSPP |
| [PlayStation Vita](https://en.wikipedia.org/wiki/PlayStation_Vita) | Vita_Mazethingie.vpk | install it with VitaShell on a Vita with homebrew enabled, or open it in Vita3K |
| Windows | Windows_Mazethingie.exe | runs on its own, the settings are saved next to it in Mazethingie.sav |
| MS-DOS | DOS_Mazethingie.zip | unzip MAZETHIN.EXE onto a DOS machine or into DOSBox and run it, the settings are saved next to it in MAZETHIN.SAV |
| MS-DOS, not dithered | DOS_Mazethingie_ND.zip | the same program with `DITHERING` 0, unzip MAZET_ND.EXE and run it the same way. On a 256 colour screen a shade the palette has no colour for is the nearer one it does have, instead of a pattern of the two |
| Browser | Web_Mazethingie.zip | upload it to an itch.io HTML project, or unzip it and open index.html from a web server, the settings are saved in the browser |

Holding RESET on the Tufty 2350 until the rear LEDs are dark puts it to sleep, a front button wakes it up again, with UP and DOWN held as well it goes into shipping mode instead.

The Thumby Color's display is 128x128, the game's own size, so it is shown 1:1 over the whole screen. That build has not been tried on the device itself yet.

The CHGame's display is 128x128 as well. The game draws straight to it, a 1 bpp screen buffer can be built in with `-DSCREENBUFFER=1`. Its 20464 bytes of RAM have no room for an 8 bpp one.

The Playdate shows the game in black and white, scaled up in the middle of its display. The menu marks the selected line with a `>` there, and the player is drawn white instead of red.

The Game Boy Advance shows the game scaled to 160x160 in the middle of its screen, with black bars at the sides.

On the Nintendo DS the game is scaled to 192x192 in the middle of the top screen, with black bars at the sides, and the bottom screen stays dark. The settings go into Mazethingie.sav on the card it was started from, so a card that libfat can not write to (or an emulator without one) plays the game but forgets them afterwards.

On the Nintendo 3DS the game is scaled to 240x240 in the middle of the top screen, with black bars at the sides, and the bottom screen stays dark. The settings go into sdmc:/3ds/Mazethingie/Mazethingie.sav.

On MS-DOS the game runs in VGA mode X, 320x240 in 256 colours, blown up to 240x240 in the middle of the screen with black bars at the sides. That mode rather than the usual 320x200 one because its pixels are square, where 320x200 is stretched over the same screen and would show the game a fifth too tall. The 256 colours are set to the RGB332 cube, which is exactly what the game's 8 bpp screen buffer holds, so a frame reaches the card without a colour being worked out. The settings are saved next to the program in MAZETHIN.SAV, and Escape quits. The program is 32 bit and carries the CWSDPMI host inside it, so it needs nothing beside it on the disk.

In a browser the game is drawn into a canvas of its own 128x128 pixels, which the page stretches to whatever room it is given while keeping it square and keeping the pixels sharp. The settings are saved in the browser's localStorage under the game's name, so a private window plays it but forgets them afterwards. The zip holds index.html, index.js and index.wasm and is what an itch.io HTML project takes as it is.

On the Nintendo 64 the game is drawn into memory in the colours the RDP takes and the RDP shows it scaled to 240x240 in the middle of its 320x240 screen, with black bars at the sides. The settings are saved in the cartridge EEPROM, which the ROM says it has, so a cartridge or an emulator without one plays the game but forgets them afterwards.

On the PlayStation the game is drawn into memory in the colours the GPU takes, handed to it as a texture and shown scaled to 240x240 in the middle of its 320x240 screen, with black bars at the sides. The memory card is not written yet, so the settings are gone when the console is switched off.

On the PlayStation Portable the game is doubled to 256x256 in the middle of the display, and the settings are saved next to the EBOOT.PBP in Mazethingie.sav.

On the PlayStation Vita the game is blown up four times to 512x512 in the middle of the display, and the settings are saved in ux0:data/Mazethingie/Mazethingie.sav.

## Building
`python tools/build_releases.py` builds a release for every device

### Where the tools are
The script looks for everything in the place it is installed in here. A tool somewhere else is passed on the command line, or set as the environment variable in the last column and left off the command line:

| Option | What it points at | Default, or environment variable |
| ------ | ----------------- | -------------------------------- |
| `--arduino-cli PATH` | arduino-cli, which builds the Arduino devices | `ARDUINO_CLI` |
| `--arduino DIR` | the Arduino IDE 1.8 folder, used when there is no arduino-cli | `C:/arduino`, `ARDUINO_DIR` |
| `--arduino2 DIR` | the Arduino IDE 2 folder, whose own arduino-cli builds the CHGame | `C:/arduino2`, `ARDUINO2_DIR` |
| `--lovyangfx DIR` | LovyanGFX for the Windows build, when it is not the one in the sketchbook | `LOVYANGFX_DIR` |
| `--msys2 DIR` | MSYS2's mingw64 bin folder, for cmake and ninja | `C:/msys64/mingw64/bin`, `MSYS2_BIN` |
| `--playdate-sdk DIR` | the Playdate SDK | `C:/playdate/PlaydateSDK`, `PLAYDATE_SDK_PATH` |
| `--playdate-arm DIR` | the bin folder of the ARM gcc the Playdate needs | `PLAYDATE_ARM_BIN` |
| `--libretro-common DIR` | libretro-common | `C:/github/libretro-common`, `LIBRETRO_COMMON_DIR` |
| `--idf DIR` | ESP-IDF, for the Gamebuino AKA | `C:/github/esp-idf`, `IDF_PATH` |
| `--idf-tools DIR` | where ESP-IDF installed its tools | `IDF_TOOLS_PATH`, else `~/.espressif` or `C:/Espressif` |
| `--aka-lib DIR` | a checkout of Gamebuino_AKA_lib | `C:/github/Gamebuino_AKA_lib`, `AKA_LIB_DIR` |
| `--devkitpro DIR` | devkitARM with libgba, libnds, calico, libctru and tools | `C:/devkitarm`, `DEVKITPRO` |
| `--psn00bsdk DIR` | PSn00bSDK | `C:/psn00bsdk`, `PSN00BSDK_PREFIX` |
| `--n64 DIR` | the mips64-elf toolchain with libdragon | `C:/n64_dev`, `N64_INST` |
| `--emsdk DIR` | the Emscripten SDK | `C:/github/emsdk`, `EMSDK` |
| `--dosdev DIR` | DJGPP with CWSDPMI | `C:/dos_dev`, `DOSDEV` |
| `--pspdev DIR` | the pspdev toolchain | `C:/psp_dev`, `PSPDEV_DIR` |
| `--vitasdk DIR` | VitaSDK | `C:/psvita_dev`, `VITASDK` |
| `--sdl2-mingw DIR` | SDL2's mingw package, its x86_64-w64-mingw32 folder | `SDL2_MINGW` |

Only the devices being built need their tool, so one missing toolchain does not stop the rest:

```
python tools/build_releases.py --only N64 DOS --n64 D:/n64_dev --dosdev D:/dos_dev
python tools/build_releases.py --list          shows what would be built
python tools/build_releases.py --only Web      one device only
```

### Build settings
Every device is built with its own settings. These change them for all of the devices at once, and `--list` shows what the defines would be without building anything. They are the same defines the device headers and the `platforms/*/CMakeLists.txt` files take, so a single device can be built with `-D<name>=<value>` from cmake instead:

| Option | What it sets | Values |
| ------ | ------------ | ------ |
| `--forcescreenbuffer N` | `SCREENBUFFER`, where drawing goes | `0`, `1`, `8` or `16` bits per pixel |
| `--forcescale N` | `SCALESCREEN`, how the game fills the display | `1` blown up, `0` 1:1 in the middle |
| `--forcewindowscale N` | `WINDOW_SCALE`, how big the Windows window opens | `1` to `8` times the game's size |
| `--forcedithering N` | `DITHERING`, whether an 8 or 1 bpp buffer spreads its colours | `1` spread, `0` the nearest colour |
| `--forcedebug` | `FORCEDEBUG 1`, the debug header is always shown | no value, on when it is given |
| `--define NAME[=VALUE]` | any other define, for every build | `VALUE` defaults to `1` |

Not every device takes every buffer mode, the device header in `source/mazethingie_embedded/Platform<device>.h` says which, and one it does not take stops that build with a message. A 1 bpp buffer only shows white on black, so there the grey menu items and the red player are drawn white and the selected menu line is marked with a `>`.

```
python tools/build_releases.py --only Windows --forcescreenbuffer 1
python tools/build_releases.py --list --forcedebug          what the defines would be
```

### Board packages and libraries
The Arduino devices are built with arduino-cli 1.5.1 and the versions below. They are the ones every release is built with, `.github/workflows/build-releases.yml` pins them:

| Device | Board package | Libraries |
| ------ | ------------- | --------- |
| ESPboy | esp8266:esp8266 3.1.2 | LovyanGFX 1.1.9, TFT_eSPI 2.4.72 |
| Gamebuino META | gamebuino:samd 1.2.2 | Gamebuino META 1.3.3 |
| Adafruit PyBadge, PyGamer | adafruit:samd 1.7.16 | Adafruit GFX Library 1.12.6, Adafruit ST7735 and ST7789 Library 1.5.15, Adafruit BusIO 1.17.4, Adafruit NeoPixel 1.15.5, Adafruit SPIFlash 5.1.1 |
| PicoSystem, Explorer, Tufty 2350, Thumby Color | rp2040:rp2040 5.5.0 | none, everything they use comes with the core |
| CHGame | CHGame:ch32v 0.3.0 | none, the core brings its own riscv-none-embed-gcc |

The ESPboy draws through LovyanGFX and only includes TFT_eSPI's header, so the exact TFT_eSPI does not matter much.  
The Gamebuino's core needs Arduino's own arduino:samd 1.8.14 beside it for sam.h, without it the build stops at "sam.h: No such file or directory".  
The CHGame's board package is only published for the Arduino IDE 2, so that device is built with the
arduino-cli that IDE 2 ships (`--arduino2`) while the rest use the IDE 1.8 folder, in the same run.
Its CH32X035 has 50944 bytes of flash for the game and 20464 bytes of RAM, see
`source/mazethingie_embedded/PlatformCHGame.h`.

The Windows build draws through the same LovyanGFX 1.1.9, see `platforms/windows/CMakeLists.txt`.

### Toolchains
The Playdate build also needs the Playdate SDK, see `platforms/playdate/CMakeLists.txt`  
The libretro core needs libretro-common, see `platforms/libretro/CMakeLists.txt`  
The Gamebuino AKA build needs ESP-IDF and Gamebuino_AKA_lib, see `platforms/aka/CMakeLists.txt`  
The Game Boy Advance build needs devkitARM and libgba, see `platforms/gba/CMakeLists.txt`  
The Nintendo DS build needs devkitARM, libnds and calico, see `platforms/nds/CMakeLists.txt`  
The Nintendo 3DS build needs devkitARM and libctru, see `platforms/3ds/CMakeLists.txt`  
The PlayStation build needs PSn00bSDK, see `platforms/psx/CMakeLists.txt`  
The Nintendo 64 build needs the mips64-elf toolchain and libdragon, see `platforms/n64/CMakeLists.txt`  
The PSP build needs the pspdev toolchain, see `platforms/psp/CMakeLists.txt` (pspdev has no Windows build, so on Windows it is built from WSL)  
The Vita build needs VitaSDK, see `platforms/vita/CMakeLists.txt`  
The browser build needs Emscripten, see `platforms/web/CMakeLists.txt`  
The MS-DOS build needs DJGPP, see `platforms/dos/CMakeLists.txt`


## Credits
Mazethingie was created by Willems Davy.

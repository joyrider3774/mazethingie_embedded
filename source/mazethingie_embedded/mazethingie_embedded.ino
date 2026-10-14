//The Arduino IDE needs a sketch file named after the folder, but nothing lives here: the
//game starts in Main.cpp (Game_Setup / Game_Loop) and the setup() and loop() that call it
//are in the device's own source: PlatformESPboy.cpp for the ESPboy, PlatformGamebuino.cpp
//for the Gamebuino META, PlatformPyBadge.cpp for the Adafruit PyBadge and PyGamer,
//PlatformPicoSystem.cpp for the Pimoroni PicoSystem, PlatformExplorer.cpp for the Pimoroni Explorer,
//PlatformTufty.cpp for the Pimoroni Tufty 2350, PlatformThumby.cpp for the TinyCircuits Thumby Color
//(the board picked in the IDE decides which one builds).
//PlatformPlaydate.cpp is the Playdate's, built with playdate/CMakeLists.txt, and PlatformLibretro.cpp the
//libretro core's, built with libretro/CMakeLists.txt, and PlatformGBA.cpp the Game Boy Advance's, built
//with gba/CMakeLists.txt, and PlatformPSP.cpp the PlayStation Portable's, built with
//psp/CMakeLists.txt, and PlatformVita.cpp the PlayStation Vita's, built with vita/CMakeLists.txt,
//none of them from the IDE

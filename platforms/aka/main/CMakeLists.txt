# The game itself as the project's "main" component. The sources are the game's own folder,
# the same set every other platform builds, minus the platform sources of other devices:
# each of those is behind its own #ifdef, but they include headers this device does not have.

file(GLOB GAME_SOURCES "${GAME_DIR}/*.cpp")
# every platform's source compiles to nothing unless its own PLATFORM_ define is set, but the
# ones that include a library this build has no headers for cannot be handed to the compiler
list(FILTER GAME_SOURCES EXCLUDE REGEX "Platform(ESPboy|Gamebuino|PyBadge|PicoSystem|Explorer|Tufty|Thumby|SDL|Web|DOS|Playdate|Libretro|GBA|NDS|3DS|PSX|N64|PSP|Vita)\.cpp$")

idf_component_register(
    SRCS ${GAME_SOURCES}
    INCLUDE_DIRS "${GAME_DIR}"
    REQUIRES gamebuino esp_timer app_update
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PLATFORM_AKA
    SCREENBUFFER=${SCREENBUFFER}
    DITHERING=${DITHERING}
    SCALESCREEN=${SCALESCREEN}
    FORCEDEBUG=${FORCEDEBUG}
    BACKLIGHT=${BACKLIGHT})

# the game is not written to ESP-IDF's warning settings, and it is not this port's place to
# change the game's own sources over them
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=all -Wno-error=extra -Wno-unused-variable -Wno-unused-function
    -Wno-missing-field-initializers -Wno-narrowing -Wno-sign-compare -Wno-char-subscripts
    -Wno-error=format-truncation)

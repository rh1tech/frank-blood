# frank-blood — the game as a static library: jfbuild (classic renderer),
# Blood (NBlood-Amiga), jfmact and jfaudiolib, built for the RP2350.
# Its .bss goes to PSRAM (memmap_blood.ld matches libblood.a).
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later

set(NB ${REPO_ROOT}/third_party/nblood-amiga)

set(BLOOD_ENGINE_SRC a-c.c asmprot.c baselayer.c cache1d.c compat.c crc32.c
    defs.c engine.c kplib.c mmulti_null.c osd.c pragmas.c scriptfile.c
    textfont.c smalltextfont.c talltextfont.c)
list(TRANSFORM BLOOD_ENGINE_SRC PREPEND ${NB}/jfbuild/src/)

set(BLOOD_GAME_SRC actor ai aibat aibeast aiboneel aiburn aicaleb aicerber
    aicult aigarg aighost aigilbst aihand aihound aiinnoc aipod airat aispid
    aitchern aizomba aizombf asound blood callback choke common compat config
    controls credits db demo dude endgame eventq fire fx gamemenu gameutil
    getopt gfx gib globals gui inifile iob levels loadsave map2d menu messages
    mirrors misc network osdcmd palette player qav qheap replace resource
    screen sectorfx seq sfx sound tile trig triggers view warp weapon)
list(TRANSFORM BLOOD_GAME_SRC PREPEND ${NB}/src/)
list(TRANSFORM BLOOD_GAME_SRC APPEND .cpp)

set(BLOOD_MACT_SRC control.c file_lib.c keyboard.c mouse.c scriplib.c util_lib.c)
list(TRANSFORM BLOOD_MACT_SRC PREPEND ${NB}/jfmact/)

set(BLOOD_AUDIO_SRC drivers.c fx_man.c cd.c multivoc.c mix.c mixst.c pitch.c
    music.c midi.c driver_nosound.c asssys.c)
list(TRANSFORM BLOOD_AUDIO_SRC PREPEND ${NB}/jfaudiolib/src/)

add_library(blood STATIC
    ${BLOOD_ENGINE_SRC} ${BLOOD_GAME_SRC} ${BLOOD_MACT_SRC} ${BLOOD_AUDIO_SRC}
    ${NB}/libsmacker/smacker.c
    src/game/frlayer.c
    src/game/driver_frank.c
    src/game/version.c)
target_include_directories(blood PUBLIC
    ${NB}/jfbuild/include ${NB}/jfmact ${NB}/jfaudiolib/include ${NB}/libsmacker
    PRIVATE src/game/sysinc ${NB}/src ${NB}/jfbuild/src ${NB}/jfaudiolib/src src src/game)
target_compile_definitions(blood PRIVATE
    NB_SMALL NETCODE_DISABLE ENGINE_19960925 ASS_REVERSESTEREO NO_GCC_BUILTINS
    USE_POLYMOST=0 USE_OPENGL=0 USE_ASM=0 FRANK_BOARD=1 HAVE_FRANK=1
    ${FRANK_DEFINITIONS})
# The game passes int32_t* where it wants int* (int32_t is long on
# arm-none-eabi, int on the hosts it was written on): same size, so make it int.
# C++14: C++17's std::byte clashes with MACT's byte under `using namespace std`.
target_compile_options(blood PRIVATE -O2 -fno-strict-aliasing -funsigned-char
    -U__INT32_TYPE__ "-D__INT32_TYPE__=int" -U__UINT32_TYPE__ "-D__UINT32_TYPE__=unsigned int"
    -U__INT_LEAST32_TYPE__ "-D__INT_LEAST32_TYPE__=int" -U__UINT_LEAST32_TYPE__ "-D__UINT_LEAST32_TYPE__=unsigned int"
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++14>
    -Wno-write-strings -Wno-char-subscripts -Wno-unused -Wno-sign-compare
    -Wno-implicit-fallthrough
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -Wno-narrowing>)
target_link_libraries(blood PUBLIC pico_stdlib)

# Music: Apogee's AL_MIDI + GM timbre bank (GPL-2.0-or-later, from Chocolate
# Blood) on emu8950 (MIT, from murmduke32), driven by opl_frank.c. Its own
# library: the Apogee headers (sndcards.h, ...) clash with jfaudiolib's.
add_library(opl STATIC
    ${REPO_ROOT}/third_party/apogee_opl/al_midi.c
    ${REPO_ROOT}/third_party/apogee_opl/ll_man.c
    ${REPO_ROOT}/third_party/apogee_opl/gmtimbre.c
    ${REPO_ROOT}/third_party/emu8950/emu8950.c
    src/game/opl_frank.c)
target_include_directories(opl PRIVATE
    ${REPO_ROOT}/third_party/apogee_opl ${REPO_ROOT}/third_party/emu8950
    ${NB}/jfaudiolib/src src/game)
target_compile_definitions(opl PRIVATE
    USE_EMU8950_OPL=1 EMU8950_NO_TIMER=1 EMU8950_NO_RATECONV=1 EMU8950_SLOT_RENDER=0)
target_compile_options(opl PRIVATE -O2 -Wno-unused -Wno-sign-compare)
target_link_libraries(opl PUBLIC pico_stdlib)
target_link_libraries(blood PUBLIC opl)

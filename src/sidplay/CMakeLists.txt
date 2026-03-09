# Vendored libsidplayfp 2.16.1 + reSIDfp — built as a static library.
# Replaces the autotools build with a minimal CMake target.
# License: GPL-2.0-or-later (see COPYING in this directory).

add_library(sidplayfp STATIC
    # --- reSIDfp engine (SID chip emulator) ---
    builders/residfp-builder/residfp/Dac.cpp
    builders/residfp-builder/residfp/EnvelopeGenerator.cpp
    builders/residfp-builder/residfp/ExternalFilter.cpp
    builders/residfp-builder/residfp/Filter.cpp
    builders/residfp-builder/residfp/Filter6581.cpp
    builders/residfp-builder/residfp/Filter8580.cpp
    builders/residfp-builder/residfp/FilterModelConfig.cpp
    builders/residfp-builder/residfp/FilterModelConfig6581.cpp
    builders/residfp-builder/residfp/FilterModelConfig8580.cpp
    builders/residfp-builder/residfp/Integrator6581.cpp
    builders/residfp-builder/residfp/Integrator8580.cpp
    builders/residfp-builder/residfp/OpAmp.cpp
    builders/residfp-builder/residfp/SID.cpp
    builders/residfp-builder/residfp/Spline.cpp
    builders/residfp-builder/residfp/WaveformCalculator.cpp
    builders/residfp-builder/residfp/WaveformGenerator.cpp
    builders/residfp-builder/residfp/resample/SincResampler.cpp
    builders/residfp-builder/residfp/version.cc

    # --- residfp-builder adapter ---
    builders/residfp-builder/residfp-builder.cpp
    builders/residfp-builder/residfp-emu.cpp

    # --- Core libsidplayfp ---
    EventScheduler.cpp
    mixer.cpp
    player.cpp
    psiddrv.cpp
    reloc65.cpp
    sidemu.cpp
    simpleMixer.cpp

    # --- Public API facade ---
    sidplayfp/sidbuilder.cpp
    sidplayfp/SidConfig.cpp
    sidplayfp/SidInfo.cpp
    sidplayfp/sidplayfp.cpp
    sidplayfp/SidTune.cpp
    sidplayfp/SidTuneInfo.cpp

    # --- SID tune format parsers ---
    sidtune/MUS.cpp
    sidtune/p00.cpp
    sidtune/prg.cpp
    sidtune/PSID.cpp
    sidtune/SidTuneBase.cpp
    sidtune/SidTuneTools.cpp

    # --- C64 hardware emulation ---
    c64/c64.cpp
    c64/mmu.cpp
    c64/CPU/mos6510.cpp
    c64/CPU/mos6510debug.cpp
    c64/CIA/interrupt.cpp
    c64/CIA/mos652x.cpp
    c64/CIA/SerialPort.cpp
    c64/CIA/timer.cpp
    c64/CIA/tod.cpp
    c64/VIC_II/mos656x.cpp

    # --- Utilities (internal MD5, ini parser, song length DB) ---
    utils/iniParser.cpp
    utils/md5Factory.cpp
    utils/SidDatabase.cpp
    utils/MD5/MD5.cpp
)

# This is third-party vendored code — silence all warnings.
target_compile_options(sidplayfp PRIVATE -w)

# The reSIDfp engine benefits from fast math (matches upstream RESIDFP_CXXFLAGS).
set(RESIDFP_SOURCES
    builders/residfp-builder/residfp/Dac.cpp
    builders/residfp-builder/residfp/EnvelopeGenerator.cpp
    builders/residfp-builder/residfp/ExternalFilter.cpp
    builders/residfp-builder/residfp/Filter.cpp
    builders/residfp-builder/residfp/Filter6581.cpp
    builders/residfp-builder/residfp/Filter8580.cpp
    builders/residfp-builder/residfp/FilterModelConfig.cpp
    builders/residfp-builder/residfp/FilterModelConfig6581.cpp
    builders/residfp-builder/residfp/FilterModelConfig8580.cpp
    builders/residfp-builder/residfp/Integrator6581.cpp
    builders/residfp-builder/residfp/Integrator8580.cpp
    builders/residfp-builder/residfp/OpAmp.cpp
    builders/residfp-builder/residfp/SID.cpp
    builders/residfp-builder/residfp/Spline.cpp
    builders/residfp-builder/residfp/WaveformCalculator.cpp
    builders/residfp-builder/residfp/WaveformGenerator.cpp
    builders/residfp-builder/residfp/resample/SincResampler.cpp
    builders/residfp-builder/residfp/version.cc
)
set_source_files_properties(${RESIDFP_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-ffast-math;-fno-unsafe-math-optimizations"
)

# Include paths matching upstream AM_CPPFLAGS.
target_include_directories(sidplayfp
    PRIVATE
        # Internal headers (#include "player.h", #include "SidTuneBase.h", etc.)
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/builders/residfp-builder
        ${CMAKE_CURRENT_SOURCE_DIR}/builders/residfp-builder/residfp
        ${CMAKE_CURRENT_SOURCE_DIR}/sidtune
    PUBLIC
        # Public API: <sidplayfp/sidplayfp.h>, <sidplayfp/builders/residfp.h>, etc.
        # The directory layout under sidplay/ mirrors the installed include tree:
        #   sidplay/sidplayfp/sidplayfp.h  ->  <sidplayfp/sidplayfp.h>
        #   sidplay/sidplayfp/builders/residfp.h  ->  <sidplayfp/builders/residfp.h>
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# The code uses #ifdef HAVE_CONFIG_H / #include "config.h" in several TUs.
target_compile_definitions(sidplayfp PRIVATE HAVE_CONFIG_H)

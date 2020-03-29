cmake_minimum_required(VERSION 3.0)

project(aeolus VERSION "0.9.7")
add_definitions(-DVERSION=\"${PROJECT_VERSION}\")

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

set(LIBDIR /usr/local/lib)
set(BINDIR /usr/local/bin)
add_definitions(-DLIBDIR=\"${LIBDIR}\")

find_path(JACK_INCLUDE_DIR NAMES jack/jack.h)
include_directories(${JACK_INCLUDE_DIR})
find_library(JACK_LIBRARY NAMES jack)

find_path(X11_INCLUDE_DIR NAMES X11/Xos.h)
include_directories(${X11_INCLUDE_DIR} ${X11_INCLUDE_DIR}/freetype2)
find_library(X11_LIBRARY NAMES X11)
find_library(XFT_LIBRARY NAMES Xft)

find_path(CLXCLIENT_INCLUDE_DIR NAMES clxclient.h)
include_directories(${CLXCLIENT_INCLUDE_DIR})
find_library(CLXCLIENT_LIBRARY NAMES clxclient)

find_path(CLTHREADS_INCLUDE_DIR NAMES clthreads.h)
include_directories(${CLTHREADS_INCLUDE_DIR})
find_library(CLTHREADS_LIBRARY NAMES clthreads)

add_library(aeolus_txt MODULE
    source/tiface.cc
    source/tiface.h
)
set_property(TARGET aeolus_txt PROPERTY PREFIX "")
target_link_libraries(aeolus_txt 
    ${CLTHREADS_LIBRARY}
    readline
)

add_library(aeolus_x11 MODULE
    source/xiface.cc
    source/xiface.h
    source/addsynth.cc
    source/addsynth.h
    source/audiowin.cc
    source/audiowin.h
    source/editwin.cc
    source/editwin.h
    source/functionwin.cc
    source/functionwin.h
    source/instrwin.cc
    source/instrwin.h
    source/mainwin.cc
    source/mainwin.h
    source/midimatrix.cc
    source/midimatrix.h
    source/midiwin.cc
    source/midiwin.h
    source/multislider.cc
    source/multislider.h
    source/styles.cc
    source/styles.h
)
set_property(TARGET aeolus_x11 PROPERTY PREFIX "")
target_link_libraries(aeolus_x11 
    ${X11_LIBRARY} 
    ${XFT_LIBRARY}
    ${CLXCLIENT_LIBRARY}
    ${CLTHREADS_LIBRARY}
)
install(TARGETS aeolus_txt aeolus_x11 DESTINATION ${LIBDIR})

set(AEOLUS_SRC
    source/addsynth.cc
    source/addsynth.h
    source/asection.cc
    source/asection.h
    source/audio.cc
    source/audio.h
    source/audio_jack.cc
    source/audio_jack.h
    source/callbacks.h
    source/division.cc
    source/division.h
    source/exp2ap.cc
    source/global.h
    source/iface.h
    source/imidi.cc
    source/imidi.h
    source/lfqueue.cc
    source/lfqueue.h
    source/main.cc
    source/messages.h
    source/model.cc
    source/model.h
    source/prbsgen.h
    source/rankwave.cc
    source/rankwave.h
    source/reverb.cc
    source/reverb.h
    source/rngen.cc
    source/rngen.h
    source/scales.cc
    source/scales.h
    source/slave.cc
    source/slave.h
)
if(LINUX)
    list(APPEND AEOLUS_SRC
        source/imidi_alsa.cc
        source/imidi_alsa.h
	source/audio_alsa.cc
	source/audio_alsa.h
    )
elseif(APPLE)
    list(APPEND AEOLUS_SRC
        source/imidi_coremidi.cc
        source/imidi_coremidi.h
	source/audio_coreaudio.cc
	source/audio_coreaudio.h
    )
endif()

add_executable(aeolus
  ${AEOLUS_SRC}
)
target_link_libraries(aeolus 
    ${CLTHREADS_LIBRARY}
    ${JACK_LIBRARY}
)
if(LINUX)
  target_link_libraries(aeolus asound pthread zita-alsa-pcmi dl rt)
elseif(APPLE)
  find_library(COCOA Cocoa)
  find_library(CORE_MIDI CoreMIDI)
  find_library(AUDIO_TOOLBOX AudioToolbox)
  target_link_libraries(aeolus ${COCOA} ${CORE_MIDI} ${AUDIO_TOOLBOX})
endif()
install(TARGETS aeolus DESTINATION ${BINDIR})

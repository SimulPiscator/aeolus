// ----------------------------------------------------------------------------
//
//  Copyright (C) 2003-2013 Fons Adriaensen <fons@linuxaudio.org>
//    
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------


#ifndef __IMIDI_H
#define __IMIDI_H


#include <stdlib.h>
#include <stdio.h>
#include <clthreads.h>
#include "lfqueue.h"
#include "messages.h"
#if __linux__
# include <alsa/asoundlib.h>
#else
enum {
    SND_SEQ_EVENT_NOTEON = 1,
    SND_SEQ_EVENT_NOTEOFF,
    SND_SEQ_EVENT_CONTROLLER,
    SND_SEQ_EVENT_PGMCHANGE,
    SND_SEQ_EVENT_NONE
};
#endif

class Imidi : public A_thread
{
public:

    Imidi (Lfq_u32 *qnote, Lfq_u8 *qmidi, uint16_t *midimap, const char *appname);
    virtual ~Imidi (void);

    void terminate (void);
    
protected:
    struct MidiEvent
    {
        int type;
        union {
            struct { int channel, note, velocity; } note;
            struct { int channel, param, value; } control;
        };
    };
    void proc_midi_event(const MidiEvent&);

    void open_midi (void);
    void close_midi (void);
    virtual void on_open_midi() = 0;
    virtual void on_close_midi() = 0;
    virtual void on_terminate() = 0;

protected:
    const char     *_appname;
    int             _client;
    int             _ipport;

private:
    Lfq_u32        *_qnote;
    Lfq_u8         *_qmidi; 
    uint16_t       *_midimap;
};


#endif

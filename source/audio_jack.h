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


#ifndef __AUDIO_JACK_H
#define __AUDIO_JACK_H

#include "audio.h"
#include <jack/jack.h>

class Audio_jack : public Audio
{
public:

    Audio_jack (const char *name, Lfq_u32 *qnote, Lfq_u32 *qcomm, const char *server, bool bform, Lfq_u8 *qmidi);
    virtual ~Audio_jack (void);

private:
   
    void  init (const char *server, bool bform, Lfq_u8 *qmidi);
    void close (void);

    virtual void thr_main (void) {}

    void proc_jmidi (int tmax);
    void jack_shutdown (void);
    int  jack_callback (jack_nframes_t);
    void on_synth_period (int);

    static void jack_static_shutdown (void *);
    static int  jack_static_callback (jack_nframes_t, void *);
    
    Lfq_u8         *_qmidi;

    jack_client_t  *_jack_handle;
    jack_port_t    *_jack_opport [8];
    jack_port_t    *_jack_midipt;
    int             _jmidi_count;
    int             _jmidi_index;
    void           *_jmidi_pdata;

    static const char *_ports_stereo [2];
    static const char *_ports_ambis1 [4];
};


#endif


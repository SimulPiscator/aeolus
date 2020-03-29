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


#include "imidi_alsa.h"
#include "messages.h"

Imidi_alsa::Imidi_alsa (Lfq_u32 *qnote, Lfq_u8 *qmidi, uint16_t *midimap, const char *appname) 
   : Imidi(qnote, qmidi, midimap, appname),
    _handle(NULL)
{
}

void Imidi_alsa::on_terminate (void)
{
    snd_seq_event_t E;

    if (_handle)
    {   
	snd_seq_ev_clear (&E);
	snd_seq_ev_set_direct (&E);
	E.type = SND_SEQ_EVENT_USR0;
	E.source.port = _opport;
	E.dest.client = _client;
	E.dest.port   = _ipport;
	snd_seq_event_output_direct (_handle, &E);
    }
}


void Imidi_alsa::thr_main (void)
{
    open_midi ();
    proc_midi ();
    close_midi ();
    send_event (EV_EXIT, 1);
}


void Imidi_alsa::on_open_midi (void)
{
    snd_seq_client_info_t *C;

    if (snd_seq_open (&_handle, "hw", SND_SEQ_OPEN_DUPLEX, 0) < 0)
    {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        exit(1);
    }

    snd_seq_client_info_alloca (&C);
    snd_seq_get_client_info (_handle, C);
    _client = snd_seq_client_info_get_client (C);
    snd_seq_client_info_set_name (C, _appname);
    snd_seq_set_client_info (_handle, C);

    if ((_ipport = snd_seq_create_simple_port (_handle, "In",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
    {
        fprintf(stderr, "Error creating sequencer input port.\n");
        exit(1);
    }

    if ((_opport = snd_seq_create_simple_port (_handle, "Out",
         SND_SEQ_PORT_CAP_WRITE,
         SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
    {
        fprintf(stderr, "Error creating sequencer output port.\n");
        exit(1);
    }
}


void Imidi_alsa::on_close_midi (void)
{
    if (_handle) snd_seq_close (_handle);
}

void Imidi_alsa::proc_midi (void)
{
    // Read and process MIDI commands from the ALSA port.
    // Events related to keyboard state are sent to the 
    // audio thread via the qnote queue. All the rest is
    // sent as raw MIDI to the model thread via qmidi.

    while (true)
    {
        snd_seq_event_t  *E;
	    snd_seq_event_input(_handle, &E);

        if(E->type == SND_SEQ_EVENT_USR0) {
            // User event, terminates this trhead if we sent it.
            if (E->source.client == _client) return;
        }

        MidiEvent ev = { 0 };
        ev.type = E->type;
        switch(ev.type) {
            case SND_SEQ_EVENT_NOTEON:
            case SND_SEQ_EVENT_NOTEOFF:
                ev.note.channel = E->data.note.channel;
                ev.note.note = E->data.note.note;
                ev.note.velocity = E->data.note.velocity;
                break;
            case SND_SEQ_EVENT_PGMCHANGE:
            case SND_SEQ_EVENT_CONTROLLER:
                ev.control.param = E->data.control.param;
                ev.control.value = E->data.control.value;
                break;
        }
    }
}

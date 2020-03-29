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


#include "audio_jack.h"
#include <jack/midiport.h>
#include "messages.h"

Audio_jack::Audio_jack (const char *name, Lfq_u32 *qnote, Lfq_u32 *qcomm, const char *server, bool bform, Lfq_u8 *qmidi) :
    Audio(name, qnote, qcomm),
    _qmidi (0),
    _jack_handle (0)
{
    init(server, bform, qmidi);
}

Audio_jack::~Audio_jack (void)
{
    if (_jack_handle) close ();
}

void Audio_jack::init (const char *server, bool bform, Lfq_u8 *qmidi)
{
    int                 i;
    int                 opts;
    jack_status_t       stat;
    struct sched_param  spar;
    const char          **p;
    
    _bform = bform;
    _qmidi = qmidi;

    opts = JackNoStartServer;
    if (server) opts |= JackServerName;
    _jack_handle = jack_client_open (_appname, (jack_options_t) opts, &stat, server);
    if (!_jack_handle)
    {
        fprintf (stderr, "Error: can't connect to JACK\n");
        exit (1);
    }
    _appname = jack_get_client_name (_jack_handle);

    jack_set_process_callback (_jack_handle, jack_static_callback, (void *)this);
    jack_on_shutdown (_jack_handle, jack_static_shutdown, (void *)this);

    if (_bform)
    {
	_nplay = 4;
	p = _ports_ambis1;
    }
    else
    {
	_nplay = 2;
	p = _ports_stereo;
    }

    for (i = 0; i < _nplay; i++)
    {
        _jack_opport [i] = jack_port_register (_jack_handle, p [i], JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	if (!_jack_opport [i])
	{
	    fprintf (stderr, "Error: can't create the '%s' jack port\n", p [i]);
	    exit (1);
	}
    }

    if (_qmidi)
    {
        _jack_midipt = jack_port_register (_jack_handle, "Midi/in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        if (!_jack_midipt)
        {
	    fprintf (stderr, "Error: can't create the 'Midi/in' jack port\n");
	    exit (1);
	}
    }
	
    _fsamp = jack_get_sample_rate (_jack_handle);
    _fsize = jack_get_buffer_size (_jack_handle);
    _jmidi_pdata = 0;
    init_audio ();

    if (jack_activate (_jack_handle))
    {
        fprintf(stderr, "Error: can't activate JACK.");
        exit (1);
    }

    pthread_getschedparam (jack_client_thread_id (_jack_handle), &_policy, &spar);
    _abspri = spar.sched_priority;
    _relpri = spar.sched_priority -  sched_get_priority_max (_policy);
}


void Audio_jack::close ()
{
    jack_deactivate (_jack_handle);
    for (int i = 0; i < _nplay; i++) jack_port_unregister(_jack_handle, _jack_opport [i]);
    jack_client_close (_jack_handle);
}


void Audio_jack::jack_static_shutdown (void *arg)
{
    return ((Audio_jack *) arg)->jack_shutdown ();
}


void Audio_jack::jack_shutdown (void)
{
    _running = false;
    send_event (EV_EXIT, 1);
}


int Audio_jack::jack_static_callback (jack_nframes_t nframes, void *arg)
{
    return ((Audio_jack *) arg)->jack_callback (nframes);
}


int Audio_jack::jack_callback (jack_nframes_t nframes)
{
    int i;

    proc_queue (_qnote);
    proc_queue (_qcomm);
    proc_keys1 ();
    proc_keys2 ();
    for (i = 0; i < _nplay; i++) _outbuf [i] = (float *)(jack_port_get_buffer (_jack_opport [i], nframes));
    _jmidi_pdata = jack_port_get_buffer (_jack_midipt, nframes);
    _jmidi_count = jack_midi_get_event_count (_jmidi_pdata);
    _jmidi_index = 0;
    proc_synth (nframes);
    proc_mesg ();
    return 0;
}


void Audio_jack::proc_jmidi (int tmax)
{
    int                 c, f, m, n, t, v;
    jack_midi_event_t   E;

    // Read and process MIDI commands from the JACK port.
    // Events related to keyboard state are dealt with
    // locally. All the rest is sent as raw MIDI to the
    // model thread via qmidi.

    while (   (jack_midi_event_get (&E, _jmidi_pdata, _jmidi_index) == 0)
           && (E.time < (jack_nframes_t) tmax))
    {
	t = E.buffer [0];
        n = E.buffer [1];
	v = E.buffer [2];
	c = t & 0x0F;
        m = _midimap [c] & 127;        // Keyboard and hold bits
//        d = (_midimap [c] >>  8) & 7;  // Division number if (f & 2)
        f = (_midimap [c] >> 12) & 7;  // Control enabled if (f & 4)

	switch (t & 0xF0)
	{
	case 0x80:
	case 0x90:
	    // Note on or off.
	    if (v && (t & 0x10))
	    {
		// Note on.
		if (n < 36)
		{
		    if ((f & 4) && (n >= 24) && (n < 34))
		    {
			// Preset selection, sent to model thread
			// if on control-enabled channel.
			if (_qmidi->write_avail () >= 3)
			{
			    _qmidi->write (0, t);
			    _qmidi->write (1, n);
			    _qmidi->write (2, v);
			    _qmidi->write_commit (3);
			}
		    }
		}
		else if (n <= 96)
		{
		    if (m) key_on (n - 36, m & _hold);
		}
	    }
	    else
	    {
		// Note off.
		if (n < 36)
		{
		}
		else if (n <= 96)
		{
		    if (m) key_off (n - 36, m & KEYS_MASK);
		}
	    }
	    break;

	case 0xB0: // Controller
	    switch (n)
	    {
	    case MIDICTL_HOLD:
		// Hold pedal.
                if (m & HOLD_MASK)
		{
                    if (v > 63)
                    {
		        _hold = KEYS_MASK | HOLD_MASK;
			cond_key_on (m, HOLD_MASK);
		    }
                    else
		    {
		        _hold = KEYS_MASK;
			cond_key_off (HOLD_MASK, HOLD_MASK);
		    }                    
		}                    
		break;

	    case MIDICTL_ASOFF:
		// All sound off, accepted on control channels only.
		// Clears all keyboards, including held notes.
		if (f & 4) cond_key_off (ALL_MASK, ALL_MASK);
		break;

	    case MIDICTL_ANOFF:
		// All notes off, accepted on channels controlling
		// a keyboard. Does not clear held notes. 
		if (m) cond_key_off (m, m);
		break;
	
	    case MIDICTL_BANK:	
	    case MIDICTL_IFELM:	
                // Program bank selection or stop control, sent
                // to model thread if on control-enabled channel.
		if (f & 4)
		{
		    if (_qmidi->write_avail () >= 3)
		    {
			_qmidi->write (0, t);
			_qmidi->write (1, n);
			_qmidi->write (2, v);
			_qmidi->write_commit (3);
		    }
		}
	    case MIDICTL_SWELL:
	    case MIDICTL_TFREQ:
	    case MIDICTL_TMODD:
		// Per-division performance controls, sent to model
                // thread if on a channel that controls a division.
		if (f & 2)
		{
		    if (_qmidi->write_avail () >= 3)
		    {
			_qmidi->write (0, t);
			_qmidi->write (1, n);
			_qmidi->write (2, v);
			_qmidi->write_commit (3);
		    }
		}
		break;
	    }
	    break;

	case 0xC0:
            // Program change sent to model thread
            // if on control-enabled channel.
            if (f & 4)
   	    {
	        if (_qmidi->write_avail () >= 3)
	        {
		    _qmidi->write (0, t);
		    _qmidi->write (1, n);
		    _qmidi->write (2, 0);
		    _qmidi->write_commit (3);
		}
	    }
	    break;
	}	
	_jmidi_index++;
    }
}

void Audio_jack::on_synth_period(int sample)
{
	if (_jmidi_pdata)
	{
	    proc_jmidi (sample + PERIOD);
	    proc_keys1 ();
	}
}

const char *Audio_jack::_ports_stereo [2] = { "out.L", "out.R" };
const char *Audio_jack::_ports_ambis1 [4] = { "out.W", "out.X", "out.Y", "out.Z" };

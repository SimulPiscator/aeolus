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


#include "imidi.h"

Imidi::Imidi (Lfq_u32 *qnote, Lfq_u8 *qmidi, uint16_t *midimap, const char *appname) :
    A_thread ("Imidi"),
    _appname (appname),
    _qnote(qnote),
    _qmidi(qmidi),
    _midimap (midimap),
    _client(0),
    _ipport(0)
{
}


Imidi::~Imidi (void)
{
}

void Imidi::open_midi (void)
{
    on_open_midi();

    M_midi_info *M = new M_midi_info ();
    M->_client = _client;
    M->_ipport = _ipport;
    memcpy (M->_chbits, _midimap, 16 * sizeof (uint16_t));
    send_event (TO_MODEL, M);
}


void Imidi::close_midi (void)
{
    on_close_midi();
}

void Imidi::terminate()
{
    on_terminate();
}

void Imidi::proc_midi_event(const MidiEvent &ev)
{
    int              c, f, m, t, n, v, p;

    c = ev.note.channel;
    m = _midimap [c] & 127;        // Keyboard and hold bits
//        d = (_midimap [c] >>  8) & 7;  // Division number if (f & 2)
    f = (_midimap [c] >> 12) & 7;  // Control enabled if (f & 4)

    t = ev.type;
	switch (t)
	{ 
	case SND_SEQ_EVENT_NOTEON:
	case SND_SEQ_EVENT_NOTEOFF:
	    n = ev.note.note;
	    v = ev.note.velocity;
            if ((t == SND_SEQ_EVENT_NOTEON) && v)
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
		            _qmidi->write (0, 0x90);
		            _qmidi->write (1, n);
		            _qmidi->write (2, v);
		            _qmidi->write_commit (3);
			}
	            }
	        }
                else if (n <= 96)
	        {
                    if (m)
		    {
	                if (_qnote->write_avail () > 0)
	                {
	                    _qnote->write (0, (1 << 24) | ((n - 36) << 8) | m);
                            _qnote->write_commit (1);
	                } 
		    }
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
                    if (m)
		    {
	                if (_qnote->write_avail () > 0)
	                {
	                    _qnote->write (0, ((n - 36) << 8) | m);
                            _qnote->write_commit (1);
	                } 
		    }
		}
	    }
	    break;

	case SND_SEQ_EVENT_CONTROLLER:
	    p = ev.control.param;
	    v = ev.control.value;
	    switch (p)
	    {
	    case MIDICTL_HOLD:
		// Hold pedal.
		if (m & HOLD_MASK)
		{
		    v = (v > 63) ? 9 : 8;
	            if (_qnote->write_avail () > 0)
	            {
	                _qnote->write (0, (v << 24) | (m << 16));
                        _qnote->write_commit (1);
	            } 
		}
		break;

	    case MIDICTL_ASOFF:
		// All sound off, accepted on control channels only.
		// Clears all keyboards, including held notes.
		if (f & 4)
		{
	            if (_qnote->write_avail () > 0)
	            {
	                _qnote->write (0, (2 << 24) | ( ALL_MASK << 16) | ALL_MASK);
                        _qnote->write_commit (1);
	            } 
		}
		break;

            case MIDICTL_ANOFF:
		// All notes off, accepted on channels controlling
		// a keyboard. Does not clear held notes. 
		if (m)
		{
	            if (_qnote->write_avail () > 0)
	            {
	                _qnote->write (0, (2 << 24) | (m << 16) | m);
                        _qnote->write_commit (1);
	            } 
		}
                break;

	    case MIDICTL_BANK:	
	    case MIDICTL_IFELM:	
                // Program bank selection or stop control, sent
                // to model thread if on control-enabled channel.
		if (f & 4)
		{
		    if (_qmidi->write_avail () >= 3)
		    {
			_qmidi->write (0, 0xB0 | c);
			_qmidi->write (1, p);
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
			_qmidi->write (0, 0xB0 | c);
			_qmidi->write (1, p);
			_qmidi->write (2, v);
			_qmidi->write_commit (3);
		    }
		}
		break;
	    }
	    break;

	case SND_SEQ_EVENT_PGMCHANGE:
            // Program change sent to model thread
            // if on control-enabled channel.
	    if (f & 4)
	    {
   	        if (_qmidi->write_avail () >= 3)
	        {
		    _qmidi->write (0, 0xC0);
		    _qmidi->write (1, ev.control.value);
		    _qmidi->write (2, 0);
		    _qmidi->write_commit (3);
		}
            }
	    break;

    }
}


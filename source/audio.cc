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


#include <math.h>
#include "audio.h"
#include "messages.h"



Audio::Audio (const char *name, Lfq_u32 *qnote, Lfq_u32 *qcomm) :
    A_thread("Audio"),
    _appname (name),
    _qnote (qnote),
    _qcomm (qcomm),
    _running (false),
    _abspri (0),
    _relpri (0),
    _nplay (0),
    _fsamp (0),
    _fsize (0),
    _bform (0),
    _nasect (0),
    _ndivis (0)
{
}


Audio::~Audio (void)
{
    int i;

    for (i = 0; i < _nasect; i++) delete _asectp [i];
    for (i = 0; i < _ndivis; i++) delete _divisp [i];
    _reverb.fini ();
}


void Audio::init_audio (void)
{
    int i;
    
    _audiopar [VOLUME]._val = 0.32f;
    _audiopar [VOLUME]._min = 0.00f;
    _audiopar [VOLUME]._max = 1.00f;
    _audiopar [REVSIZE]._val = _revsize = 0.075f;
    _audiopar [REVSIZE]._min =  0.025f;
    _audiopar [REVSIZE]._max =  0.150f;
    _audiopar [REVTIME]._val = _revtime = 4.0f;
    _audiopar [REVTIME]._min =  2.0f;
    _audiopar [REVTIME]._max =  7.0f;
    _audiopar [STPOSIT]._val =  0.5f;
    _audiopar [STPOSIT]._min = -1.0f;
    _audiopar [STPOSIT]._max =  1.0f;

    _reverb.init (_fsamp);
    _reverb.set_t60mf (_revtime);
    _reverb.set_t60lo (_revtime * 1.50f, 250.0f);
    _reverb.set_t60hi (_revtime * 0.50f, 3e3f);

    _nasect = NASECT;
    for (i = 0; i < NASECT; i++)
    {
        _asectp [i] = new Asection ((float) _fsamp);
        _asectp [i]->set_size (_revsize);
    }
    _hold = KEYS_MASK;
}


void Audio::start (void)
{
    M_audio_info  *M;
    int           i;

    M = new M_audio_info ();
    M->_nasect = _nasect;
    M->_fsamp  = _fsamp;
    M->_fsize  = _fsize;
    M->_instrpar = _audiopar;
    for (i = 0; i < _nasect; i++) M->_asectpar [i] = _asectp [i]->get_apar ();
    send_event (TO_MODEL, M);
}

void Audio::proc_queue (Lfq_u32 *Q)
{
    uint32_t  k;
    int       b, c, i, j, n;
    union     { uint32_t i; float f; } u;

    // Execute commands from the model thread (qcomm),
    // or from the midi thread (qnote).

    n = Q->read_avail ();
    while (n > 0)
    {
	k = Q->read (0);
        c = k >> 24;      
        j = (k >> 16) & 255;
        i = (k >>  8) & 255; 
        b = k & 255;

        switch (c)
	{
	case 0:
	    // Single key off.
            key_off (i, b);
	    Q->read_commit (1);
	    break;

	case 1:
	    // Single key on.
            key_on (i, b);
	    Q->read_commit (1);
	    break;

	case 2:
	    // Conditional key off.
	    cond_key_off (j, b);
	    Q->read_commit (1);
	    break;

	case 3:
	    // Conditional key on.
	    cond_key_on (j, b);
	    Q->read_commit (1);
	    break;

        case 4:
	    // Clear bits in division mask.
            _divisp [j]->clr_div_mask (b); 
	    Q->read_commit (1);
            break;

        case 5:
	    // Set bits in division mask.
            _divisp [j]->set_div_mask (b); 
	    Q->read_commit (1);
            break;

        case 6:
	    // Clear bits in rank mask.
            _divisp [j]->clr_rank_mask (i, b); 
	    Q->read_commit (1);
            break;

        case 7:
	    // Set bits in rank mask.
            _divisp [j]->set_rank_mask (i, b); 
	    Q->read_commit (1);
            break;

        case 8:
	    // Hold off.
            _hold = KEYS_MASK;
	    cond_key_off (HOLD_MASK, HOLD_MASK);
	    Q->read_commit (1);
	    break;

        case 9:
	    // Hold on.
            _hold = KEYS_MASK | HOLD_MASK;
	    cond_key_on (j, HOLD_MASK);
	    Q->read_commit (1);
	    break;

        case 16:
	    // Tremulant on/off.
            if (b) _divisp [j]->trem_on (); 
            else   _divisp [j]->trem_off ();
	    Q->read_commit (1);
            break;

        case 17:
	    // Per-division performance controllers.
	    if (n < 2) return;
            u.i = Q->read (1);
            Q->read_commit (2);        
            switch (i)
 	    {
            case 0: _divisp [j]->set_swell (u.f); break;
            case 1: _divisp [j]->set_tfreq (u.f); break;
            case 2: _divisp [j]->set_tmodd (u.f); break;
            break;
	    }
	}
        n = Q->read_avail ();
    }
}


void Audio::proc_keys1 (void)
{    
    int d, m, n;

    for (n = 0; n < NNOTES; n++)
    {
	m = _keymap [n];
	if (m & 128)
	{
            m &= 127;
   	    _keymap [n] = m;
            for (d = 0; d < _ndivis; d++) _divisp [d]->update (n, m);
	}
    }
}


void Audio::proc_keys2 (void)
{    
    int d;

    for (d = 0; d < _ndivis; d++) _divisp [d]->update (_keymap);
}


void Audio::proc_synth (int nframes) 
{
    int           j, k;
    float         W [PERIOD];
    float         X [PERIOD];
    float         Y [PERIOD];
    float         Z [PERIOD];
    float         R [PERIOD];
    float        *out [8];

    if (fabsf (_revsize - _audiopar [REVSIZE]._val) > 0.001f)
    {
        _revsize = _audiopar [REVSIZE]._val;
	_reverb.set_delay (_revsize);
        for (j = 0; j < _nasect; j++) _asectp[j]->set_size (_revsize);
    }
    if (fabsf (_revtime - _audiopar [REVTIME]._val) > 0.1f)
    {
        _revtime = _audiopar [REVTIME]._val;
 	_reverb.set_t60mf (_revtime);
 	_reverb.set_t60lo (_revtime * 1.50f, 250.0f);
 	_reverb.set_t60hi (_revtime * 0.50f, 3e3f);
    }

    for (j = 0; j < _nplay; j++) out [j] = _outbuf [j];
    for (k = 0; k < nframes; k += PERIOD)
    {
        on_synth_period(k);

        memset (W, 0, PERIOD * sizeof (float));
        memset (X, 0, PERIOD * sizeof (float));
        memset (Y, 0, PERIOD * sizeof (float));
        memset (Z, 0, PERIOD * sizeof (float));
        memset (R, 0, PERIOD * sizeof (float));

        for (j = 0; j < _ndivis; j++) _divisp [j]->process ();
        for (j = 0; j < _nasect; j++) _asectp [j]->process (_audiopar [VOLUME]._val, W, X, Y, R);
        _reverb.process (PERIOD, _audiopar [VOLUME]._val, R, W, X, Y, Z);

        if (_bform)
	{
            for (j = 0; j < PERIOD; j++)
            {
	        out [0][j] = W [j];
	        out [1][j] = 1.41 * X [j];
	        out [2][j] = 1.41 * Y [j];
	        out [3][j] = 1.41 * Z [j];
   	    }
	}
        else
        {
            for (j = 0; j < PERIOD; j++)
            {
	        out [0][j] = W [j] + _audiopar [STPOSIT]._val * X [j] + Y [j];
	        out [1][j] = W [j] + _audiopar [STPOSIT]._val * X [j] - Y [j];
   	    }
	}
	for (j = 0; j < _nplay; j++) out [j] += PERIOD;
    }
}


void Audio::proc_mesg (void) 
{
    ITC_mesg *M;

    while (get_event_nowait () != EV_TIME)
    {
	M = get_message ();
        if (! M) continue; 

        switch (M->type ())
	{
	    case MT_NEW_DIVIS:
	    {
	        M_new_divis  *X = (M_new_divis *) M;
                Division     *D = new Division (_asectp [X->_asect], (float) _fsamp);
                D->set_div_mask (X->_dmask);
                D->set_swell (X->_swell);
                D->set_tfreq (X->_tfreq);
                D->set_tmodd (X->_tmodd);
                _divisp [_ndivis] = D;
                _ndivis++;
                break; 
	    }
	    case MT_CALC_RANK:
	    case MT_LOAD_RANK:
	    {
	        M_def_rank *X = (M_def_rank *) M;
                _divisp [X->_divis]->set_rank (X->_rank, X->_wave,  X->_sdef->_pan, X->_sdef->_del);  
                send_event (TO_MODEL, M);
                M = 0;
	        break;
	    }
	    case MT_AUDIO_SYNC:
                send_event (TO_MODEL, M);
                M = 0;
		break;
	} 
        if (M) M->recover ();
    }
}

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


#ifndef __ADDSYNTH_H
#define __ADDSYNTH_H


#include <stdio.h>
#include <stdint.h>


#define N_NOTE 11
#define N_HARM 64
#define NOTE_MIN 36
#define NOTE_MAX 96



class N_func
{
public:

    N_func (void);
    void reset (float v);
    void setv (int i, float v);
    void clrv (int i);
    float vs (int i) const { return _v [i]; } // value set
    int   st (int i) const { return (_b & (1 << i)) ? 1 : 0; } // has value been set?
    float vi (int n) const // value interpolated, index scaled by factor 6
    {
	int   i = n / 6;
        int   k = n - 6 * i;
        float v = _v [i];
        if (k) v += k * (_v [i + 1] - v) / 6; 
        return v;
    } 	

    void write (FILE *F);
    void read (FILE *F);
                
private:

    int   _b;          // bitmask indicating values that have been set, bit 4 set if all entries have been set to the same value via clrv()
    float _v [N_NOTE]; // values at notes 36, 42, 48, 54, 60, 66, 72, 78, 84, 90, 96
};



class HN_func
{
public:

    HN_func (void);
    void reset (float v);
    void setv (int i, float v);
    void clrv (int i);
    void setv (int h, int i, float v) { _h [h].setv (i, v); }
    void clrv (int h, int i) { _h [h].clrv (i); }     
    float vs (int h, int i) const { return _h [h].vs (i); }
    int   st (int h, int i) const { return _h [h].st (i); }
    float vi (int h, int n) const { return _h [h].vi (n); }
    void write (FILE *F, int k);
    void read (FILE *F, int k);
                
private:

    N_func _h [N_HARM];
};



class Addsynth
{
public:

    Addsynth (void);

    void reset (void);
    int save (const char *sdir); 
    int load (const char *sdir);
    
    char       _filename [64]; 
    char       _stopname [32];
    char       _copyrite [56];
    char       _mnemonic [8];
    char       _comments [56];
    char       _reserved [8];
    int32_t    _n0;     // first note
    int32_t    _n1;     // last note
    int32_t    _fn;     // frequency multiplier numerator, 1 for 16', 2 for 4'
    int32_t    _fd;     // frequency multiplier denominator, 2 for 16', 1 for 4'
    N_func     _n_vol;  // pipe amplitude in dB
    N_func     _n_off;  // pipe offset in Hz
    N_func     _n_ran;  // random pipe offset amplitude in Hz
    N_func     _n_ins;  // instability in cents
    N_func     _n_att;  // attack duration in s
    N_func     _n_atd;  // attack detune in cents
    N_func     _n_dct;  // release time in s
    N_func     _n_dcd;  // release detune in cents
    HN_func    _h_lev;  // harmonic's amplitude level in dB
    HN_func    _h_ran;  // harmonic's random amplitude level variation in dB (?)
    HN_func    _h_att;  // harmonic's attack duration in s
    HN_func    _h_atp;  // harmonic's attack peak in dB

    char       _pan;    // panning position: 'L', 'C', or 'R'
    int32_t    _del;    // reverb delay in s
};


#endif


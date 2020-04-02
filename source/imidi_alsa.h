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


#ifndef __IMIDI_ALSA_H
#define __IMIDI_ALSA_H

#include "imidi.h"
#include <alsa/asoundlib.h>

class Imidi_alsa : public Imidi
{
public:
    Imidi_alsa (Lfq_u32 *qnote, Lfq_u8 *qmidi, uint16_t *midimap, const char *appname);

protected:
    virtual void on_open_midi (void);
    virtual void on_close_midi (void);
    virtual void on_terminate (void);

private:
    virtual void thr_main (void);
    void proc_midi (void);

    int             _opport;
    snd_seq_t      *_handle;
};

#endif

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


#ifndef __AUDIO_COREAUDIO_H
#define __AUDIO_COREAUDIO_H


#include <AudioToolbox/AudioToolbox.h>
#include "audio.h"

class Audio_coreaudio : public Audio
{
public:

    Audio_coreaudio (const char *jname, Lfq_u32 *qnote, Lfq_u32 *qcomm, int fsamp, int fsize);
    virtual ~Audio_coreaudio (void);

private:
   
    void  init (int fsamp, int fsize);
    void close ();
    virtual void thr_main (void) {}
    void coreaudio_callback(int nframes, AudioBufferList*);
    static OSStatus coreaudio_static_callback(void *,
                      AudioUnitRenderActionFlags *,
                      const AudioTimeStamp *,
                      UInt32, UInt32, AudioBufferList *);

    AudioUnit       _coreaudio_handle;
};


#endif


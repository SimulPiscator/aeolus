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


#include "audio_coreaudio.h"

Audio_coreaudio::Audio_coreaudio (const char *name, Lfq_u32 *qnote, Lfq_u32 *qcomm, int fsamp, int fsize) :
    Audio (name, qnote, qcomm),
    _coreaudio_handle(0)
{
    init(fsamp, fsize);
}

Audio_coreaudio::~Audio_coreaudio (void)
{
    if (_coreaudio_handle) close();
}

void Audio_coreaudio::init(int fsamp, int fsize)
{
    AudioComponentDescription outputcd = {0};
    outputcd.componentType = kAudioUnitType_Output;
    outputcd.componentSubType = kAudioUnitSubType_DefaultOutput;
    outputcd.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    AudioComponent comp = ::AudioComponentFindNext(NULL, &outputcd);
    if (comp == NULL) {
        fprintf (stderr, "CoreAudio: can't get output unit\n");
        exit(1);
    }
    OSStatus err = ::AudioComponentInstanceNew(comp, &_coreaudio_handle);
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't open component for output unit\n");
        exit(1);
    }
    
    Float64 sampleRate = fsamp;
    AudioUnitSetProperty(_coreaudio_handle,
                         kAudioUnitProperty_SampleRate,
                         kAudioUnitScope_Input,
                         0,
                         &sampleRate,
                         sizeof(sampleRate));
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't set sample rate to %d\n", fsamp);
        exit(1);
    }

    UInt32 paramSize = sizeof(sampleRate);
    AudioUnitGetProperty(_coreaudio_handle,
                         kAudioUnitProperty_SampleRate,
                         kAudioUnitScope_Input,
                         0,
                         &sampleRate,
                         &paramSize);
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't get sample rate\n");
        exit(1);
    }
    if(sampleRate != fsamp)
        fprintf(stderr, "Warning: CoreAudio: sample rate is now %f\n", sampleRate);

    UInt32 frameSize = fsize;
    err = AudioUnitSetProperty(_coreaudio_handle,
                         kAudioDevicePropertyBufferFrameSize,
                         kAudioUnitScope_Input,
                         0,
                         &frameSize,
                         sizeof(frameSize));
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't set frame size to %d\n", fsize);
        exit(1);
    }

    paramSize = sizeof(frameSize);
    err = AudioUnitGetProperty(_coreaudio_handle,
                         kAudioDevicePropertyBufferFrameSize,
                         kAudioUnitScope_Input,
                         0,
                         &frameSize,
                         &paramSize);
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't get frame size\n");
        exit(1);
    }
    if(frameSize != fsize)
        fprintf(stderr, "Warning: CoreAudio: frame size is now %d\n", frameSize);

    _fsamp = sampleRate;
    _fsize = frameSize;
    _nplay = 2;
    init_audio ();

    AURenderCallbackStruct input;
    input.inputProc = &coreaudio_static_callback;
    input.inputProcRefCon = this;
    err = AudioUnitSetProperty(_coreaudio_handle,
                                kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input,
                                0,
                                &input,
                                sizeof(input));
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't set callback function\n");
        exit(1);
    }
        
    err = AudioUnitInitialize(_coreaudio_handle);
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't initialize output unit\n");
        exit(1);
    }
    
    err = AudioOutputUnitStart(_coreaudio_handle);
    if(err) {
        fprintf(stderr, "CoreAudio: couldn't start output unit\n");
        exit(1);
    }
}

void Audio_coreaudio::close()
{
    AudioOutputUnitStop(_coreaudio_handle);
    AudioUnitUninitialize(_coreaudio_handle);
    AudioComponentInstanceDispose(_coreaudio_handle);
    _coreaudio_handle = NULL;
}

OSStatus Audio_coreaudio::coreaudio_static_callback(void *refCon,
          AudioUnitRenderActionFlags *,
          const AudioTimeStamp *,
          UInt32,
          UInt32 inNumberFrames, AudioBufferList *ioData)
{
    static_cast<Audio_coreaudio*>(refCon)->coreaudio_callback(inNumberFrames, ioData);
    return noErr;
}

void Audio_coreaudio::coreaudio_callback(int nframes, AudioBufferList* bufs)
{
    proc_queue (_qnote);
    proc_queue (_qcomm);
    proc_keys1 ();
    proc_keys2 ();
    for (int i = 0; i < _nplay; i++) _outbuf [i] = static_cast<float*>(bufs->mBuffers[i].mData);
    proc_synth (nframes);
    proc_mesg ();
}

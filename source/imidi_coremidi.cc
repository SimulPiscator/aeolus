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


#include "imidi_coremidi.h"

Imidi_coremidi::Imidi_coremidi (Lfq_u32 *qnote, Lfq_u8 *qmidi, uint16_t *midimap, const char *appname) : Imidi(qnote, qmidi, midimap, appname),
    _handle(NULL),
    _endpoint(NULL)
{
}


void Imidi_coremidi::on_terminate (void)
{
    put_event (EV_EXIT, 1);
}


void Imidi_coremidi::thr_main (void)
{
    open_midi ();
    get_event (1 << EV_EXIT);
    close_midi ();
    send_event (EV_EXIT, 1);
}


void Imidi_coremidi::on_open_midi (void)
{
    CFStringRef name = CFStringCreateWithCString(0, _appname, kCFStringEncodingUTF8);
    MIDIClientCreate(name, NULL, NULL, &_handle);
    MIDIEndpointRef dest = NULL;
    MIDIDestinationCreate(_handle, name, &coremidi_callback, this, &dest);
}

void Imidi_coremidi::on_close_midi (void)
{
    MIDIEndpointDispose(_endpoint);
    _endpoint = NULL;
    MIDIClientDispose(_handle);
    _handle = NULL;
}

void Imidi_coremidi::coremidi_callback(const MIDIPacketList *pktlist, void *refCon, void *connRefCon)
{
    Imidi_coremidi* pInstance = static_cast<Imidi_coremidi*>(refCon);
    const MIDIPacket* packet = pktlist->packet;
    bool skip = false;
    for(int i = 0; i < pktlist->numPackets; ++i) {
        const Byte* pByte = packet->data;
        while(pByte - packet->data < packet->length) {
            if(skip) {
                if(*pByte == 0xf7)
                    skip = false;
                ++pByte;
                continue;
            }
            Imidi::MidiEvent ev = { 0 };
            int upper = *pByte & 0xf0,
                lower = *pByte & 0x0f;
            ev.note.channel = lower;
            switch(upper) {
                case 0x80: // note off
                    ev.type = SND_SEQ_EVENT_NOTEOFF;
                    ev.note.note = *++pByte;
                    ev.note.velocity = *++pByte;
                    break;
                case 0x90: // note on
                    ev.type = SND_SEQ_EVENT_NOTEON;
                    ev.note.note = *++pByte;
                    ev.note.velocity = *++pByte;
                    break;
                case 0xb0: // control change
                    ev.type = SND_SEQ_EVENT_CONTROLLER;
                    ev.control.param = *++pByte;
                    ev.control.value = *++pByte;
                    break;
                case 0xd0: // program change
                    ev.type = SND_SEQ_EVENT_PGMCHANGE;
                    ev.control.value = *++pByte;
                    break;
                case 0xa0: // after touch
                case 0xe0: // pitch bend change
                    ev.type = SND_SEQ_EVENT_NONE;
                    pByte += 2;
                    break;
                case 0xc0: // channel pressure
                    ev.type = SND_SEQ_EVENT_NONE;
                    pByte += 1;
                    break;
                case 0xf0: // system
                    ev.type = SND_SEQ_EVENT_NONE;
                    switch(lower) {
                        case 0x00: // sysex
                            skip = true;
                            break;
                        case 0x01: // midi time point quarter frame
                        case 0x03: // song position pointer
                            pByte += 1;
                            break;
                        case 0x02: // song select
                            pByte += 2;
                            break;
                        // remaining system events have no data
                    }
            }
            ++pByte;
            pInstance->proc_midi_event(ev);
        }
        packet = MIDIPacketNext(packet);
    }
}

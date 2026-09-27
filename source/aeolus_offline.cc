// ----------------------------------------------------------------------------
//
//  Aeolus Offline Renderer
//  Pure Mathematical Synthesis directly from MIDI to WAV
//  Zero ALSA / Zero JACK / Zero PipeWire / Zero Real-Time Sleeping
//
// ----------------------------------------------------------------------------

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <chrono>
#include <cmath>
#include <clthreads.h>

#include "audio.h"
#include "imidi.h"
#include "model.h"
#include "slave.h"
#include "iface.h"
#include "messages.h"
#include "global.h"

// MIDI Event structure
struct MidiEventRecord {
    uint64_t sample_offset;
    int type;      // 1: Note On, 2: Note Off, 3: Controller, 4: ProgChange
    int channel;   // 0-15
    int param1;    // note number or controller number
    int param2;    // velocity or controller value
};

// Simple SMF (Standard MIDI File) Parser
class MidiParser {
public:
    static uint32_t read_vlq(const uint8_t *&p, const uint8_t *end) {
        uint32_t val = 0;
        while (p < end) {
            uint8_t b = *p++;
            val = (val << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return val;
    }

    struct RawTrackEvent {
        uint64_t tick;
        int type;
        int channel;
        int p1;
        int p2;
    };

    struct TempoChange {
        uint64_t tick;
        uint32_t us_per_quarter;
    };

    static bool parse(const std::string &path, uint32_t sample_rate, std::vector<MidiEventRecord> &out_events, uint64_t &total_samples) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            std::cerr << "Error: cannot open MIDI file: " << path << std::endl;
            return false;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        if (data.size() < 14) {
            std::cerr << "Error: MIDI file too short" << std::endl;
            return false;
        }

        const uint8_t *p = data.data();
        const uint8_t *end = p + data.size();

        if (memcmp(p, "MThd", 4) != 0) {
            std::cerr << "Error: not a valid MIDI file (missing MThd)" << std::endl;
            return false;
        }
        p += 4;
        uint32_t hlen = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        p += 4;
        uint16_t format = (p[0] << 8) | p[1];
        uint16_t ntracks = (p[2] << 8) | p[3];
        uint16_t division = (p[4] << 8) | p[5];
        p += hlen;

        std::vector<TempoChange> tempos;
        tempos.push_back({0, 500000}); // Default 120 BPM (500,000 us/quarter)

        std::vector<RawTrackEvent> raw_events;

        for (uint16_t trk = 0; trk < ntracks && p < end; ++trk) {
            if (p + 8 > end) break;
            if (memcmp(p, "MTrk", 4) != 0) {
                // Seek to next MTrk if padding
                while (p + 4 <= end && memcmp(p, "MTrk", 4) != 0) p++;
                if (p + 8 > end) break;
            }
            p += 4;
            uint32_t trk_len = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            p += 4;
            const uint8_t *trk_end = p + trk_len;
            if (trk_end > end) trk_end = end;

            uint64_t current_tick = 0;
            uint8_t running_status = 0;

            while (p < trk_end) {
                uint32_t delta = read_vlq(p, trk_end);
                current_tick += delta;
                if (p >= trk_end) break;

                uint8_t status = *p;
                if (status & 0x80) {
                    running_status = status;
                    p++;
                } else {
                    status = running_status;
                }

                if (status == 0xFF) {
                    // Meta event
                    if (p >= trk_end) break;
                    uint8_t meta_type = *p++;
                    uint32_t len = read_vlq(p, trk_end);
                    if (p + len > trk_end) break;
                    if (meta_type == 0x51 && len == 3) {
                        uint32_t us = (p[0] << 16) | (p[1] << 8) | p[2];
                        tempos.push_back({current_tick, us});
                    }
                    p += len;
                } else if (status == 0xF0 || status == 0xF7) {
                    // Sysex
                    uint32_t len = read_vlq(p, trk_end);
                    p += len;
                } else {
                    uint8_t cmd = status & 0xF0;
                    int ch = status & 0x0F;
                    if (cmd == 0x80) {
                        uint8_t note = *p++;
                        uint8_t vel = *p++;
                        raw_events.push_back({current_tick, 2, ch, note, vel});
                    } else if (cmd == 0x90) {
                        uint8_t note = *p++;
                        uint8_t vel = *p++;
                        if (vel == 0) {
                            raw_events.push_back({current_tick, 2, ch, note, 0});
                        } else {
                            raw_events.push_back({current_tick, 1, ch, note, vel});
                        }
                    } else if (cmd == 0xB0) {
                        uint8_t cc = *p++;
                        uint8_t val = *p++;
                        raw_events.push_back({current_tick, 3, ch, cc, val});
                    } else if (cmd == 0xC0) {
                        uint8_t prog = *p++;
                        raw_events.push_back({current_tick, 4, ch, prog, 0});
                    } else if (cmd == 0xA0 || cmd == 0xE0) {
                        p += 2;
                    } else if (cmd == 0xD0) {
                        p += 1;
                    }
                }
            }
            p = trk_end;
        }

        // Sort tempo changes
        std::sort(tempos.begin(), tempos.end(), [](const TempoChange &a, const TempoChange &b) {
            return a.tick < b.tick;
        });

        // Convert ticks to sample time
        auto tick_to_sample = [&](uint64_t tick) -> uint64_t {
            double current_time_sec = 0.0;
            uint64_t prev_tick = 0;
            uint32_t cur_tempo = 500000;

            for (const auto &tc : tempos) {
                if (tick <= tc.tick) break;
                uint64_t dt = tc.tick - prev_tick;
                current_time_sec += (double)dt * (double)cur_tempo / ((double)division * 1000000.0);
                prev_tick = tc.tick;
                cur_tempo = tc.us_per_quarter;
            }
            uint64_t dt = tick - prev_tick;
            current_time_sec += (double)dt * (double)cur_tempo / ((double)division * 1000000.0);
            return (uint64_t)(current_time_sec * sample_rate);
        };

        out_events.clear();
        uint64_t max_sample = 0;
        for (const auto &re : raw_events) {
            uint64_t samp = tick_to_sample(re.tick);
            if (samp > max_sample) max_sample = samp;
            out_events.push_back({samp, re.type, re.channel, re.p1, re.p2});
        }

        std::sort(out_events.begin(), out_events.end(), [](const MidiEventRecord &a, const MidiEventRecord &b) {
            return a.sample_offset < b.sample_offset;
        });

        total_samples = max_sample;
        return true;
    }
};

// Offline Audio Subclass
class Audio_offline : public Audio {
public:
    Audio_offline(const char *name, Lfq_u32 *qnote, Lfq_u32 *qcomm, int fsamp, int fsize)
        : Audio(name, qnote, qcomm) {
        _fsamp = fsamp;
        _fsize = fsize;
        _nplay = 2;
        init_audio();
        for (int i = 0; i < _nplay; i++) _outbuf[i] = new float[fsize];
        _running = true;
    }

    virtual ~Audio_offline() {
        for (int i = 0; i < _nplay; i++) delete[] _outbuf[i];
    }

    void pump_messages() {
        proc_mesg();
    }

    void activate_rank(int divis, int rank) {
        if (divis < _ndivis && _divisp[divis]) {
            _divisp[divis]->set_rank_mask(rank, 128);
        }
    }

    void render_period(float *interleaved_stereo) {
        proc_queue(_qnote);
        proc_queue(_qcomm);
        proc_keys1();
        proc_keys2();
        proc_synth(_fsize);
        proc_mesg();

        for (unsigned int i = 0; i < _fsize; i++) {
            interleaved_stereo[2 * i]     = _outbuf[0][i];
            interleaved_stereo[2 * i + 1] = _outbuf[1][i];
        }
    }

    virtual void thr_main() {}
};

// Offline MIDI Subclass
class Imidi_offline : public Imidi {
public:
    Imidi_offline(Lfq_u32 *qnote, Lfq_u8 *qmidi, uint16_t *midimap, const char *appname)
        : Imidi(qnote, qmidi, midimap, appname) {}
    virtual ~Imidi_offline() {}

    void init() {
        open_midi();
    }

    virtual void on_open_midi() {}
    virtual void on_close_midi() {}
    virtual void on_terminate() {}
    virtual void thr_main() {}

    void send_event_direct(int type, int ch, int p1, int p2) {
        MidiEvent ev;
        if (type == 1) { // NOTEON
            ev.type = SND_SEQ_EVENT_NOTEON;
            ev.note.channel = ch;
            ev.note.note = p1;
            ev.note.velocity = p2;
        } else if (type == 2) { // NOTEOFF
            ev.type = SND_SEQ_EVENT_NOTEOFF;
            ev.note.channel = ch;
            ev.note.note = p1;
            ev.note.velocity = p2;
        } else if (type == 3) { // CONTROLLER
            ev.type = SND_SEQ_EVENT_CONTROLLER;
            ev.control.channel = ch;
            ev.control.param = p1;
            ev.control.value = p2;
        } else if (type == 4) { // PGMCHANGE
            ev.type = SND_SEQ_EVENT_PGMCHANGE;
            ev.control.channel = ch;
            ev.control.param = p1;
            ev.control.value = 0;
        }
        proc_midi_event(ev);
    }
};

// Headless UI Subclass
class Iface_offline : public Iface {
public:
    volatile bool is_ready;

    Iface_offline() : Iface(), is_ready(false) {}
    virtual ~Iface_offline() {}
    virtual void stop() { put_event(EV_EXIT, 1); }

    virtual void thr_main() {
        int E;
        while ((E = get_event()) != EV_EXIT) {
            ITC_mesg *M = get_message();
            if (!M) continue;
            if (M->type() == MT_IFC_READY) {
                is_ready = true;
            }
            M->recover();
        }
    }

    void set_stop(int group, int ifelm, int state) {
        send_event(TO_MODEL, new M_ifc_ifelm(state ? MT_IFC_ELSET : MT_IFC_ELCLR, group, ifelm));
    }

    void load_preset(int bank, int pres) {
        send_event(TO_MODEL, new M_ifc_preset(MT_IFC_PRRCL, bank, pres, 0, nullptr));
    }
};

// Simple WAV File Writer (16-bit stereo PCM)
class WavWriter {
public:
    FILE *f;
    uint32_t total_data_bytes;

    WavWriter() : f(nullptr), total_data_bytes(0) {}
    ~WavWriter() { close(); }

    bool open(const char *path, uint32_t sample_rate) {
        f = fopen(path, "wb");
        if (!f) return false;

        uint8_t header[44] = {
            'R', 'I', 'F', 'F',
            0, 0, 0, 0, // riff size (36 + data)
            'W', 'A', 'V', 'E',
            'f', 'm', 't', ' ',
            16, 0, 0, 0, // subchunk1 size
            1, 0,        // PCM
            2, 0,        // 2 channels
            (uint8_t)(sample_rate & 0xFF),
            (uint8_t)((sample_rate >> 8) & 0xFF),
            (uint8_t)((sample_rate >> 16) & 0xFF),
            (uint8_t)((sample_rate >> 24) & 0xFF),
            (uint8_t)((sample_rate * 4) & 0xFF),
            (uint8_t)(((sample_rate * 4) >> 8) & 0xFF),
            (uint8_t)(((sample_rate * 4) >> 16) & 0xFF),
            (uint8_t)(((sample_rate * 4) >> 24) & 0xFF),
            4, 0,        // block align
            16, 0,       // 16 bits
            'd', 'a', 't', 'a',
            0, 0, 0, 0  // data size
        };
        fwrite(header, 1, 44, f);
        total_data_bytes = 0;
        return true;
    }

    void write_samples(const float *interleaved, size_t count) {
        if (!f) return;
        std::vector<int16_t> pcm(count);
        for (size_t i = 0; i < count; ++i) {
            float s = interleaved[i];
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            pcm[i] = (int16_t)(s * 32767.0f);
        }
        fwrite(pcm.data(), sizeof(int16_t), count, f);
        total_data_bytes += count * sizeof(int16_t);
    }

    void close() {
        if (f) {
            // Update RIFF size and data size
            uint32_t riff_size = 36 + total_data_bytes;
            fseek(f, 4, SEEK_SET);
            fwrite(&riff_size, 4, 1, f);
            fseek(f, 40, SEEK_SET);
            fwrite(&total_data_bytes, 4, 1, f);
            fclose(f);
            f = nullptr;
        }
    }
};

int main(int argc, char *argv[]) {
    const char *stops_dir = "stops";
    const char *instr_name = "Aeolus";
    const char *waves_dir = "waves";
    const char *midi_file = nullptr;
    const char *wav_out = nullptr;
    int sample_rate = 48000;
    float reverb_tail_sec = 3.5f;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-S") && i + 1 < argc) stops_dir = argv[++i];
        else if (!strcmp(argv[i], "-I") && i + 1 < argc) instr_name = argv[++i];
        else if (!strcmp(argv[i], "-W") && i + 1 < argc) waves_dir = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) sample_rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-tail") && i + 1 < argc) reverb_tail_sec = atof(argv[++i]);
        else if (!midi_file) midi_file = argv[i];
        else if (!wav_out) wav_out = argv[i];
    }

    if (!midi_file || !wav_out) {
        std::cerr << "Usage: aeolus_offline [-S stops_dir] [-I instr] [-W waves] <input.mid> <output.wav>" << std::endl;
        return 1;
    }

    std::cout << "========================================================\n"
              << "   Aeolus Pure Mathematical Synth (Direct Offline)\n"
              << "========================================================\n"
              << "MIDI Input:    " << midi_file << "\n"
              << "WAV Output:    " << wav_out << "\n"
              << "Instrument:    " << stops_dir << "/" << instr_name << "\n"
              << "Sample Rate:   " << sample_rate << " Hz\n";

    // 1. Parse MIDI file
    std::vector<MidiEventRecord> midi_events;
    uint64_t total_midi_samples = 0;
    if (!MidiParser::parse(midi_file, sample_rate, midi_events, total_midi_samples)) {
        return 1;
    }
    std::cout << "Loaded MIDI:   " << midi_events.size() << " events, duration: "
              << (double)total_midi_samples / sample_rate << " seconds\n";

    // 2. Initialize Queues and Components
    Lfq_u32 note_queue(1024);
    Lfq_u32 comm_queue(1024);
    Lfq_u8 midi_queue(2048);
    ITC_ctrl itcc;

    const int fsize = PERIOD; // 64 samples per period
    Audio_offline *audio = new Audio_offline("aeolus", &note_queue, &comm_queue, sample_rate, fsize);
    Model *model = new Model(&comm_queue, &midi_queue, audio->midimap(), audio->appname(), stops_dir, instr_name, waves_dir, false);
    Slave *slave = new Slave();
    Imidi_offline *imidi = new Imidi_offline(&note_queue, &midi_queue, audio->midimap(), audio->appname());
    Iface_offline *iface = new Iface_offline();

    // 3. Connect ITC graph
    ITC_ctrl::connect(audio, EV_EXIT,  &itcc, EV_EXIT);
    ITC_ctrl::connect(audio, EV_QMIDI, model, EV_QMIDI);
    ITC_ctrl::connect(audio, TO_MODEL, model, FM_AUDIO);

    ITC_ctrl::connect(imidi, EV_EXIT,  &itcc, EV_EXIT);
    ITC_ctrl::connect(imidi, EV_QMIDI, model, EV_QMIDI);
    ITC_ctrl::connect(imidi, TO_MODEL, model, FM_IMIDI);

    ITC_ctrl::connect(model, EV_EXIT,  &itcc, EV_EXIT);
    ITC_ctrl::connect(model, TO_AUDIO, audio, FM_MODEL);
    ITC_ctrl::connect(model, TO_SLAVE, slave, FM_MODEL);
    ITC_ctrl::connect(model, TO_IFACE, iface, FM_MODEL);

    ITC_ctrl::connect(slave, EV_EXIT,  &itcc, EV_EXIT);
    ITC_ctrl::connect(slave, TO_AUDIO, audio, FM_SLAVE);
    ITC_ctrl::connect(slave, TO_MODEL, model, FM_SLAVE);

    ITC_ctrl::connect(iface, EV_EXIT,  &itcc, EV_EXIT);
    ITC_ctrl::connect(iface, TO_MODEL, model, FM_IFACE);

    // 4. Start Synthesis Subsystems
    audio->start();
    imidi->init();
    model->thr_start(SCHED_OTHER, 0, 0);
    slave->thr_start(SCHED_OTHER, 0, 0);
    iface->thr_start(SCHED_OTHER, 0, 0);

    // 5. Wait for model and rankwaves calculation
    std::cout << "Calculating / loading pipe ranks..." << std::flush;
    auto t_start = std::chrono::steady_clock::now();
    while (!iface->is_ready) {
        audio->pump_messages();
        usleep(1000); // 1 ms
    }
    std::cout << " Done in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count()
              << " ms.\n";

    // Setup direct MIDI mapping:
    // Ch 0 (MIDI 1) -> Manual I (mask = 4)
    // Ch 1 (MIDI 2) -> Manual II (mask = 2)
    // Ch 2 (MIDI 3) -> Manual III (mask = 1)
    // Ch 3 (MIDI 4) -> Pedal (mask = 8)
    uint16_t *mmap = audio->midimap();
    mmap[0] = 4 | 0x4000; // Manual I
    mmap[1] = 2 | 0x4000; // Manual II
    mmap[2] = 1 | 0x4000; // Manual III
    mmap[3] = 8 | 0x4000; // Pedal

    // 6. Activate Pipe Stops for Baroque Organ Tutti
    // Division 2 (Manual I): Principal 8', 4', 2', Cymbel, Mixtur 1-5
    std::cout << "Activating organ stops on synthesis divisions...\n";
    audio->activate_rank(2, 0);  // Principal 8'
    audio->activate_rank(2, 1);  // Principal 4'
    audio->activate_rank(2, 2);  // Octave 2'
    audio->activate_rank(2, 11); // Cymbel
    audio->activate_rank(2, 12); // Mixtur 1
    audio->activate_rank(2, 13); // Mixtur 2
    audio->activate_rank(2, 14); // Mixtur 3
    audio->activate_rank(2, 15); // Mixtur 4
    audio->activate_rank(2, 16); // Mixtur 5

    // Division 3 (Pedal): Bourdon 16', Principal 16', Principal 8', Trombone 16'
    audio->activate_rank(3, 0);  // Bourdon 16'
    audio->activate_rank(3, 1);  // Principal 16'
    audio->activate_rank(3, 2);  // Principal 8'
    audio->activate_rank(3, 9);  // Trombone 16'

    // Flush stop activation into engine
    for (int k = 0; k < 50; ++k) {
        float dummy[128];
        audio->render_period(dummy);
    }

    // 7. Open WAV writer
    WavWriter wav;
    if (!wav.open(wav_out, sample_rate)) {
        std::cerr << "Error: cannot create WAV file: " << wav_out << std::endl;
        return 1;
    }

    // 8. Offline Render Loop
    std::cout << "Rendering offline (faster-than-realtime)...\n";
    auto t_render_start = std::chrono::steady_clock::now();

    uint64_t current_sample = 0;
    uint64_t render_limit = total_midi_samples + (uint64_t)(reverb_tail_sec * sample_rate);
    size_t event_idx = 0;
    float block_buffer[PERIOD * 2];
    float peak_amp = 0.0f;
    int notes_sent = 0;

    while (current_sample < render_limit) {
        // Dispatch all MIDI events due in this period
        while (event_idx < midi_events.size() && midi_events[event_idx].sample_offset <= current_sample) {
            const auto &ev = midi_events[event_idx++];
            imidi->send_event_direct(ev.type, ev.channel, ev.param1, ev.param2);
            if (ev.type == 1) notes_sent++;
        }

        // Compute direct mathematical additive synthesis slice
        audio->render_period(block_buffer);

        for (int i = 0; i < PERIOD * 2; ++i) {
            float a = std::abs(block_buffer[i]);
            if (a > peak_amp) peak_amp = a;
        }

        // Stream stereo samples to WAV
        wav.write_samples(block_buffer, PERIOD * 2);

        current_sample += PERIOD;
    }

    wav.close();
    auto t_render_end = std::chrono::steady_clock::now();
    double render_time_sec = std::chrono::duration<double>(t_render_end - t_render_start).count();
    double audio_duration_sec = (double)current_sample / sample_rate;

    std::cout << "========================================================\n"
              << " Render Complete!\n"
              << " Notes Dispatched: " << notes_sent << "\n"
              << " Peak Amplitude:   " << peak_amp << " (" << (20.0 * log10(peak_amp + 1e-6)) << " dB)\n"
              << " Audio Duration:   " << audio_duration_sec << " s\n"
              << " Render Time:      " << render_time_sec << " s\n"
              << " Speed Factor:     " << (audio_duration_sec / render_time_sec) << "x realtime\n"
              << " Output File:      " << wav_out << "\n"
              << "========================================================\n";

    // 9. Clean shutdown
    iface->stop();
    model->terminate();
    slave->terminate();
    delete audio;
    delete imidi;
    delete model;
    delete slave;
    delete iface;

    return 0;
}

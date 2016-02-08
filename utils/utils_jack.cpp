/*
 * MOD-UI utilities
 * Copyright (C) 2015 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the COPYING file.
 */

#include "utils.h"

#include <cstdio>
#include <cstring>

#include <alsa/asoundlib.h>
#include <jack/jack.h>

#include <algorithm>
#include <mutex>
#include <vector>

#define ALSA_SOUNDCARD_ID  "hw:MOD-Duo"
#define ALSA_CONTROL_LEFT  "Left True-Bypass"
#define ALSA_CONTROL_RIGHT "Right True-Bypass"

// #define ALSA_SOUNDCARD_ID  "hw:PCH"
// #define ALSA_CONTROL_LEFT  "IEC958"
// #define ALSA_CONTROL_RIGHT "IEC958_"

// --------------------------------------------------------------------------------------------------------

static jack_client_t* gClient = nullptr;
static volatile unsigned gXrunCount = 0;
static const char** gPortListRet = nullptr;

static std::mutex gPortRegisterMutex;
static std::vector<jack_port_id_t> gRegisteredPorts;

static snd_mixer_t* gAlsaMixer = nullptr;
static snd_mixer_elem_t* gAlsaControlLeft = nullptr;
static snd_mixer_elem_t* gAlsaControlRight = nullptr;
static bool gLastAlsaValueLeft = false;
static bool gLastAlsaValueRight = false;

static JackMidiPortDeleted jack_midi_port_deleted_cb = nullptr;
static TrueBypassStateChanged true_bypass_changed_cb = nullptr;

// --------------------------------------------------------------------------------------------------------

static bool _get_alsa_switch_value(snd_mixer_elem_t* const elem)
{
    int val = 0;
    snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &val);
    return (val != 0);
}

// --------------------------------------------------------------------------------------------------------

static void JackPortRegistration(jack_port_id_t port, int reg, void*)
{
    if (reg != 0 && jack_midi_port_deleted_cb != nullptr)
        return;

    const std::lock_guard<std::mutex> clg(gPortRegisterMutex);
    gRegisteredPorts.push_back(port);
}

static int JackXRun(void*)
{
    gXrunCount += 1;
    return 0;
}

static void JackShutdown(void*)
{
    gClient = nullptr;
}

// --------------------------------------------------------------------------------------------------------

bool init_jack(void)
{
    if (gAlsaMixer == nullptr)
    {
        if (snd_mixer_open(&gAlsaMixer, SND_MIXER_ELEM_SIMPLE) == 0)
        {
            snd_mixer_selem_id_t* sid;

            if (snd_mixer_attach(gAlsaMixer, ALSA_SOUNDCARD_ID) == 0 &&
                snd_mixer_selem_register(gAlsaMixer, nullptr, nullptr) == 0 &&
                snd_mixer_load(gAlsaMixer) == 0 &&
                snd_mixer_selem_id_malloc(&sid) == 0)
            {
                snd_mixer_selem_id_set_index(sid, 0);
                snd_mixer_selem_id_set_name(sid, ALSA_CONTROL_LEFT);

                if ((gAlsaControlLeft = snd_mixer_find_selem(gAlsaMixer, sid)) != nullptr)
                    gLastAlsaValueLeft = _get_alsa_switch_value(gAlsaControlLeft);

                snd_mixer_selem_id_set_index(sid, 0);
                snd_mixer_selem_id_set_name(sid, ALSA_CONTROL_RIGHT);

                if ((gAlsaControlRight = snd_mixer_find_selem(gAlsaMixer, sid)) != nullptr)
                    gLastAlsaValueRight = _get_alsa_switch_value(gAlsaControlRight);

                snd_mixer_selem_id_free(sid);
            }
            else
            {
                snd_mixer_close(gAlsaMixer);
                gAlsaMixer = nullptr;
            }
        }
    }

    if (gClient != nullptr)
    {
        printf("jack client activated before, nothing to do\n");
        return true;
    }

    const jack_options_t options = static_cast<jack_options_t>(JackNoStartServer|JackUseExactName);
    jack_client_t* const client = jack_client_open("mod-ui", options, nullptr);

    if (client == nullptr)
        return false;

    jack_set_port_registration_callback(client, JackPortRegistration, nullptr);
    jack_set_xrun_callback(client, JackXRun, nullptr);
    jack_on_shutdown(client, JackShutdown, nullptr);

    gClient = client;
    gXrunCount = 0;
    jack_activate(client);

    printf("jack client activated\n");
    return true;
}

void close_jack(void)
{
    if (gPortListRet != nullptr)
    {
        jack_free(gPortListRet);
        gPortListRet = nullptr;
    }

    if (gAlsaMixer != nullptr)
    {
        gAlsaControlLeft = nullptr;
        gAlsaControlRight = nullptr;
        snd_mixer_close(gAlsaMixer);
        gAlsaMixer = nullptr;
    }

    if (gClient == nullptr)
    {
        printf("jack client deactivated NOT\n");
        return;
    }

    jack_client_t* const client = gClient;
    gClient = nullptr;

    jack_deactivate(client);
    jack_client_close(client);

    printf("jack client deactivated\n");
}

// --------------------------------------------------------------------------------------------------------

JackData* get_jack_data(void)
{
    static JackData data;
    static std::vector<jack_port_id_t> localPorts;

    static char  aliases[0xff][2];
    static char* aliasesptr[2] = {
        aliases[0],
        aliases[1]
    };
    static const char* nc = "";

    if (gClient != nullptr)
    {
        data.cpuLoad = jack_cpu_load(gClient);
        data.xruns   = gXrunCount;

        // See if any new ports have been registered
        {
            const std::lock_guard<std::mutex> clg(gPortRegisterMutex);

            if (gRegisteredPorts.size() > 0)
                gRegisteredPorts.swap(localPorts);
        }

        for (const jack_port_id_t& port_id : localPorts)
        {
            const jack_port_t* const port(jack_port_by_id(gClient, port_id));

            if (port == nullptr)
                continue;
            if ((jack_port_flags(port) & JackPortIsPhysical) == 0)
                continue;
            if (std::strcmp(jack_port_type(port), JACK_DEFAULT_MIDI_TYPE))
                continue;

            const char* const portName = jack_port_name(port);
            const char* portAlias;

            if (jack_port_get_aliases(port, aliasesptr) > 0)
                portAlias = aliases[0];
            else
                portAlias = nc;

            jack_midi_port_deleted_cb(portName, portAlias);
        }

        localPorts.clear();
    }
    else
    {
        data.cpuLoad = 0.0f;
        data.xruns   = 0;
    }

    if (gAlsaMixer != nullptr && true_bypass_changed_cb != nullptr)
    {
        bool changed = false;
        snd_mixer_handle_events(gAlsaMixer);

        if (gAlsaControlLeft != nullptr)
        {
            const bool newValue = _get_alsa_switch_value(gAlsaControlLeft);

            if (gLastAlsaValueLeft != newValue)
            {
                changed = true;
                gLastAlsaValueLeft = newValue;
            }
        }

        if (gAlsaControlRight != nullptr)
        {
            const bool newValue = _get_alsa_switch_value(gAlsaControlRight);

            if (gLastAlsaValueRight != newValue)
            {
                changed = true;
                gLastAlsaValueRight = newValue;
            }
        }

        if (changed)
            true_bypass_changed_cb(gLastAlsaValueLeft, gLastAlsaValueRight);
    }

    return &data;
}

float get_jack_sample_rate(void)
{
    if (gClient == nullptr)
        return 48000.0f;

    return jack_get_sample_rate(gClient);
}

const char* get_jack_port_alias(const char* portname)
{
    static char  aliases[0xff][2];
    static char* aliasesptr[2] = {
        aliases[0],
        aliases[1]
    };

    if (gClient != nullptr && jack_port_get_aliases(jack_port_by_name(gClient, portname), aliasesptr) > 0)
        return aliases[0];

    return nullptr;
}

const char* const* get_jack_hardware_ports(const bool isAudio, bool isOutput)
{
    if (gPortListRet != nullptr)
    {
        jack_free(gPortListRet);
        gPortListRet = nullptr;
    }

    if (gClient == nullptr)
        return nullptr;

    const unsigned long flags = JackPortIsPhysical | (isOutput ? JackPortIsInput : JackPortIsOutput);
    const char* const type    = isAudio ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE;
    const char** const ports  = jack_get_ports(gClient, "system:", type, flags);

    if (ports == nullptr)
        return nullptr;

    gPortListRet = ports;

    return ports;
}

// --------------------------------------------------------------------------------------------------------

bool has_serial_midi_input_port(void)
{
    if (gClient == nullptr)
        return false;

    return (jack_port_by_name(gClient, "ttymidi:MIDI_in") != nullptr);
}

bool has_serial_midi_output_port(void)
{
    if (gClient == nullptr)
        return false;

    return (jack_port_by_name(gClient, "ttymidi:MIDI_out") != nullptr);
}

// --------------------------------------------------------------------------------------------------------

void connect_jack_ports(const char* port1, const char* port2)
{
    if (gClient != nullptr)
        jack_connect(gClient, port1, port2);
}

void disconnect_jack_ports(const char* port1, const char* port2)
{
    if (gClient != nullptr)
        jack_disconnect(gClient, port1, port2);
}

// --------------------------------------------------------------------------------------------------------

bool get_truebypass_value(bool right)
{
    return right ? gLastAlsaValueRight : gLastAlsaValueLeft;
}

bool set_truebypass_value(bool right, bool bypassed)
{
    if (gAlsaMixer == nullptr)
        return false;

    if (right)
    {
        if (gAlsaControlRight != nullptr)
            return (snd_mixer_selem_set_playback_switch_all(gAlsaControlRight, bypassed) == 0);
    }
    else
    {
        if (gAlsaControlLeft != nullptr)
            return (snd_mixer_selem_set_playback_switch_all(gAlsaControlLeft, bypassed) == 0);
    }

    return false;
}

// --------------------------------------------------------------------------------------------------------

void set_util_callbacks(JackMidiPortDeleted midiPortDeleted, TrueBypassStateChanged trueBypassChanged)
{
    jack_midi_port_deleted_cb = midiPortDeleted;
    true_bypass_changed_cb    = trueBypassChanged;
}

// --------------------------------------------------------------------------------------------------------

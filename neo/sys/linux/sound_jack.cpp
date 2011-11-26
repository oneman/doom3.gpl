/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include "../../idlib/precompiled.h"
#include "../../sound/snd_local.h"
#include "../posix/posix_public.h"
#include "sound.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>

int jack_process_callback (jack_nframes_t nframes, void *arg);

/*
===============
idAudioHardwareJACK::DLOpen
===============
*/
bool idAudioHardwareJACK::Running( void ) {

	if (m_jack_client == NULL) {
		common->Printf( "Jack open with %s %s\n", m_jack_client_name, m_jack_server_name );
		m_jack_client = jack_client_open (m_jack_client_name, JackNoStartServer, &m_jack_status);
	}

	if (m_jack_client == NULL) {
		common->Printf( "jack_client_open() failed. status = 0x%2.0x\n", m_jack_status );
		InitFailed();
		return false;
	}

	return true;
}

/*
===============
idAudioHardwareJACK::Release
===============
*/
void idAudioHardwareJACK::Release() {

	if ( m_jack_client != NULL) {
		jack_client_close (m_jack_client);
		m_jack_client = NULL;
	}
	
	if ( m_jack_ringbuffer[0] != NULL) {
		jack_ringbuffer_free ( m_jack_ringbuffer[0] );
		m_jack_ringbuffer[0] = NULL;
	}

	if ( m_jack_ringbuffer[1] != NULL) {
		jack_ringbuffer_free ( m_jack_ringbuffer[1] );
		m_jack_ringbuffer[1] = NULL;
	}

	if ( m_buffer ) {
		free( m_buffer );
		m_buffer = NULL;
	}

}

/*
=================
idAudioHardwareJACK::InitFailed
=================	
*/	
void idAudioHardwareJACK::InitFailed() {
	Release();
	cvarSystem->SetCVarBool( "s_noSound", true );
	common->Warning( "sound subsystem disabled\n" );
	common->Printf( "--------------------------------------\n" );
}

/*
=====================
idAudioHardwareJACK::Initialize
=====================
*/
bool idAudioHardwareJACK::Initialize( void ) {
	
	
	common->Printf( "------ Jack Sound Initialization -----\n" );

	if (m_jack_client == NULL) {
		m_jack_client = jack_client_open (m_jack_client_name, m_jack_options, &m_jack_status, m_jack_server_name);
	}

	if (m_jack_client == NULL) {
		common->Printf( "jack_client_open() failed, status = 0x%2.0x\n", m_jack_status );
		InitFailed();
		return false;
	}


	common->Printf( "------ Jack Client Opened -----\n" );

	if (m_jack_status & JackNameNotUnique) {
		m_jack_client_name = jack_get_client_name(m_jack_client);
		common->Printf( "Jack unique name `%s' assigned\n", m_jack_client_name);
	}

	// channels

	// sanity over number of speakers
	if ( idSoundSystemLocal::s_numberOfSpeakers.GetInteger() != 6 && idSoundSystemLocal::s_numberOfSpeakers.GetInteger() != 2 ) {
		common->Warning( "invalid value for s_numberOfSpeakers. Use either 2 or 6" );
		idSoundSystemLocal::s_numberOfSpeakers.SetInteger( 2 );
	}

	
	// temp kludge
	common->Printf( "Kludge Jack setting speakers to 2\n");
	idSoundSystemLocal::s_numberOfSpeakers.SetInteger( 2 );

	m_channels = idSoundSystemLocal::s_numberOfSpeakers.GetInteger();

	// set sample rate (frequency)
	
	if (PRIMARYFREQ != jack_get_sample_rate(m_jack_client)) {
		common->Printf( "Jack is not running at a %d sample rate\n", PRIMARYFREQ);
		InitFailed();
		return false;
	}

	// have enough space in the input buffer for our MIXBUFFER_SAMPLE feedings and async ticks
	//int frames;
	//frames = MIXBUFFER_SAMPLES + MIXBUFFER_SAMPLES / 3;


	// check the buffer size

	//common->Printf( "snd_pcm_hw_params_get_buffer_size failed: %s\n", id_snd_strerror( err ) );

	common->Printf( "Mixbuffer samples define is %d\n", MIXBUFFER_SAMPLES );


	// allocate the final mix buffer
	m_buffer_size = MIXBUFFER_SAMPLES * m_channels * 2 * 3;
	m_buffer = malloc( m_buffer_size );
	common->Printf( "allocated a mix buffer of %d bytes\n", m_buffer_size );

	// Set up Callbacks

	jack_set_process_callback (m_jack_client, jack_process_callback, this);
	//jack_on_shutdown (kradmixer->jack_client, jack_shutdown, kradmixer->userdata);
	//jack_set_xrun_callback (kradmixer->jack_client, xrun_callback, kradmixer->userdata);
	
	// Create Ports
	
	m_jack_ports[0] = jack_port_register (m_jack_client, "Left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
																				  
	if (m_jack_ports[0] == NULL) {
		common->Printf( "Could not register Jack port\n" );
		InitFailed();
		return false;
	}
	
	m_jack_ports[1] = jack_port_register (m_jack_client, "Right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	
	if (m_jack_ports[1] == NULL) {
		common->Printf( "Could not register Jack port\n" );
		InitFailed();
		return false;
	}
	
	m_jack_ringbuffer[0] = jack_ringbuffer_create (1800000);
	
	if (m_jack_ringbuffer[0] == NULL) {
		common->Printf( "Could not create jack ringbuffer\n" );
		InitFailed();
		return false;
	}
	
	m_jack_ringbuffer[1] = jack_ringbuffer_create (1800000);
	
	if (m_jack_ringbuffer[1] == NULL) {
		common->Printf( "Could not create jack ringbuffer\n" );
		InitFailed();
		return false;

	
	}
	
	// Activate

	if (jack_activate (m_jack_client)) {
		common->Printf( "cannot activate Jack client" );
		InitFailed();
		return false;
	}
	
	// kludge connect to default first two hardware ports
	
	char *src_port;
	char **remote_ports;
	
	remote_ports = jack_get_ports (m_jack_client, NULL, NULL, JackPortIsInput | JackPortIsPhysical);
	
	src_port = jack_port_name (m_jack_ports[0]);
	jack_connect (m_jack_client, src_port, remote_ports[0]);
	
	src_port = jack_port_name (m_jack_ports[1]);
	jack_connect (m_jack_client, src_port, remote_ports[1]);

	common->Printf( "--------------------------------------\n" );
	return true;
}

/*
===============
idAudioHardwareJACK::~idAudioHardwareJACK
===============
*/
idAudioHardwareJACK::~idAudioHardwareJACK() {
	common->Printf( "----------- Jack Shutdown ------------\n" );
	Release();
	common->Printf( "--------------------------------------\n" );
}

/*
=================
idAudioHardwareJACK::GetMixBufferSize
=================
*/	
int idAudioHardwareJACK::GetMixBufferSize() {
	return m_buffer_size;
}

/*
=================
idAudioHardwareJACK::GetMixBuffer
=================
*/	
short* idAudioHardwareJACK::GetMixBuffer() {
	return (short *)m_buffer;
}

/*
===============
idAudioHardwareJACK::Flush
===============
*/
bool idAudioHardwareJACK::Flush( void ) {
	//common->Printf( "got flushed\n" );
}

/*
===============
idAudioHardwareJACK::Write
rely on m_freeWriteChunks which has been set in Flush() before engine did the mixing for this MIXBUFFER_SAMPLE
===============
*/
void idAudioHardwareJACK::Write( bool flushing ) {

	//common->Printf( "got Write\n" );

	//m_remainingFrames = MIXBUFFER_SAMPLES;
	//int pos = (int)m_buffer + ( MIXBUFFER_SAMPLES - m_remainingFrames ) * m_channels * 2;
	//snd_pcm_sframes_t frames = id_snd_pcm_writei( m_pcm_handle, (void*)pos, m_remainingFrames );

	int *int_buffer = (int)m_buffer;

	int len = MIXBUFFER_SAMPLES * m_channels;

	while (len)
	{	

		len--;
		m_converted_samples1[len] = (float) ((int_buffer [len] << 16) / (8.0 * 0x10000000));

		len--;
		m_converted_samples2[len] = (float) ((int_buffer [len] << 16) / (8.0 * 0x10000000));

	}

	jack_ringbuffer_write(m_jack_ringbuffer[0], (void *)m_converted_samples1, MIXBUFFER_SAMPLES * 4);
	jack_ringbuffer_write(m_jack_ringbuffer[1], (void *)m_converted_samples2, MIXBUFFER_SAMPLES * 4);

}


int jack_process_callback (jack_nframes_t nframes, void *arg)
{

	idAudioHardwareJACK *id_audio = (idAudioHardwareJACK *)arg;

	int s;

	id_audio->m_samples[0] = jack_port_get_buffer (id_audio->m_jack_ports[0], nframes);
	id_audio->m_samples[1] = jack_port_get_buffer (id_audio->m_jack_ports[1], nframes);
	
	if (jack_ringbuffer_read_space(id_audio->m_jack_ringbuffer[1]) >= nframes * 4) {
	
		jack_ringbuffer_read(id_audio->m_jack_ringbuffer[0], (void *)id_audio->m_samples[0], nframes * 4);
		jack_ringbuffer_read(id_audio->m_jack_ringbuffer[1], (void *)id_audio->m_samples[1], nframes * 4);
	
	} else {
	
		for (s = 0; s < nframes; s++) {
			id_audio->m_samples[0][s] = 0.0f;
			id_audio->m_samples[1][s] = 0.0f;
		}
	
	}
	

	return 0;

}


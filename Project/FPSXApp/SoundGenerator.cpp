#include "SoundGenerator.h"

SoundGenerator::SoundGenerator()
{
	nextSamples_lock = SDL_CreateMutex();

    SDL_AudioSpec wanted;
	wanted.callback = fill_audio;
	wanted.userdata = this;
	wanted.freq = 44100;
	wanted.format = AUDIO_S16;
	wanted.channels = 2;
	wanted.samples = 4096;

	SDL_OpenAudio(&wanted, NULL);
	SDL_PauseAudio(0);
}

void SoundGenerator::fill(Int16 value)
{
	if (SDL_LockMutex(nextSamples_lock) == 0)
	{
		if (nextSamples.size() < 10000000)
		{
			nextSamples.push(value);
		}
		SDL_UnlockMutex(nextSamples_lock);
	}
}

void SoundGenerator::play(bool pause)
{
	SDL_PauseAudio(pause);
}

void fill_audio(void* udata, Uint8* stream, int len)
{
	SoundGenerator* soundGenerator = (SoundGenerator*)udata;

	if (SDL_LockMutex(soundGenerator->nextSamples_lock) == 0)
	{
		for (int n = 0; n < len; n += 4)
		{
			if (soundGenerator->nextSamples.size() > 1)
			{
				Int16 valueInt = soundGenerator->nextSamples.front();
				soundGenerator->nextSamples.pop();
				// left
				stream[n] = (byte)(valueInt & 0xFF);
				stream[n + 1] = (byte)(valueInt >> 8);
				// right
				valueInt = soundGenerator->nextSamples.front();
				soundGenerator->nextSamples.pop();
				stream[n + 2] = (byte)(valueInt & 0xFF);
				stream[n + 3] = (byte)(valueInt >> 8);
			}
			else
			{
				stream[n] = 0;
				stream[n + 1] = 0;
				stream[n + 2] = 0;
				stream[n + 3] = 0;
			}
		}
		SDL_UnlockMutex(soundGenerator->nextSamples_lock);
	}
}
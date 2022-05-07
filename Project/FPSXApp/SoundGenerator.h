#include <queue>
using namespace std;

#include "types.h"
#include "SDL.h"

class SoundGenerator
{
public:
	std::queue<Int16> nextSamples;
	SDL_mutex* nextSamples_lock;

	SoundGenerator();
	void fill(Int16 value);
	void play(bool pause);

	static Uint8* audio_chunk;
	static Uint32 audio_len;
	static Uint8* audio_pos;
};

extern void fill_audio(void* udata, Uint8* stream, int len);

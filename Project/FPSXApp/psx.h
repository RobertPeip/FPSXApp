#pragma once
#include <string>
using namespace std;
#include "SDL.h"

#include "types.h"

class Psx
{
public:
	SDL_mutex* psxlock;
	bool on = false;
	bool pause = false;
	bool coldreset = false;
	bool debugsavestate = false;

	bool is_color = false;
	bool is_crystal = false;
	bool vertical = false;

	string filename;

	string statefilename;
	bool do_savestate = false;
	bool do_loadstate = false;
	bool loading_state;
	UInt32 savestate[1048576];
	UInt32 savestate2[1048576];

	void reset();
	void run();
	void savestate_addvalue(int index, int bitend, int bitstart, UInt32 value);
	UInt32 savestate_loadvalue(int index, int bitend, int bitstart);
	void create_savestate();
	void load_savestate();
	void log(string text);
};
extern Psx psx;

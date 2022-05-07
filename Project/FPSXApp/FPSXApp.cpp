#include <iostream>
#include <algorithm>
#include "SDL.h"
using namespace std;

//#define CONSOLE

#include <windows.h>

#include "psx.h"
#include "DMA.h"
#include "GPU.h"
#include "Joypad.h"
#include "CPU.h"
#include "FileIO.h"
#include "Memory.h"
#include "OSD.h"
#include "CDROM.h"

const int WIDTH = 1024;
const int HEIGHT = 512;

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* framebuffer;
SDL_mutex* mutex;

SDL_Thread* psxthread;

int emuframes;

bool speedunlock = false;

bool autotest = false;
int autoframes = 0;
int autoImageFreezed = 0;
bool autoImageNonBlack = false;
UInt64 autoOldHash;
UInt32 randState = 777;
char fastRand()
{
	randState = randState * 1664525 + 1013904223;
	return randState >> 24;
}

int emu(void* ptr)
{
	psx.run();
	return 0;
}

void savegame()
{
	Memory.save_gameram(psx.filename);
}

void loadgame()
{
	Memory.load_gameram(psx.filename);
}

void savestate()
{
	psx.pause = false;
	psx.do_savestate = true;

	while (psx.do_savestate)
	{
		SDL_Delay(1);
	}
}

void loadstate()
{
	psx.pause = false;
	psx.do_loadstate = true;

	while (psx.do_loadstate && psx.on)
	{
		SDL_Delay(1);
	}
}

void loadstate_fromdisk(string filename)
{
	psx.pause = false;
	FileIO.readfile(psx.savestate, filename, true);

	FileIO.readfile(psx.savestate2, "a.sst", true);
	
	UInt32 value1 = psx.savestate[131072 + 1688];
	UInt32 value2 = psx.savestate2[131072 + 1688];
	

	// all
	//for (int i = 0; i < 1048576; i++)
	//{
	//	psx.savestate[i] = psx.savestate2[i];
	//}

	// soundram
	//for (int i = 0; i < 131072; i++)
	//for (int i = 1688; i < 1689; i++)
	//{
	//	psx.savestate[131072 + i] = psx.savestate2[131072 + i];
	//}

	// ram
	//for (int i = 0; i < 524288; i++)
	//{
	//	psx.savestate[524288 + i] = psx.savestate2[524288 + i];
	//}

	loadstate();
}

void loadstate_fromdisk_auto(string filename)
{
	psx.pause = false;
	string savefilename = filename.substr(filename.find_last_of("/\\") + 1);
	savefilename = savefilename.substr(0, savefilename.find_last_of(".") + 1) + "sst";
	FileIO.readfile(psx.savestate, savefilename, false);
	loadstate();
}

void savestate_todisk(string filename)
{
	psx.pause = false;
	string savefilename = filename.substr(filename.find_last_of("/\\") + 1);
	savefilename = savefilename.substr(0, savefilename.find_last_of(".") + 1) + "sst";
	FileIO.writefile(psx.savestate, savefilename, 1048576 * 4,false);
}

void openrom()
{
	if (psx.filename != "")
	{
		OSD.isOpen = false;
		if (psxthread != 0)
		{
			if (SDL_LockMutex(psx.psxlock) == 0)
			{
				psx.on = false;
				SDL_UnlockMutex(psx.psxlock);
			}
			int threadReturnValue;
			SDL_WaitThread(psxthread, &threadReturnValue);
		}
		psx.coldreset = true;
		emuframes = 0;
		psxthread = SDL_CreateThread(emu, "emuthread", (void*)NULL);
		while (true)
		{
			if (SDL_LockMutex(psx.psxlock) == 0)
			{
				bool ret = psx.on;
				SDL_UnlockMutex(psx.psxlock);
				if (ret) return;
			}
		}
	}
}

void set_displaysize(int mult, bool fullscreen)
{
	if (fullscreen)
	{
		OSD.displaysize = 0;
		SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	}
	else
	{
		OSD.displaysize = mult;
		SDL_SetWindowFullscreen(window, 0);
		SDL_SetWindowSize(window, WIDTH * mult, HEIGHT * mult);
	}
}

void drawer()
{
	window = SDL_CreateWindow ("FPSXApp", 200, 200,WIDTH * 2, HEIGHT * 2, SDL_WINDOW_OPENGL);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
	SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
	SDL_RenderSetIntegerScale(renderer, SDL_bool::SDL_TRUE);
	mutex = SDL_CreateMutex();
	const Uint8* keystate = SDL_GetKeyboardState(NULL);

	// gamepad
	SDL_GameController* controller = NULL;
	for (int i = 0; i < SDL_NumJoysticks(); ++i) {
		if (SDL_IsGameController(i)) {
			controller = SDL_GameControllerOpen(i);
			if (controller) 
			{
				break;
			}
		}
	}

	// drawer
	Uint64 currentTime = SDL_GetPerformanceCounter();
	Uint64 lastTime_frame = SDL_GetPerformanceCounter();
	Uint64 lastTime_second = SDL_GetPerformanceCounter();
	Uint64 lastTime_idle = SDL_GetPerformanceCounter();
	double delta = 0;

	unsigned short frames = 0;

	int OSDidlecheck = 0;

#ifdef CONSOLE
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
#endif

	const long frametime = (1000000 / 60);

	UInt64 oldcycles = 0;
	UInt64 oldcommands = 0;
	UInt64 oldpixels = 0;

	bool running = true;
	while (running || autotest)
	{
		currentTime = SDL_GetPerformanceCounter();

		double frametimeleft = frametime;

		delta = (double)((currentTime - lastTime_frame) * 1000000 / (double)SDL_GetPerformanceFrequency());
		if (delta >= frametimeleft)
		{
			frametimeleft = max(5000, frametimeleft + frametime - delta);
			lastTime_frame = SDL_GetPerformanceCounter();

			if (OSD.isOpen)
			{
				OSD.render(renderer);
			}
			else
			{
				GPU.draw_game();
				SDL_UpdateTexture(framebuffer, NULL, GPU.buffer, WIDTH * sizeof(uint32_t));
				SDL_RenderClear(renderer);
				SDL_RenderCopy(renderer, framebuffer, NULL, NULL);
			}

			SDL_RenderPresent(renderer);
			SDL_PumpEvents();

			frames++;

			if (OSD.isOpen)
			{
				psx.pause = true;
				if (keystate[SDL_SCANCODE_ESCAPE] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK))
				{
					if (OSD.idle)
					{
						OSD.isOpen = false;
						OSD.idle = false;
					}
				}
				else if (keystate[SDL_SCANCODE_UP] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))
				{
					OSD.selectPrev();
				}
				else if (keystate[SDL_SCANCODE_DOWN] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
				{
					OSD.selectNext();
				}
				else if (keystate[SDL_SCANCODE_LEFT] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
				{
					OSD.largePrev();
				}
				else if (keystate[SDL_SCANCODE_RIGHT] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
				{
					OSD.largeNext();
				}
				else if (keystate[SDL_SCANCODE_A] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A))
				{
					if (OSD.idle)
					{
						OSD.idle = false;
						if (OSD.OsdType == OSDTYPE::MAIN)
						{
							switch ((OSDMAINMENU)OSD.selected)
							{
							case OSDMAINMENU::LOADGAME: OSD.gotoLoadGame(); break;
							case OSDMAINMENU::SAVESTATE: savestate(); OSD.isOpen = false; break;
							case OSDMAINMENU::SAVEMEMDISK: savegame(); OSD.isOpen = false; break;
							case OSDMAINMENU::SAVESTATEDISK: savestate_todisk(psx.filename); OSD.isOpen = false; break;
							case OSDMAINMENU::LOADSTATE: loadstate(); OSD.isOpen = false; break;
							case OSDMAINMENU::LOADSTATEDISK: loadstate_fromdisk_auto(psx.filename); OSD.isOpen = false; break;
							case OSDMAINMENU::DISPLAYSIZE:
								OSD.displaysize += 1;
								if (OSD.displaysize > 6)
									OSD.displaysize = 0;
								if (OSD.displaysize == 0)
								{
									set_displaysize(0, true);
									OSD.exchangeText((int)OSDMAINMENU::DISPLAYSIZE, "Displaysize: Fullscreen");
								}
								else
								{
									set_displaysize(OSD.displaysize, false);
									OSD.exchangeText((int)OSDMAINMENU::DISPLAYSIZE, "Displaysize: " + std::to_string(OSD.displaysize));
								}
								break;
							case OSDMAINMENU::CPUSTEPS:
								CPU.additional_steps += 1;
								if (CPU.additional_steps > 10)
									CPU.additional_steps = -10;
								OSD.exchangeText((int)OSDMAINMENU::CPUSTEPS, "CPU Steps: " + std::to_string(CPU.additional_steps));
								break;
							case OSDMAINMENU::EXIT: running = false; break;
							}
						}
						else if (OSD.OsdType == OSDTYPE::LOAD)
						{
							if (OSD.selectFile())
							{
								openrom();
								OSD.isOpen = false;
							}
						}
					}
				}
				else if (keystate[SDL_SCANCODE_S] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X))
				{
					if (OSD.idle)
					{
						OSD.idle = false;
						if (OSD.OsdType == OSDTYPE::MAIN)
						{
							OSD.isOpen = false;
						}
						else if (OSD.OsdType == OSDTYPE::LOAD)
						{
							OSD.gotoMain();
						}
					}
				}
				else
				{
					OSD.idle = true;
				}
			}
			else
			{
				psx.pause = false;

				if (!Joypad.blockbuttons)
				{
					Joypad.KeySquare = keystate[SDL_SCANCODE_A] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A);
					Joypad.KeyCross = keystate[SDL_SCANCODE_S] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X);
					Joypad.KeyCircle = keystate[SDL_SCANCODE_D];
					Joypad.KeyTriangle = keystate[SDL_SCANCODE_W];
					Joypad.KeyL1 = keystate[SDL_SCANCODE_Q] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
					Joypad.KeyR1 = keystate[SDL_SCANCODE_E] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
					Joypad.KeyL2 = keystate[SDL_SCANCODE_TAB] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK);
					Joypad.KeyR2 = keystate[SDL_SCANCODE_R] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
					Joypad.KeyStart = keystate[SDL_SCANCODE_X] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
					Joypad.KeySelect = keystate[SDL_SCANCODE_Y] | keystate[SDL_SCANCODE_Z] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
					Joypad.KeyLeft = keystate[SDL_SCANCODE_LEFT] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
					Joypad.KeyRight = keystate[SDL_SCANCODE_RIGHT] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
					Joypad.KeyUp = keystate[SDL_SCANCODE_UP] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
					Joypad.KeyDown = keystate[SDL_SCANCODE_DOWN] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
				}

				if (autotest)
				{
					Joypad.KeyStart = false;
					Joypad.KeySelect = false;
					Joypad.KeySquare = false;
					Joypad.KeyCross = false;
					Joypad.KeyCircle = false;
					Joypad.KeyTriangle = false;
					int autojoypos = fastRand() % 6;
					switch (autojoypos)
					{
					case 0: Joypad.KeyStart    = true; break;
					case 1: Joypad.KeySelect   = true; break;
					case 2: Joypad.KeySquare   = true; break;
					case 3: Joypad.KeyCross    = true; break;
					case 4: Joypad.KeyCircle   = true; break;
					case 5: Joypad.KeyTriangle = true; break;
					}
					autoframes++;
					if (autoOldHash == GPU.hash) autoImageFreezed++;
					else autoImageFreezed = 0;
					if (GPU.hash != 0) autoImageNonBlack = true;
					if (autoframes > 10000 || autoImageNonBlack)
					//if (autoframes > 1000 || autoImageNonBlack)
					//if (autoframes > 3000 || autoImageFreezed >= 300)
					{
						FILE* file = fopen("R:\\test.txt", "a");
						if (!autoImageNonBlack) fprintf(file, "BLACK ");
						else if (autoImageFreezed >= 300) fprintf(file, "HANG "); 
						else fprintf(file, "PASS ");
						fprintf(file, "%04X ", autoframes);
						fprintf(file, "%s", OSD.selectedItem.c_str());
						fputs("\n", file);
						fclose(file);
						OSD.selected++;
						OSD.selectFile();
						openrom();
						autoframes = 0;
						autoImageFreezed = 0;
						autoImageNonBlack = false;
						randState = 777;
					}
					autoOldHash = GPU.hash;
				}

				if (keystate[SDL_SCANCODE_ESCAPE] | SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK))
				{
					if (OSD.idle)
					{
						OSD.gotoMain();
						OSD.isOpen = true;
						OSD.idle = false;
					}
				}
				else
				{
					OSD.idle = true;
				}

				if (keystate[SDL_SCANCODE_F5])
				{
					savestate();
				}
				if (keystate[SDL_SCANCODE_F7])
				{
					savegame();
				}
				if (keystate[SDL_SCANCODE_F8])
				{
					loadgame();
				}
				if (keystate[SDL_SCANCODE_F6])
				{
					savestate_todisk(psx.filename);
				}
				if (keystate[SDL_SCANCODE_F9])
				{
					loadstate();
				}
				if (keystate[SDL_SCANCODE_F10])
				{
					loadstate_fromdisk_auto(psx.filename);
				}

				if (keystate[SDL_SCANCODE_SPACE] || keystate[SDL_SCANCODE_0] || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y))
				{
					GPU.lockSpeed = false;
					GPU.frameskip = 3;
					speedunlock = keystate[SDL_SCANCODE_0];
				}
				else if (!speedunlock && !keystate[SDL_SCANCODE_SPACE])
				{
					GPU.lockSpeed = true;
					GPU.frameskip = 0;
				}
				else if (speedunlock && keystate[SDL_SCANCODE_SPACE])
				{
					speedunlock = false;
				}
			}

			if (keystate[SDL_SCANCODE_1]) { set_displaysize(1, false); }
			if (keystate[SDL_SCANCODE_2]) { set_displaysize(2, false); }
			if (keystate[SDL_SCANCODE_3]) { set_displaysize(3, false); }
			if (keystate[SDL_SCANCODE_4]) { set_displaysize(4, false); }
			if (keystate[SDL_SCANCODE_5]) { set_displaysize(5, false); }
			if (keystate[SDL_SCANCODE_6]) { set_displaysize(6, false); }
			if (keystate[SDL_SCANCODE_RETURN] && keystate[SDL_SCANCODE_LALT]) { set_displaysize(0, true); }
		}

		delta = (double)((currentTime - lastTime_idle) * 1000000 / (double)SDL_GetPerformanceFrequency());
		if (delta >= 10000)
		{
			if (!OSD.idle)
			{
				OSDidlecheck--;
				if (OSDidlecheck <= 0)
				{
					OSD.idle = true;
					OSDidlecheck = 20;
				}
			}
			else
			{
				OSDidlecheck = 50;
			}
			lastTime_idle = SDL_GetPerformanceCounter();
		}

		delta = (double)((currentTime - lastTime_second) * 1000000 / (double)SDL_GetPerformanceFrequency());
		if (delta >= 1000000)
		{
			UInt64 cpucycles = (UInt64)CPU.totalticks;
			double newcycles = (double)(cpucycles - oldcycles);
#ifdef CONSOLE
			std::cout << "CPU%: " << (int)(100 * newcycles / 16780000);
			std::cout << " | FPS: " << frames;
			std::cout << " | Intern FPS: " << GPU.intern_frames;
			std::cout << "(" << GPU.videomode_frames << ")";
			std::cout << " | AVG Cycles: " << (newcycles / (CPU.commands - oldcommands));
			std::cout << " | unknown OPCode: " << CPU.unknownOpCode << ";" << "\n";
#endif
			if (autotest)
			{
				//SDL_SetWindowTitle(window, std::to_string(autoImageFreezed).c_str());
				SDL_SetWindowTitle(window, OSD.selectedItem.c_str());
			}
			else
			{
				std::string debugstring = "Speed: ";
				debugstring.append(std::to_string(100 * newcycles / 33870000));
				debugstring.append("%, Pixels: ");
				debugstring.append(std::to_string(GPU.pixeldrawn - oldpixels));
				debugstring.append(", CMD: ");
				debugstring.append(std::to_string(psx.savestate[2]));
				debugstring.append(", GFIFO: ");
				debugstring.append(std::to_string(GPU.fifo.size()));
				debugstring.append(", CD LBA: ");
				debugstring.append(std::to_string(CDRom.currentLBA));

				SDL_SetWindowTitle(window, debugstring.c_str());
			}

			lastTime_second = SDL_GetPerformanceCounter();
			frames = 0;
			if (SDL_LockMutex(mutex) == 0)
			{
				emuframes = 0;
				SDL_UnlockMutex(mutex);
			}
			if (SDL_LockMutex(GPU.drawlock) == 0)
			{
				GPU.intern_frames = 0;
				oldcycles = cpucycles;
				oldpixels = GPU.pixeldrawn;
				oldcommands = CPU.commands;
				SDL_UnlockMutex(GPU.drawlock);
			}
		}
	}

	psx.on = false;
	int threadReturnValue;
	SDL_WaitThread(psxthread, &threadReturnValue);

	SDL_DestroyWindow(window);
	SDL_Quit();
}

int main(int argc, char* argv[])
{
#if DEBUG
	speedunlock = true;
#endif

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER); // SDL_INIT_EVERYTHING
	TTF_Init();
	set_displaysize(4, false);
	OSD.init();

	GPU.drawlock = SDL_CreateMutex();
	psx.psxlock = SDL_CreateMutex();

	//autotest = true;
	if (autotest)
	{
		speedunlock = true;
		GPU.lockSpeed = false;
		GPU.frameskip = 0;
		OSD.gotoLoadGame();
		OSD.selected = 2;
		OSD.selectFile();
		FILE* file = fopen("R:\\test.txt", "w");
		fclose(file);
		openrom();
	}
	else
	{
		//psx.filename = "CPUADD.exe";
		
		//loadstate_fromdisk("ss_out.ss");

		openrom();
	}

	//autotest = true;
	GPU.autotest = autotest;

	drawer();

	return 0;
}
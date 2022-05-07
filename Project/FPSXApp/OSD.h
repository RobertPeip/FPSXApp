#include <iostream>
#include <vector>
#include <algorithm>
#ifdef WIN32
	#include <windows.h>
#endif
using namespace std;

#include "dirent.h"
#include "SDL.h"
#include "SDL_ttf.h"

enum class OSDTYPE
{
	MAIN,
	LOAD
};

enum class OSDMAINMENU
{
	TITLE,
	LOADGAME,
	SAVESTATE,
	SAVEMEMDISK,
	SAVESTATEDISK,
	LOADSTATE,
	LOADSTATEDISK,
	DISPLAYSIZE,
	CPUSTEPS,
	EXIT
};

class Osd
{
public:
	bool isOpen;
	bool idle;
	int selected;
	int selectedFile;
	string selectedItem;

	int displaysize;

	OSDTYPE OsdType;

	void init();
	void gotoMain();
	void gotoLoadGame();
	void selectNext();
	void selectPrev();
	void largeNext();
	void largePrev();
	void exchangeText(int index, string text);
	bool selectFile();
	void render(SDL_Renderer* renderer);

private:
	string currentPath;
	TTF_Font* font;
	std::vector<string> currentMenu;

	void saveLastFolder();
	void loadLastFolder();
};
extern Osd OSD;
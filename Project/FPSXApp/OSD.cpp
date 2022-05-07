#include "OSD.h"
#include "FileIO.h"
#include "CPU.h"
#include "psx.h"
#include "GPU.h"

Osd OSD;

void Osd::init()
{
	font = TTF_OpenFont(FileIO.getFullPath("font.ttf").c_str(), 8);
	gotoMain();
	isOpen = true;
	selectedFile = 1;
	loadLastFolder();
}

void Osd::gotoMain()
{
	selected = 1;
	OsdType = OSDTYPE::MAIN;

	currentMenu.clear();
	currentMenu.push_back("FPSXApp Settings:");
	currentMenu.push_back("Load Game");
	currentMenu.push_back("SaveState (F5)");
	currentMenu.push_back("SaveMemDisk (F7)");
	currentMenu.push_back("SaveStateDisk (F6)");
	currentMenu.push_back("LoadState (F9)");
	currentMenu.push_back("LoadStateDisk (F10)");
	currentMenu.push_back("Displaysize: " + std::to_string(displaysize));
	currentMenu.push_back("CPU Steps: " + std::to_string(CPU.additional_steps));
	currentMenu.push_back("EXIT");
}

void Osd::gotoLoadGame()
{
	selected = selectedFile;
	OsdType = OSDTYPE::LOAD;
	currentMenu.clear();
	currentMenu.push_back(currentPath);
	currentMenu.push_back("..");

	DIR* dir;
	struct dirent* ent;
	if ((dir = opendir(currentPath.c_str())) != NULL) 
	{
		while ((ent = readdir(dir)) != NULL) 
		{
			string name = ent->d_name;
			if (name.compare(".") > 0 && name.compare("..") > 0 && ent->d_type == DT_DIR)
			{
				currentMenu.push_back(ent->d_name);
			}
		}
		closedir(dir);
	}
	else 
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not open Folder: ", currentPath.c_str(), NULL);
	}

	std::vector<string> files;
	if ((dir = opendir(currentPath.c_str())) != NULL)
	{
		while ((ent = readdir(dir)) != NULL)
		{
			string name = ent->d_name;
			transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
		
			if (name.find(".exe") != -1 && name.length() >= 5) files.push_back(name);
			if (name.find(".bin") != -1 && name.length() >= 5 && (name.find("track") == -1 || name.find("track 1)") != -1) || name.find("track 01)") != -1) files.push_back(name);
		}
		closedir(dir);
	}
	else
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not open Folder: ", currentPath.c_str(), NULL);
	}
	sort(files.begin(), files.end());

	for (int i = 0; i < files.size(); i++)
	{
		currentMenu.push_back(files[i]);
	}
}

void Osd::selectNext()
{
	if (idle)
	{
		selected++;
		if (selected >= currentMenu.size())
		{
			selected = 1;
		}
		idle = false;
	}
}

void Osd::selectPrev()
{
	if (idle)
	{
		selected--;
		if (selected < 1)
		{
			selected = currentMenu.size() - 1;
		}
		idle = false;
	}
}

void Osd::largeNext()
{
	if (idle)
	{
		selected += 18;
		if (selected >= currentMenu.size())
		{
			selected = currentMenu.size() - 1;
		}
		idle = false;
	}
}

void Osd::largePrev()
{
	if (idle)
	{
		selected -= 18;
		if (selected < 1)
		{
			selected = 1;
		}
		idle = false;
	}
}

void Osd::exchangeText(int index, string text)
{
	currentMenu[index] = text;
}

bool Osd::selectFile()
{
	if (selected >= currentMenu.size()) return false;

	selectedItem = currentMenu[selected];
	selectedFile = selected;

	int pos = selectedItem.find(".");

	if (currentPath.length() == 0)
	{
		currentPath = selectedItem;
		gotoLoadGame();
	}
	else if (selectedItem.compare("..") == 0)
	{
		currentPath = currentPath.substr(0, currentPath.find_last_of("/\\"));
		currentPath = currentPath.substr(0, currentPath.find_last_of("/\\") + 1);
		if (currentPath.length() == 0)
		{
			currentMenu.clear();
			currentMenu.push_back("Select Drive");
#ifdef WIN32
			DWORD drivesCheck = 255;
			char driveArray[255];
			DWORD drivesCount = GetLogicalDriveStrings(drivesCheck, driveArray);
			int i = 0;
			string newname = "";
			while (i < drivesCount)
			{
				if (driveArray[i] == 0)
				{
					currentMenu.push_back(newname);
					newname = "";
				}
				else
				{
					newname = newname + string(1, driveArray[i]);
				}
				i++;
			}
#endif
		}
		else
		{
			gotoLoadGame();
		}
	}
	else if (pos == 0xFFFFFFFF)
	{
		currentPath += selectedItem;
		currentPath += "\\";
		gotoLoadGame();
	}
	else
	{
		saveLastFolder();
		psx.filename = currentPath + selectedItem;
		return true;
	}

	return false;
}

void Osd::render(SDL_Renderer* renderer)
{
	string fulltext = currentMenu[0] + "\n";
	int start = max(1, selected - 9);
	int end = min(start + 26, currentMenu.size());

	int itemcount = 1;
	for (int i = start; i < end; i++)
	{
		if (i == selected)
		{
			fulltext += "-->";
		}
		else if (i > 0)
		{
			fulltext += "   ";
		}

		fulltext += currentMenu[i].substr(0, 30);
		if (i < currentMenu.size() - 1)
		{
			fulltext += '\n';
		}
		itemcount++;
	}

	const char* charstring = fulltext.c_str();

	SDL_Color White = { 255, 255, 255 };
	SDL_Surface* surfaceMessage = TTF_RenderText_Blended_Wrapped(font, charstring, White, 240);
	SDL_Texture* Message = SDL_CreateTextureFromSurface(renderer, surfaceMessage);
	SDL_Rect Message_rect;
	Message_rect.x = 0;
	Message_rect.y = 0;
	if (currentMenu.size() == 1)
	{
		Message_rect.w = 35;
	}
	else
	{
		Message_rect.w = 240;
	}
	Message_rect.h = itemcount * 8;
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, Message, NULL, &Message_rect);

	SDL_FreeSurface(surfaceMessage);
	SDL_DestroyTexture(Message);
}

void Osd::saveLastFolder()
{
	const char* savetext = currentPath.c_str();
	FileIO.writefile((void*)savetext, "lastDir.txt", currentPath.size(), false);
}

void Osd::loadLastFolder()
{
	currentPath = SDL_GetBasePath();
	if (FileIO.fileExists("lastDir.txt", false))
	{
		char loadtext[2000];
		FileIO.readfile((void*)loadtext, "lastDir.txt", false);
		currentPath = loadtext;
		currentPath = currentPath.substr(0, currentPath.find_last_of("/\\"));
		currentPath += "\\";
	}
}

#include "Player.h"
#include "Platform.h"
#include "Commonvars.h"

void DrawPlayer()
{

	GFX.fillRect((xoffset + ((PlayerPosX + 1) * BoxWidth))+1, (yoffset + ((PlayerPosY + 1) * BoxHeight) + 1),
		BoxWidth - 1, BoxHeight - 1,ColorRed);
}

void ErasePlayer()
{
	GFX.fillRect((xoffset + ((PlayerPosX + 1) * BoxWidth))+1, (yoffset + ((PlayerPosY + 1) * BoxHeight) + 1),
		BoxWidth - 1, BoxHeight - 1, ColorBlack);
}

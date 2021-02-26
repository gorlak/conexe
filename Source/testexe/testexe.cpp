#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	printf("Hello world from /SUBSYSTEM:WINDOWS exe\n");
	fflush(stdout);
	return 0;
}
#include <windows.h>

////////////////////////////////////////////////////////////////////////////////

#undef CopyMemory
typedef void (*CopyMemoryFunc)(void*, const void*, size_t);
CopyMemoryFunc CopyMemory;

bool LoadFunctions()
{
	HMODULE hKernel32 = LoadLibrary(L"kernel32.dll");
	if (!hKernel32)
	{
		return false;
	}

	CopyMemory = (CopyMemoryFunc)::GetProcAddress(hKernel32, "RtlMoveMemory");
	if (!CopyMemory)
	{
		return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////

void CopyChars(wchar_t* destination, wchar_t const* source, size_t count)
{
	CopyMemory(destination, source, count * sizeof(wchar_t));
}

////////////////////////////////////////////////////////////////////////////////

void CopyString(wchar_t* destination, wchar_t const* source, size_t count)
{
	CopyChars(destination, source, count);
	destination[count] = L'\0';
}

////////////////////////////////////////////////////////////////////////////////

size_t StringLength(wchar_t const* string)
{
	size_t length = 0;
	for (const wchar_t* c = string; *c != L'\0'; c++)
	{
		length++;
	}
	return length;
}

////////////////////////////////////////////////////////////////////////////////

bool SplitPathAndName(wchar_t* path, size_t length, wchar_t const** name)
{
	for (size_t i = length - 1; i != 0; i--)
	{
		if (path[i] == L'\\' || path[i] == L'/')
		{
			path[i] = L'\0';
			(*name) = &path[i + 1];
			return true;
		}
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
// https://www.codeproject.com/Articles/1045674/Load-EXE-as-DLL-Mission-Possible
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN64
# define IMAGE_ORDINAL_FLAG IMAGE_ORDINAL_FLAG64
#else
# define IMAGE_ORDINAL_FLAG IMAGE_ORDINAL_FLAG32
#endif

bool RemapImportAddressTable(HINSTANCE hInstance)
{
	HMODULE hDbgHelp = ::LoadLibrary(L"dbghelp.dll");
	if (!hDbgHelp)
	{
		return false;
	}

	typedef PVOID(__stdcall* ImageDirectoryEntryToDataFunc)(PVOID Base, BOOLEAN MappedAsImage, USHORT DirectoryEntry, PULONG Size);
	ImageDirectoryEntryToDataFunc ImageDirectoryEntryToData = (ImageDirectoryEntryToDataFunc)::GetProcAddress(hDbgHelp, "ImageDirectoryEntryToData");
	if (!ImageDirectoryEntryToData)
	{
		return false;
	}

	DWORD ulsize = 0;
	PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(hInstance, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &ulsize);
	if (!pImportDesc)
	{
		return false;
	}

	bool result = true;
	for (; pImportDesc->Name; pImportDesc++)
	{
		PSTR pszModName = (PSTR)((PBYTE)hInstance + pImportDesc->Name);
		if (!pszModName)
		{
			break;
		}

		HINSTANCE hImportDll = LoadLibraryA(pszModName);
		if (!hImportDll)
		{
			result = false;
			continue;
		}

		PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((PBYTE)hInstance + pImportDesc->FirstThunk);
		for (; pThunk->u1.Function; pThunk++)
		{
			FARPROC pfnNew = 0;
			size_t rva = 0;
			if (pThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)
			{
				size_t ord = IMAGE_ORDINAL(pThunk->u1.Ordinal);
				PROC* ppfn = (PROC*)&pThunk->u1.Function;
				if (!ppfn)
				{
					result = false;
					continue;
				}
				rva = (size_t)pThunk;

				pfnNew = GetProcAddress(hImportDll, (LPCSTR)ord);
				if (!pfnNew)
				{
					result = false;
					continue;
				}
			}
			else
			{
				PROC* ppfn = (PROC*)&pThunk->u1.Function;
				if (!ppfn)
				{
					result = false;
					continue;
				}

				rva = (size_t)pThunk;
				PSTR fName = (PSTR)hInstance;
				fName += pThunk->u1.Function;
				fName += 2;
				if (!fName)
				{
					result = false;
					break;
				}

				pfnNew = GetProcAddress(hImportDll, fName);
				if (!pfnNew)
				{
					result = false;
					continue;
				}
			}

			HANDLE hProcess = GetCurrentProcess();
			if (!WriteProcessMemory(hProcess, (LPVOID*)rva, &pfnNew, sizeof(pfnNew), NULL) && (ERROR_NOACCESS == GetLastError()))
			{
				DWORD dwOldProtect;
				if (VirtualProtect((LPVOID)rva, sizeof(pfnNew), PAGE_WRITECOPY, &dwOldProtect))
				{
					if (!WriteProcessMemory(GetCurrentProcess(), (LPVOID*)rva, &pfnNew,
						sizeof(pfnNew), NULL))
					{
						continue;
					}
					if (!VirtualProtect((LPVOID)rva, sizeof(pfnNew), dwOldProtect,
						&dwOldProtect))
					{
						continue;
					}
				}
			}
		}
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////////

wchar_t szDirectory[8192];

int wmain(int argc, wchar_t* argv[])
{
	if (argc < 2)
	{
		wchar_t message[] = L"Please specify the path of the exe with exported entry point WinMainCRTStartup or WinMain\n";
		DWORD written;
		WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message)/sizeof(wchar_t), &written, nullptr);
		return 1;
	}

	// import some help from system dlls
	if (!LoadFunctions())
	{
		return ~0ll;
	}

	// chdir to the exe we wish to load
	wchar_t const* lpszExePath = argv[1];
	size_t lpszExePathLength = StringLength(lpszExePath);
	CopyString(szDirectory, lpszExePath, lpszExePathLength);
	wchar_t const* lpszName = nullptr;
	if (SplitPathAndName(szDirectory, lpszExePathLength, &lpszName))
	{
		SetCurrentDirectory(szDirectory);
	}
	else
	{
		// would be handy to search %PATH% here
		lpszName = szDirectory;
	}

	// load the exe as dll
	HINSTANCE hExecutable = LoadLibrary(lpszName);
	if (!hExecutable)
	{
		return ~0ll;
	}

	// fixup import table addresses
	if (!RemapImportAddressTable(hExecutable))
	{
		return ~0ll;
	}

	// call entry point
	typedef int (*WinMainCRTStartupFunc)();
	WinMainCRTStartupFunc winMainCRTStartup = (WinMainCRTStartupFunc)GetProcAddress(hExecutable, "WinMainCRTStartup");
	if (winMainCRTStartup)
	{
		return winMainCRTStartup();
	}

	// call entry point
	typedef int(__stdcall* WinMainFunc)(HINSTANCE, HINSTANCE, LPWSTR, int);
	WinMainFunc winMain = (WinMainFunc)GetProcAddress(hExecutable, "WinMain");
	if (winMain)
	{
		return winMain(GetModuleHandle(NULL), nullptr, ::GetCommandLine(), SW_SHOWDEFAULT);
	}

	// unable to locate entry point
	return ~0ll;
}

////////////////////////////////////////////////////////////////////////////////

extern "C" int wmainCRTStartup()
{
	LPWSTR* szArglist;
	int nArgs;
	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	return wmain(nArgs, szArglist);
}
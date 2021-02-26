conexe
===

Run Windows subsystem program with stdio handles
---

Windows and it's programs are designed where you have to choose at _link time_ if you want stdio handles to be allocated to your program _at runtime_. This creates a choice at link/compile time:

`/SUBSYSTEM:CONSOLE`
---

You get stdin/stdout/stderr `HANDLE`s allocated, but the system almost always opens a console window of some sort (if one isn't open already). In frequent use cases this console window is just in the way, and closing it can have consequences.

`/SUBSYSTEM:WINDOWS`
---

You don't get stdin/stdout/stderr `HANDLE`s allocated, and allocating them after the fact is tricky. Particularly registering them with the CRT of your process after the CRT has already been static initialized and main() called.

There are some workarounds that people tend to use:
* `::AllocConsole` will give you a console window, but the `HANDLE` experience and piping isn't _real_.
* Linking another copy of your program using `/SUBSYSTEM:CONSOLE` is wasteful and adds complexity.

Approach
---

The idea behind conexe is that you invoke conexe, which is a Console subsystem program, in front of your Windows subsystem program. Windows will allocate native handles to conexe. Then conexe loads your program via `::LoadLibrary` and call an agreed upon entry point.

A test Windows subsystem program is program is provided. All you have to do is export an entry point that conexe can `::GetProcAddress`, and take the customary parameters and return code.

Details
---

conexe is coded to have as few dependencies as possible, via `/NODEFAULTLIB`. This means it uses routines from the most base Windows usermode DLLs, and doesn't consume any C/C++ Runtime.

conexe will `chdir` to the target exe's folder, if it's provided by absolute path.

To the best of my knowledge static imports (kernel loads) and dynamic loads (`::LoadLibrary`) should retain default load order specific by [Dynamic-Link Library Search Order](https://docs.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order). If you find something that fails to load, then make a minimal repro and writeup an issue.

Future
---

I think there are some nice-to-haves to explore here:
* %PATH% lookups for convenience of command line authoring
* Servicing of `::OutputDebugStringW` and echo to conexe's `HANDLE`s

Shoutouts
---

* Import table patching via [Load EXE as DLL: Mission Possible](https://www.codeproject.com/Articles/1045674/Load-EXE-as-DLL-Mission-Possible)
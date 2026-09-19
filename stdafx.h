#pragma once

#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <timeapi.h> 
#include <winhttp.h>
#include <objbase.h> 
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdio>

#include "../foobar2000-sdk/foobar2000/SDK/foobar2000.h"
#include "../foobar2000-sdk/foobar2000/helpers/helpers.h"
#include "../foobar2000-sdk/foobar2000/SDK/preferences_page.h"

#include <atlbase.h>
#include <atlapp.h>
extern CAppModule _Module;
#include <atlwin.h>
#include <atlcrack.h> 
#include <atlgdi.h>
#include <atlctrls.h>
#include <atlmisc.h>
#include <atldlgs.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")

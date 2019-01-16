#include "stdafx.h"
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <iostream>
#include <commctrl.h>
#include <strsafe.h>
#include <objbase.h>

#include <functional>
#include <vector>
#include <codecvt>
#include <string>
#include <ctime>

#include "pluginapi.h"

// NSIS vars
unsigned int g_stringsize;
stack_t **g_stacktop;
TCHAR *g_variables;

// Used in DlgProc and RunModalDialog
HINSTANCE gDllInstance;

// NSIS stack

const TCHAR* const NSISCALL popstring()
{
	stack_t *th;
	if (!g_stacktop || !*g_stacktop) return nullptr;
	th = (*g_stacktop);

	size_t strLen = _tcsclen(th->text);
	TCHAR* buf = new TCHAR[strLen + 1];
	_tcsncpy_s(buf, strLen + 1, th->text, strLen);
	buf[strLen] = 0;

	*g_stacktop = th->next;
	GlobalFree((HGLOBAL)th);

	return buf;
}

int NSISCALL popstring(TCHAR *str)
{
	stack_t *th;
	if (!g_stacktop || !*g_stacktop) return 1;
	th = (*g_stacktop);
	if (str) _tcsncpy_s(str, g_stringsize, th->text, g_stringsize);
	*g_stacktop = th->next;
	GlobalFree((HGLOBAL)th);
	return 0;
}

INT_PTR NSISCALL popintptr()
{
	INT_PTR result = 0;

	const TCHAR* str = popstring();

	if (str) {
		result = _tstoi(str);
		
		GlobalFree((HGLOBAL)str);
	}
	else {
		result = 0;
	}

	return result;
}

int NSISCALL popstringn(TCHAR *str, int maxlen)
{
	stack_t *th;
	if (!g_stacktop || !*g_stacktop) return 1;
	th = (*g_stacktop);
	if (str) _tcsncpy_s(str, maxlen ? maxlen : g_stringsize, th->text, g_stringsize);
	*g_stacktop = th->next;
	GlobalFree((HGLOBAL)th);
	return 0;
}

void NSISCALL pushstring(const TCHAR *str)
{
	stack_t *th;
	if (!g_stacktop) return;
	th = (stack_t*)GlobalAlloc(GPTR, (sizeof(stack_t) + (g_stringsize) * sizeof(TCHAR)));
	_tcsncpy_s(th->text, g_stringsize, str, g_stringsize);
	th->text[g_stringsize - 1] = 0;
	th->next = *g_stacktop;
	*g_stacktop = th;
}

TCHAR* NSISCALL getuservariable(const int varnum)
{
	if (varnum < 0 || varnum >= __INST_LAST) return NULL;
	return g_variables + varnum * g_stringsize;
}

void NSISCALL setuservariable(const int varnum, const TCHAR *var)
{
	if (var != NULL && varnum >= 0 && varnum < __INST_LAST)
		_tcsncpy_s(g_variables + varnum * g_stringsize, g_stringsize, var, g_stringsize);
}

///////////////////////////////////////////////////////////////////////
// HTTP
///////////////////////////////////////////////////////////////////////

// convert UTF-8 string to wstring
std::wstring utf8_to_wstring(const std::string& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.from_bytes(str);
}

// convert wstring to UTF-8 string
std::string wstring_to_utf8(const std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.to_bytes(str);
}

std::string tchar_to_utf8(const TCHAR* str)
{
#ifdef UNICODE
	return wstring_to_utf8(std::wstring(str));
#else
	return std::string(str);
#endif
}

#ifdef UNICODE
typedef std::wstring tstring;
#else
typedef std::string tstring;
#endif

tstring utf8_to_tstring(const std::string& str)
{
#ifdef UNICODE
	return utf8_to_wstring(str);
#else
	return str;
#endif
}

tstring wstring_to_tstring(const std::wstring& str)
{
#ifdef UNICODE
	return str;
#else
	return wstring_to_utf8(str);
#endif
}

///////////////////////////////////////////////////////////////////////
// GUID
///////////////////////////////////////////////////////////////////////

static tstring CreateGUIDString()
{
	tstring result;

	const int GUID_STRING_LENGTH = 39;

	GUID guid;
	CoCreateGuid(&guid);

	OLECHAR guidStr[GUID_STRING_LENGTH];
	StringFromGUID2(guid, guidStr, GUID_STRING_LENGTH);

	guidStr[GUID_STRING_LENGTH - 2] = 0;
	result = wstring_to_tstring(guidStr + 1);

	return result;
}

#include <wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Advapi32.lib")
static std::string GetComputerSystemUniqueId()
{
	std::string result = "";

	const char* REG_KEY_PATH = "SOFTWARE\\StreamElements";
	const char* REG_VALUE_NAME = "MachineUniqueIdentifier";

	HKEY hkeyRoot;
	if (ERROR_SUCCESS == RegCreateKeyA(HKEY_LOCAL_MACHINE, REG_KEY_PATH, &hkeyRoot)) {
		RegCloseKey(hkeyRoot);
	}

	char buf[128];
	DWORD bufLen = 128;

	if (ERROR_SUCCESS == RegGetValueA(
		HKEY_LOCAL_MACHINE,
		REG_KEY_PATH,
		REG_VALUE_NAME,
		RRF_RT_REG_SZ,
		NULL,
		buf,
		&bufLen)) {
		result = buf;
	}
	else {
		// Get unique ID from WMI

		HRESULT hr = CoInitialize(NULL);

		// https://docs.microsoft.com/en-us/windows/desktop/wmisdk/initializing-com-for-a-wmi-application
		if (SUCCEEDED(hr)) {
			bool uinitializeCom = hr == S_OK;

			// https://docs.microsoft.com/en-us/windows/desktop/wmisdk/setting-the-default-process-security-level-using-c-
			CoInitializeSecurity(
				NULL,                       // security descriptor
				-1,                          // use this simple setting
				NULL,                        // use this simple setting
				NULL,                        // reserved
				RPC_C_AUTHN_LEVEL_DEFAULT,   // authentication level  
				RPC_C_IMP_LEVEL_IMPERSONATE, // impersonation level
				NULL,                        // use this simple setting
				EOAC_NONE,                   // no special capabilities
				NULL);                          // reserved

			IWbemLocator *pLocator;

			// https://docs.microsoft.com/en-us/windows/desktop/wmisdk/creating-a-connection-to-a-wmi-namespace
			hr = CoCreateInstance(
				CLSID_WbemLocator, 0,
				CLSCTX_INPROC_SERVER,
				IID_IWbemLocator,
				(LPVOID*)&pLocator);

			if (SUCCEEDED(hr)) {
				IWbemServices *pSvc = 0;

				// https://docs.microsoft.com/en-us/windows/desktop/wmisdk/creating-a-connection-to-a-wmi-namespace
				hr = pLocator->ConnectServer(
					BSTR(L"root\\cimv2"),  //namespace
					NULL,       // User name 
					NULL,       // User password
					0,         // Locale 
					NULL,     // Security flags
					0,         // Authority 
					0,        // Context object 
					&pSvc);   // IWbemServices proxy

				if (SUCCEEDED(hr)) {
					hr = CoSetProxyBlanket(pSvc,
						RPC_C_AUTHN_WINNT,
						RPC_C_AUTHZ_NONE,
						NULL,
						RPC_C_AUTHN_LEVEL_CALL,
						RPC_C_IMP_LEVEL_IMPERSONATE,
						NULL,
						EOAC_NONE
					);

					if (SUCCEEDED(hr)) {
						IEnumWbemClassObject *pEnumerator = NULL;

						hr = pSvc->ExecQuery(
							(BSTR)L"WQL",
							(BSTR)L"select * from Win32_ComputerSystemProduct",
							WBEM_FLAG_FORWARD_ONLY,
							NULL,
							&pEnumerator);

						if (SUCCEEDED(hr)) {
							IWbemClassObject *pObj = NULL;

							ULONG resultCount;
							hr = pEnumerator->Next(
								WBEM_INFINITE,
								1,
								&pObj,
								&resultCount);

							if (SUCCEEDED(hr)) {
								VARIANT value;

								hr = pObj->Get(L"UUID", 0, &value, NULL, NULL);

								if (SUCCEEDED(hr)) {
									if (value.vt != VT_NULL) {
										result = std::string("WUID/") + wstring_to_utf8(std::wstring(value.bstrVal));
									}
									VariantClear(&value);
								}
							}

							pEnumerator->Release();
						}
					}

					pSvc->Release();
				}

				pLocator->Release();
			}

			if (uinitializeCom) {
				CoUninitialize();
			}
		}
	}

	if (!result.size()) {
		// Failed retrieving UUID, generate our own
		result = std::string("SEID/") + tchar_to_utf8(CreateGUIDString().c_str());
	}

	// Save for future use
	RegSetKeyValueA(
		HKEY_LOCAL_MACHINE,
		REG_KEY_PATH,
		REG_VALUE_NAME,
		REG_SZ,
		result.c_str(),
		result.size());

	return result;
}

///////////////////////////////////////////////////////////////////////
// API
///////////////////////////////////////////////////////////////////////

#define NSISFUNC(name) extern "C" void __declspec(dllexport) name(HWND hWndParent, int string_size, TCHAR* variables, stack_t** stacktop, extra_parameters* extra)

NSISFUNC(GenerateGloballyUniqueIdentifier)
{
	EXDLL_INIT();

	pushstring(CreateGUIDString().c_str());
}

NSISFUNC(GetComputerSystemUniqueIdentifier)
{
	EXDLL_INIT();
	
	pushstring(utf8_to_tstring(GetComputerSystemUniqueId()).c_str());
}

NSISFUNC(GetSecondsSinceEpochStart)
{
	EXDLL_INIT();

	std::time_t time = std::time(nullptr);

#ifdef UNICODE
	pushstring(std::to_wstring(time).c_str());
#else
	pushstring(std::to_string(time).c_str());
#endif
}

static tstring GetRegKeyPath(const TCHAR* regValueName, const TCHAR* productName = nullptr)
{
	tstring REG_KEY_PATH = TEXT("SOFTWARE\\StreamElements");
	if (productName && productName[0]) {
		REG_KEY_PATH += TEXT("\\");
		REG_KEY_PATH += productName;
	}

	return REG_KEY_PATH;
}

static tstring ReadEnvironmentConfigString(const TCHAR* regValueName, const TCHAR* productName = nullptr)
{
	tstring result = TEXT("");

	tstring REG_KEY_PATH = GetRegKeyPath(regValueName, productName);

	DWORD bufLen = 16384;
	TCHAR* buffer = new TCHAR[bufLen];

	LSTATUS lResult = RegGetValue(
		HKEY_LOCAL_MACHINE,
		REG_KEY_PATH.c_str(),
		regValueName,
		RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
		NULL,
		buffer,
		&bufLen);

	if (lResult != ERROR_SUCCESS) {
		lResult = RegGetValue(
			HKEY_LOCAL_MACHINE,
			REG_KEY_PATH.c_str(),
			regValueName,
			RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY,
			NULL,
			buffer,
			&bufLen);
	}

	if (ERROR_SUCCESS == lResult) {
		result = buffer;
	}

	delete[] buffer;

	return result;
}

static tstring WriteEnvironmentConfigString(const TCHAR* regValueName, const TCHAR* regValue, const TCHAR* productName = nullptr)
{
	tstring result = TEXT("");

	tstring REG_KEY_PATH = GetRegKeyPath(regValueName, productName);

	LSTATUS lResult = RegSetKeyValue(
		HKEY_LOCAL_MACHINE,
		REG_KEY_PATH.c_str(),
		regValueName,
		REG_SZ,
		regValue,
		_tcslen(regValue));

	if (lResult != ERROR_SUCCESS) {
		result = TEXT("error");
	}
	else {
		result = TEXT("ok");
	}

	return result;
}

NSISFUNC(ReadEnvironmentConfigurationString)
{
	EXDLL_INIT();

	tstring result = TEXT("");

	const TCHAR* regValueName = popstring();

	if (regValueName) {
		result = ReadEnvironmentConfigString(regValueName, nullptr);

		::GlobalFree((HGLOBAL)regValueName);
	}

	pushstring(result.c_str());
}

NSISFUNC(WriteEnvironmentConfigurationString)
{
	EXDLL_INIT();

	tstring result = TEXT("");

	const TCHAR* regValue = popstring();
	const TCHAR* regValueName = popstring();

	if (regValueName && regValue) {
		result = WriteEnvironmentConfigString(regValueName, regValue, nullptr);
	}

	if (regValue) {
		::GlobalFree((HGLOBAL)regValue);
	}

	if (regValueName) {
		::GlobalFree((HGLOBAL)regValueName);
	}

	pushstring(result.c_str());
}

NSISFUNC(ReadProductEnvironmentConfigurationString)
{
	EXDLL_INIT();

	tstring result = TEXT("");

	const TCHAR* regValueName = popstring();
	const TCHAR* productName = popstring();

	if (regValueName && productName) {
		result = ReadEnvironmentConfigString(regValueName, productName);
	}

	if (productName) {
		::GlobalFree((HGLOBAL)productName);
	}

	if (regValueName) {
		::GlobalFree((HGLOBAL)regValueName);
	}

	pushstring(result.c_str());
}

NSISFUNC(WriteProductEnvironmentConfigurationString)
{
	EXDLL_INIT();

	tstring result = TEXT("");

	const TCHAR* regValue = popstring();
	const TCHAR* regValueName = popstring();
	const TCHAR* productName = popstring();

	if (regValueName && regValue) {
		result = WriteEnvironmentConfigString(regValueName, regValue, productName);
	}

	if (productName) {
		::GlobalFree((HGLOBAL)productName);
	}

	if (regValueName) {
		::GlobalFree((HGLOBAL)regValueName);
	}

	if (regValue) {
		::GlobalFree((HGLOBAL)regValue);
	}

	pushstring(result.c_str());
}

BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
)
{
	if (hinstDLL) {
		gDllInstance = hinstDLL;
	}

	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}


#ifdef TARGET_EXE
//
// This is used only in "EXE Debug" configuration
// for easy step-through debugging as an EXE.
//
int main(void)
{
	std::cout << ReadEnvironmentConfigString(TEXT("HeapAnalyticsAppId")) << std::endl;
	//std::cout << GetComputerSystemUniqueId() << std::endl;
	/*
	std::cout << CreateGUIDString() << std::endl;

	{
		std::string postData = "{ \"app_id\": \"3780144397\", \"identity\": \"1E1B8CEE-884E-4ED9-9359-DC5BA786836A\", \"event\": \"Test Server-Side API Event\", \"properties\": { } }";
		const TCHAR* url = TEXT("https://heapanalytics.com/api/track");
		const TCHAR* request_headers = "Content-Type: application/json";
		
		bool result = HttpRequest(
			TEXT("POST"),
			url,
			g_userAgent.c_str(), // user-agent
			request_headers,
			_tcslen(request_headers),
			postData.c_str(),
			postData.size(),
			[](const TCHAR* headers, const void* buffer, const size_t buffer_len) -> bool {
				std::cout << TEXT("Headers: ") << headers << std::endl;

				char* buf = (char*)buffer;
				buf[buffer_len] = 0;
				std::cout << "Buffer:" << std::endl << buf << std::endl;
				return true;
			});

		std::cout << result << std::endl;
	}

	TaskConsumersInit(4);

	for (size_t i = 0; i < 0; ++i) {
		EnqueueTask([=]() {
			const TCHAR* REQUEST_HEADERS = TEXT("Content-Type: text/json");

			std::string request_content;
			request_content = "{}";

			HttpRequest(
				TEXT("POST"),
				TEXT("http://localhost:3000"),
				NULL, // user-agent
				REQUEST_HEADERS,
				_tcslen(REQUEST_HEADERS),
				request_content.c_str(),
				request_content.size(),
				[](const TCHAR* headers, const void* buffer, const size_t buffer_len) -> bool {
					std::cout << TEXT("Headers: ") << headers << std::endl;

					char* buf = (char*)buffer;
					buf[buffer_len] = 0;
					std::cout << "Buffer:" << std::endl << buf << std::endl;
					return true;
				});
		});
	}

	TaskConsumersShutdown(true);
	*/

	return 0;
}
#endif
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
// Strings
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

#include <regex>
tstring clean_guid_string(tstring input)
{
#ifdef UNICODE
	return std::regex_replace(std::wstring(input), std::wregex(L"-"), L"");
#else
	return std::regex_replace(input, std::regex("-"), "");
#endif
}

///////////////////////////////////////////////////////////////////////
// Registry
///////////////////////////////////////////////////////////////////////

static long RegCloneBranch(HKEY hkeyDestRoot, HKEY hkeySrcRoot)
{
	long result = ERROR_SUCCESS;
	DWORD index;
	DWORD subkeys, maxkeyname, values, maxvaluename, maxdata, type;
	LPTSTR lpName = NULL, lpData = NULL;

	/* get information, so that we know how much memory to allocate */
	result = RegQueryInfoKey(hkeySrcRoot, NULL, NULL, NULL, &subkeys, &maxkeyname,
		NULL, &values, &maxvaluename, &maxdata, NULL, NULL);
	
	if (result != ERROR_SUCCESS)
		return result;

	/* in Windows NT/2000/XP, the name lengths do not include the '\0' terminator */
	maxkeyname++;
	maxvaluename++;

	/* allocate buffers, one for data and one for value & class names */
	if (maxvaluename > maxkeyname)
		maxkeyname = maxvaluename;

	lpName = new TCHAR[maxkeyname];

	if (lpName == NULL) {
		result = ERROR_NOT_ENOUGH_MEMORY;
		goto error_exit;
	} /* if */

	if (maxdata > 0) {
		lpData = new TCHAR[maxdata];
		if (lpData == NULL) {
			result = ERROR_NOT_ENOUGH_MEMORY;
			goto error_exit;
		} /* if */
	}
	else {
		lpData = NULL;
	} /* if */

	/* first walk through the values */
	for (index = 0; index < values; index++) {
		DWORD namesize = maxkeyname;
		DWORD datasize = maxdata;

		result = RegEnumValue(hkeySrcRoot, index, lpName, &namesize, NULL, &type, (LPBYTE)lpData, &datasize);

		if (result != ERROR_SUCCESS)
			goto error_exit;

		result = RegSetValueEx(hkeyDestRoot, lpName, 0L, type, (LPBYTE)lpData, datasize);
		if (result != ERROR_SUCCESS)
			goto error_exit;
	} /* for */

	/* no longer need the data block */
	if (lpData != NULL) {
		delete[] lpData;
		lpData = NULL;
	} /* if */

	/* no walk through all subkeys, and recursively call this function to copy the tree */
	for (index = 0; index < subkeys; index++) {
		DWORD namesize = maxkeyname;
		HKEY hkeySrc;
		HKEY hkeyDest;
		
		RegEnumKeyEx(hkeySrcRoot, index, lpName, &namesize, NULL, NULL, NULL, NULL);
		
		result = RegOpenKeyEx(hkeySrcRoot, lpName, 0L, KEY_READ, &hkeySrc);
		
		if (result != ERROR_SUCCESS)
			goto error_exit;
		
		result = RegCreateKeyEx(hkeyDestRoot, lpName, 0L, NULL, REG_OPTION_NON_VOLATILE,
			KEY_WRITE, NULL, &hkeyDest, NULL);
		
		if (result != ERROR_SUCCESS)
			goto error_exit;
		
		RegCloneBranch(hkeyDest, hkeySrc);

		RegCloseKey(hkeySrc);
		RegCloseKey(hkeyDest);
	} /* for */

	delete[] lpName;
	lpName = NULL;

	return ERROR_SUCCESS;

error_exit:
	if (lpName != NULL)
		free(lpName);
	if (lpData != NULL)
		free(lpData);
	return result;
}

static bool Move64BitRegistryBranchTo32BitLocation()
{
#ifdef _WIN64
	bool success = false;

	HKEY h64 = 0;
	HKEY h32 = 0;

	if (ERROR_SUCCESS != RegOpenKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StreamElements"), &h64)) {
		goto cleanup;
	}

	if (ERROR_SUCCESS != RegOpenKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\WOW6432Node\\StreamElements"), &h32)) {
		goto cleanup;
	}

	if (ERROR_SUCCESS == RegCloneBranch(h32, h64)) {
		success = true;
	}

cleanup:
	if (h64) RegCloseKey(h64);
	if (h32) RegCloseKey(h32);

	if (success) {
		if (ERROR_SUCCESS == RegDeleteKey(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StreamElements"))) {
			return true;
		}
	}
#endif

	return false;
}


static tstring GetEnvironmentConfigRegKeyPath(const TCHAR* regValueName, const TCHAR* productName = nullptr)
{
#ifdef _WIN64
	tstring REG_KEY_PATH = TEXT("SOFTWARE\\WOW6432Node\\StreamElements");
#else
	tstring REG_KEY_PATH = TEXT("SOFTWARE\\StreamElements");
#endif

	if (productName && productName[0]) {
		REG_KEY_PATH += TEXT("\\");
		REG_KEY_PATH += productName;
	}

	return REG_KEY_PATH;
}

static tstring ReadEnvironmentConfigString(const TCHAR* regValueName, const TCHAR* productName = nullptr)
{
	tstring result = TEXT("");

	tstring REG_KEY_PATH = GetEnvironmentConfigRegKeyPath(regValueName, productName);

	DWORD bufLen = 16384;
	TCHAR* buffer = new TCHAR[bufLen];

	LSTATUS lResult = RegGetValue(
		HKEY_LOCAL_MACHINE,
		REG_KEY_PATH.c_str(),
		regValueName,
		RRF_RT_REG_SZ,
		NULL,
		buffer,
		&bufLen);

	if (ERROR_SUCCESS == lResult) {
		result = buffer;
	}

	delete[] buffer;

	return result;
}

static tstring WriteEnvironmentConfigString(const TCHAR* regValueName, const TCHAR* regValue, const TCHAR* productName = nullptr)
{
	tstring result = TEXT("");

	tstring REG_KEY_PATH = GetEnvironmentConfigRegKeyPath(regValueName, productName);

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

///////////////////////////////////////////////////////////////////////
// GUID
///////////////////////////////////////////////////////////////////////

static tstring CreateGloballyUniqueIdString()
{
	const int GUID_STRING_LENGTH = 39;

	GUID guid;
	CoCreateGuid(&guid);

	OLECHAR guidStr[GUID_STRING_LENGTH];
	StringFromGUID2(guid, guidStr, GUID_STRING_LENGTH);

	guidStr[GUID_STRING_LENGTH - 2] = 0;

	return wstring_to_tstring(guidStr + 1);
}

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
static tstring CreateCryptoSecureRandomNumberString()
{
	tstring result = TEXT("0");

	BCRYPT_ALG_HANDLE hAlgo;

	if (0 == BCryptOpenAlgorithmProvider(&hAlgo, BCRYPT_RNG_ALGORITHM, NULL, 0)) {
		uint64_t buffer;

		if (0 == BCryptGenRandom(hAlgo, (PUCHAR)&buffer, sizeof(buffer), 0)) {
			char buf[sizeof(buffer) * 2 + 1];
			sprintf_s(buf, sizeof(buf), "%llX", buffer);

			result = utf8_to_tstring(buf);
		}

		BCryptCloseAlgorithmProvider(hAlgo, 0);
	}

	return result;
}

#include <wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Advapi32.lib")
static tstring GetComputerSystemUniqueId()
{
	const TCHAR* REG_VALUE_NAME = TEXT("MachineUniqueIdentifier");

	tstring result = ReadEnvironmentConfigString(REG_VALUE_NAME);

	if (result.size()) {
		// Discard invalid values
		if (result == TEXT("WUID/03000200-0400-0500-0006-000700080009") || // Known duplicate
			result == TEXT("WUID/00000000-0000-0000-0000-000000000000") || // Null value
			result == TEXT("WUID/FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF") || // Invalid value
			result == TEXT("WUID/00412F4E-0000-0000-0000-0000FFFFFFFF")) { // Set by russian MS Office crack
			result = TEXT("");
		}
	}

	if (!result.size()) {
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
										result = tstring(TEXT("SWID/")) + clean_guid_string(wstring_to_tstring(std::wstring(value.bstrVal)));
										result += TEXT("-");
										result += clean_guid_string(CreateGloballyUniqueIdString());
										result += TEXT("-");
										result += CreateCryptoSecureRandomNumberString();
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
		result = tstring(TEXT("SEID/")) + clean_guid_string(CreateGloballyUniqueIdString());
		result += TEXT("-");
		result += CreateCryptoSecureRandomNumberString();
	}

	result = result;

	// Save for future use
	WriteEnvironmentConfigString(REG_VALUE_NAME, result.c_str());

	return result;
}

///////////////////////////////////////////////////////////////////////
// API
///////////////////////////////////////////////////////////////////////

#define NSISFUNC(name) extern "C" void __declspec(dllexport) name(HWND hWndParent, int string_size, TCHAR* variables, stack_t** stacktop, extra_parameters* extra)

NSISFUNC(GenerateGloballyUniqueIdentifier)
{
	EXDLL_INIT();

	pushstring(CreateGloballyUniqueIdString().c_str());
}

NSISFUNC(GetComputerSystemUniqueIdentifier)
{
	EXDLL_INIT();
	
	pushstring(GetComputerSystemUniqueId().c_str());
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
		Move64BitRegistryBranchTo32BitLocation();
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
	std::cout << GetComputerSystemUniqueId() << std::endl;
	std::cout << Move64BitRegistryBranchTo32BitLocation() << std::endl;
	return 0;
}
#endif
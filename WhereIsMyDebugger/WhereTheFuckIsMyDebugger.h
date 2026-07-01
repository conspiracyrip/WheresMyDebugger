#pragma once
#include <windows.h>
#include <iostream>
#include <TlHelp32.h>
#include <vector>
#include <random>


namespace WheresMyDebugger
{
	struct ProcessData
	{
		const char* WindowTitle;
		const char* WindowClass;
		void* CreateWindowExA;
		void* RegisterClassA;
		void* DefWindowProcA;
		void* Sleep;
		void* AddressToUseAsModule;
	};



#pragma runtime_checks( "", off )
#pragma optimize( "", off )
	void __stdcall RemoteWindowThread(ProcessData ProcessData)
	{
		auto pRegisterClassA = (decltype(&RegisterClassA))ProcessData.RegisterClassA;
		auto pCreateWindowExA = (decltype(&CreateWindowExA))ProcessData.CreateWindowExA;
		auto pDefWindowProcA = (decltype(&DefWindowProcA))ProcessData.DefWindowProcA;
				

		WNDCLASSA wc;

		wc.lpfnWndProc = pDefWindowProcA;
		wc.hInstance = (HINSTANCE)ProcessData.AddressToUseAsModule;
		wc.lpszClassName = ProcessData.WindowClass;

		if (!pRegisterClassA(&wc))
			return; //__fastfail(1);

		HWND hwnd = pCreateWindowExA(
			0,
			ProcessData.WindowClass,
			ProcessData.WindowTitle,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			NULL,
			NULL,
			(HINSTANCE)(HINSTANCE)ProcessData.AddressToUseAsModule,
			NULL
		);

		if (!hwnd)
			return; // __fastfail(2);

		/* if the thread exits the window dies too LOL */
		/*
		int WSleep = 0;
		while (true)
		{
			WSleep++;
			
			if (WSleep == -1) break;
		}
		*/

		reinterpret_cast<decltype(&Sleep)>(ProcessData.Sleep)(4000);

		return;
	}

	bool InjectRemoteWindow(const char* WindowTitle, const char* WindowClass, HANDLE ProcessHandle)
	{
		ProcessData RemoteProcessData = ProcessData();

		size_t TitleAllocationSize = strlen(WindowTitle) + 1;
		size_t ClassAllocationSize = strlen(WindowClass) + 1;

		printf("TitleAllocationSize: 0x%X!\r\n", TitleAllocationSize);
		printf("ClassAllocationSize: 0x%X!\r\n", ClassAllocationSize);

		void* TitleAllocationRemote = VirtualAllocEx(ProcessHandle, nullptr, TitleAllocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!TitleAllocationRemote) return false;

		printf("TitleAllocationRemote: %p!\r\n", TitleAllocationRemote);

		void* ClassAllocationRemote = VirtualAllocEx(ProcessHandle, nullptr, ClassAllocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!ClassAllocationRemote) return false;

		printf("ClassAllocationRemote: %p!\r\n", ClassAllocationRemote);

		void* RemoteProcessDataAllocation = VirtualAllocEx(ProcessHandle, nullptr, sizeof(ProcessData), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!RemoteProcessDataAllocation) return false;

		printf("RemoteProcessDataAllocation: %p!\r\n", RemoteProcessDataAllocation);

		void* RemoteCodeAddress = VirtualAllocEx(ProcessHandle, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!RemoteCodeAddress) return false;

		printf("RemoteCodeAddress: %p!\r\n", RemoteCodeAddress);

		RemoteProcessData.CreateWindowExA = reinterpret_cast<void*>(&CreateWindowExA);
		RemoteProcessData.WindowClass = (const char*)ClassAllocationRemote;
		RemoteProcessData.WindowTitle = (const char*)TitleAllocationRemote;
		RemoteProcessData.DefWindowProcA = &DefWindowProcA;
		RemoteProcessData.RegisterClassA = &RegisterClassA;
		RemoteProcessData.Sleep = &Sleep;
		RemoteProcessData.AddressToUseAsModule = GetModuleHandleA("ntdll.dll"); /* lmao */

		printf("writing class naes.\r\n");

		if (!WriteProcessMemory(ProcessHandle, TitleAllocationRemote, WindowTitle, TitleAllocationSize, nullptr))
		{
			printf("failed to write remote window title.\r\n");

			return false;
		}


		if (!WriteProcessMemory(ProcessHandle, ClassAllocationRemote, WindowClass, ClassAllocationSize, nullptr))
		{
			printf("failed to write remote cass.\r\n");

			return false;
		}

		printf("wrote class names.\r\n");

		if (!WriteProcessMemory(ProcessHandle, RemoteProcessDataAllocation, &RemoteProcessData, sizeof(ProcessData), nullptr)) return false;

		/* will likely return partial completion who cares. */
		WriteProcessMemory(ProcessHandle, RemoteCodeAddress, &RemoteWindowThread, 0x1000, nullptr);

		HANDLE Thread = CreateRemoteThread(ProcessHandle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(RemoteCodeAddress), RemoteProcessDataAllocation, 0, nullptr);
		if (!Thread || Thread == INVALID_HANDLE_VALUE)
		{
			printf("failed to create thread.\r\n");

			return false;
		}

		/* just to be safe-ish. */
		Sleep(2000);

		if (!FindWindowA(WindowClass, WindowTitle))
			return false;

		return true;

		//return TerminateThread(Thread, STATUS_ACCESS_VIOLATION);
	}

	bool IsCorrectTargetArchitecture(HANDLE hProc) {
		BOOL bTarget = FALSE;
		if (!IsWow64Process(hProc, &bTarget)) {
			printf("Can't confirm target process architecture: 0x%X\n", GetLastError());
			return false;
		}

		BOOL bHost = FALSE;
		IsWow64Process(GetCurrentProcess(), &bHost);

		return (bTarget == bHost);
	}

	DWORD GetProcessIdByName(const wchar_t* ProcessName)
	{
		DWORD pid = 0;

		HANDLE Snapshit = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (Snapshit == INVALID_HANDLE_VALUE) return 0;

		PROCESSENTRY32W ProcessEntry;
		ProcessEntry.dwSize = sizeof(PROCESSENTRY32W);

		if (Process32FirstW(Snapshit, &ProcessEntry)) {
			do {
				if (wcsicmp(ProcessEntry.szExeFile, ProcessName) == 0)
				{
					pid = ProcessEntry.th32ProcessID;
					break;
				}
			} while (Process32NextW(Snapshit, &ProcessEntry));
		}

		CloseHandle(Snapshit);
		return pid;
	}

	struct ProcessCandidate
	{
		DWORD pid;
		HWND  hwnd;
	};

	std::vector<ProcessCandidate> g_candidates;

	BOOL __stdcall EnumWindowCollect(HWND hWnd, LPARAM lParam)
	{
		DWORD pid = 0;
		if (!GetWindowThreadProcessId(hWnd, &pid) || !pid)
			return TRUE;

		HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
		if (!hProc) return TRUE;

		CloseHandle(hProc);

		g_candidates.push_back({ pid, hWnd });

		return TRUE;
	}

	HANDLE GetRandomValidProcess()
	{
		g_candidates.clear();

		EnumWindows(EnumWindowCollect, 0);

		if (g_candidates.empty())
			return NULL;

		std::random_device rd;
		std::mt19937 gen(rd());

		std::uniform_int_distribution<size_t> dist(0, g_candidates.size() - 1);

		const auto& chosen = g_candidates[dist(gen)];

		HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, chosen.pid);
		if (!hProc || hProc == INVALID_HANDLE_VALUE) return NULL;

		return hProc;
	}
}


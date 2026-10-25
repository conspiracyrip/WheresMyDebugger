#pragma once
#include <windows.h>
#include <iostream>
#include <TlHelp32.h>
#include <Psapi.h>
#include <vector>
#include <random>

#pragma comment(lib, "psapi.lib")

namespace WheresMyDebugger
{
	struct ProcessData
	{
		const char* WindowTitle;
		const char* WindowClass;
		void* CreateWindowExA;
		void* RegisterClassA;
		void* DefWindowProcA;
		void* GetMessageA;
		void* TranslateMessage;
		void* DispatchMessageA;
		void* AddressToUseAsModule;
	};

#pragma runtime_checks( "", off )
#pragma optimize( "", off )
#pragma code_seg(push, ".rwtext")

	__declspec(safebuffers, noinline)
	DWORD __stdcall RemoteWindowThread(void* lpParameter)
	{
		ProcessData* ProcessDataPtr = (ProcessData*)lpParameter;
		if (!ProcessDataPtr) return 1;

		auto pRegisterClassA = (decltype(&RegisterClassA))ProcessDataPtr->RegisterClassA;
		auto pCreateWindowExA = (decltype(&CreateWindowExA))ProcessDataPtr->CreateWindowExA;
		auto pDefWindowProcA = (decltype(&DefWindowProcA))ProcessDataPtr->DefWindowProcA;
		auto pGetMessageA = (decltype(&GetMessageA))ProcessDataPtr->GetMessageA;
		auto pTranslateMessage = (decltype(&TranslateMessage))ProcessDataPtr->TranslateMessage;
		auto pDispatchMessageA = (decltype(&DispatchMessageA))ProcessDataPtr->DispatchMessageA;

		WNDCLASSA wc = {};

		wc.lpfnWndProc = pDefWindowProcA;
		wc.hInstance = (HINSTANCE)ProcessDataPtr->AddressToUseAsModule;
		wc.lpszClassName = ProcessDataPtr->WindowClass;

		if (!pRegisterClassA(&wc))
			return 2;

		HWND hwnd = pCreateWindowExA(
			0,
			ProcessDataPtr->WindowClass,
			ProcessDataPtr->WindowTitle,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			NULL,
			NULL,
			(HINSTANCE)ProcessDataPtr->AddressToUseAsModule,
			NULL
		);

		if (!hwnd)
			return 3;

		/* actually serve the window */
		MSG Msg;
		while (pGetMessageA(&Msg, NULL, 0, 0) > 0)
		{
			pTranslateMessage(&Msg);
			pDispatchMessageA(&Msg);
		}

		return 0;
	}

	/* just so we know how many bytes to copy the __debugbreak spam prevents msvc from folding this into some other ret */
	__declspec(noinline)
	void __stdcall RemoteWindowThreadEnd()
	{
		__debugbreak(); __debugbreak(); __debugbreak();
		__debugbreak(); __debugbreak();
	}

#pragma code_seg(pop)
#pragma optimize( "", on )
#pragma runtime_checks( "", restore )
	size_t RemoteWindowThreadSize()
	{
		return (size_t)((const BYTE*)&RemoteWindowThreadEnd - (const BYTE*)&RemoteWindowThread);
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
	// usually wouldnt matter, but on non x64 im pretty sure it does 
	void* GetRemoteNtdllBase(HANDLE ProcessHandle)
	{
		HMODULE Modules[1024];
		DWORD Needed = 0;

		if (!EnumProcessModules(ProcessHandle, Modules, sizeof(Modules), &Needed))
			return nullptr;

		for (DWORD i = 0; i < Needed / sizeof(HMODULE); i++)
		{
			wchar_t ModuleName[MAX_PATH] = {};
			if (GetModuleBaseNameW(ProcessHandle, Modules[i], ModuleName, MAX_PATH) &&
				_wcsicmp(ModuleName, L"ntdll.dll") == 0)
			{
				return (void*)Modules[i];
			}
		}

		return nullptr;
	}

	struct RemoteWindowState
	{
		HANDLE ProcessHandle;
		HANDLE Thread;
		void* TitleAllocationRemote;
		void* ClassAllocationRemote;
		void* RemoteProcessDataAllocation;
		void* RemoteCodeAddress;
	};

	bool InjectRemoteWindow(const char* WindowTitle, const char* WindowClass, HANDLE ProcessHandle, RemoteWindowState* OutState = nullptr)
	{
		if (!ProcessHandle) return false;

		//didnt call this..
		if (!IsCorrectTargetArchitecture(ProcessHandle))
		{
			printf("bad architecture!\r\n");
			return false;
		}

		ProcessData RemoteProcessData = ProcessData();

		size_t TitleAllocationSize = strlen(WindowTitle) + 1;
		size_t ClassAllocationSize = strlen(WindowClass) + 1;
		size_t CodeAllocationSize = RemoteWindowThreadSize();

		printf("TitleAllocationSize: 0x%X!\r\n", TitleAllocationSize);
		printf("ClassAllocationSize: 0x%X!\r\n", ClassAllocationSize);
		printf("CodeAllocationSize: 0x%X!\r\n", CodeAllocationSize);

		// alloc properly
		void* TitleAllocationRemote = VirtualAllocEx(ProcessHandle, nullptr, TitleAllocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!TitleAllocationRemote) return false;

		printf("TitleAllocationRemote: %p!\r\n", TitleAllocationRemote);

		void* ClassAllocationRemote = VirtualAllocEx(ProcessHandle, nullptr, ClassAllocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!ClassAllocationRemote) return false;

		printf("ClassAllocationRemote: %p!\r\n", ClassAllocationRemote);

		void* RemoteProcessDataAllocation = VirtualAllocEx(ProcessHandle, nullptr, sizeof(ProcessData), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!RemoteProcessDataAllocation) return false;

		printf("RemoteProcessDataAllocation: %p!\r\n", RemoteProcessDataAllocation);
		
		void* RemoteCodeAddress = VirtualAllocEx(ProcessHandle, nullptr, CodeAllocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!RemoteCodeAddress) return false;

		printf("RemoteCodeAddress: %p!\r\n", RemoteCodeAddress);

		RemoteProcessData.CreateWindowExA = reinterpret_cast<void*>(&CreateWindowExA);
		RemoteProcessData.WindowClass = (const char*)ClassAllocationRemote;
		RemoteProcessData.WindowTitle = (const char*)TitleAllocationRemote;
		RemoteProcessData.DefWindowProcA = &DefWindowProcA;
		RemoteProcessData.RegisterClassA = &RegisterClassA;
		RemoteProcessData.GetMessageA = &GetMessageA;
		RemoteProcessData.TranslateMessage = &TranslateMessage;
		RemoteProcessData.DispatchMessageA = &DispatchMessageA;
		RemoteProcessData.AddressToUseAsModule = GetRemoteNtdllBase(ProcessHandle);

		if (!RemoteProcessData.AddressToUseAsModule)
		{
			printf("could not get remote ntdll.\r\n");
			return false;
		}

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

		if (!WriteProcessMemory(ProcessHandle, RemoteCodeAddress, &RemoteWindowThread, CodeAllocationSize, nullptr)) return false;

		/* ARM requires this (i wanna be cool and compatible nigga)*/
		FlushInstructionCache(ProcessHandle, RemoteCodeAddress, CodeAllocationSize);

		HANDLE Thread = CreateRemoteThread(ProcessHandle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(RemoteCodeAddress), RemoteProcessDataAllocation, 0, nullptr);
		if (!Thread || Thread == INVALID_HANDLE_VALUE)
		{
			printf("failed to create thread.\r\n");
			return false;
		}

		/* false positive fixed */
		DWORD TargetPid = GetProcessId(ProcessHandle);
		HWND FoundWindow = nullptr;

		for (int Attempt = 0; Attempt < 100; Attempt++)
		{
			FoundWindow = FindWindowA(WindowClass, WindowTitle);

			if (FoundWindow)
			{
				DWORD OwnerPid = 0;
				GetWindowThreadProcessId(FoundWindow, &OwnerPid);

				if (OwnerPid == TargetPid)
					break;

				FoundWindow = nullptr;
			}

			Sleep(50);
		}

		if (!FoundWindow)
		{
			printf("window never appeared in target.\r\n");

			TerminateThread(Thread, 0);
			WaitForSingleObject(Thread, 2000);
			CloseHandle(Thread);

			return false;
		}

		if (OutState)
		{
			OutState->ProcessHandle = ProcessHandle;
			OutState->Thread = Thread;
			OutState->TitleAllocationRemote = TitleAllocationRemote;
			OutState->ClassAllocationRemote = ClassAllocationRemote;
			OutState->RemoteProcessDataAllocation = RemoteProcessDataAllocation;
			OutState->RemoteCodeAddress = RemoteCodeAddress;
		}
		else
		{
			/*  we just drop our handle */
			CloseHandle(Thread);
		}

		return true;
	}

	void UninjectRemoteWindow(RemoteWindowState& State)
	{
		if (State.Thread)
		{
			PostThreadMessageW(GetThreadId(State.Thread), WM_QUIT, 0, 0);

			if (WaitForSingleObject(State.Thread, 2000) == WAIT_TIMEOUT)
				TerminateThread(State.Thread, 0);

			CloseHandle(State.Thread);
			State.Thread = nullptr;
		}

		if (State.TitleAllocationRemote) { VirtualFreeEx(State.ProcessHandle, State.TitleAllocationRemote, 0, MEM_RELEASE); State.TitleAllocationRemote = nullptr; }
		if (State.ClassAllocationRemote) { VirtualFreeEx(State.ProcessHandle, State.ClassAllocationRemote, 0, MEM_RELEASE); State.ClassAllocationRemote = nullptr; }
		if (State.RemoteProcessDataAllocation) { VirtualFreeEx(State.ProcessHandle, State.RemoteProcessDataAllocation, 0, MEM_RELEASE); State.RemoteProcessDataAllocation = nullptr; }
		if (State.RemoteCodeAddress) { VirtualFreeEx(State.ProcessHandle, State.RemoteCodeAddress, 0, MEM_RELEASE); State.RemoteCodeAddress = nullptr; }
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
		// not all access because i dont like having an all access handle open - ellie
		HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
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

		// not all access because i dont like having an all access handle open - ellie
		HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, chosen.pid);
		if (!hProc || hProc == INVALID_HANDLE_VALUE) return NULL;

		return hProc;
	}
}

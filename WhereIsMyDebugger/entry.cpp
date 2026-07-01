#include <windows.h>
#include <iostream>


#include "WhereTheFuckIsMyDebugger.h"


int main()
{

	HANDLE TargetProcess1 = WheresMyDebugger::GetRandomValidProcess();
	HANDLE TargetProcess2 = WheresMyDebugger::GetRandomValidProcess();
	HANDLE TargetProcess3 = WheresMyDebugger::GetRandomValidProcess();

	
	if (!WheresMyDebugger::InjectRemoteWindow("IDA: Quick start", "Qt5153QTQWindowIcon", TargetProcess1))
		printf("debugger detected (idx: 1, handle: 0x%X).\r\n", TargetProcess1);
	else
		printf("debugger NOT detected (idx: 1, handle: 0x%X).\r\n", TargetProcess1);

	
	if (!WheresMyDebugger::InjectRemoteWindow("GDI+ Hook Window Class", "GDI+ Window (CFF Explorer.exe)", TargetProcess2))
		printf("debugger detected (idx: 2, handle: 0x%X).\r\n", TargetProcess2);
	else
		printf("debugger NOT detected (idx: 2, handle: 0x%X).\r\n", TargetProcess2);

	if (!WheresMyDebugger::InjectRemoteWindow("#8601", "CFF Explorer", TargetProcess3))
		printf("debugger detected (idx: 3, handle: 0x%X).\r\n", TargetProcess3);
	else
		printf("debugger NOT detected (idx: 3, handle: 0x%X).\r\n", TargetProcess3);
		

	if (!WheresMyDebugger::InjectRemoteWindow("IDA: Quick start", "Qt5153QTQWindowIcon", GetCurrentProcess()))
		printf("debugger detected (idx: 0)\r\n");
	else
		printf("debugger NOT detected (idx: 0)\r\n");

	Sleep(2500);
}
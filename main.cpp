#include <string>
#include "Main.h"
#include <iostream>
#include <Windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Sensors.h>
#include <Ntddvdeo.h>
#include <thread>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Sensors;

HKEY displayKey = nullptr;
DWORD IsAutobrightnessOn = 0;

void CheckForBrightnessControlChange()
{
	HANDLE hEvent = CreateEvent(NULL, true, false, NULL);

	RegNotifyChangeKeyValue(displayKey, true, REG_NOTIFY_CHANGE_LAST_SET, hEvent, true);

	while (true)
	{
		if (WaitForSingleObject(hEvent, INFINITE) == WAIT_FAILED)
		{
			exit(0);
		}

		DWORD type = 0;
		DWORD size = sizeof(IsAutobrightnessOn);

		RegQueryValueEx(displayKey, L"IsAutobrightnessOn", NULL, &type, (LPBYTE)&IsAutobrightnessOn, &size);

		RegNotifyChangeKeyValue(displayKey, false, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_SECURITY, hEvent, true);
	}
}


//
// Subject: Change screen brightness
//
// Parameters:
//
//             brightness:
//
// Returns: void
//
void UpdateDisplayBrightness(int brightness)
{
	if (!IsAutobrightnessOn)
	{
		return;
	}

	typedef struct _DISPLAY_BRIGHTNESS {
		UCHAR ucDisplayPolicy;
		UCHAR ucACBrightness;
		UCHAR ucDCBrightness;
	} DISPLAY_BRIGHTNESS, * PDISPLAY_BRIGHTNESS;

	DISPLAY_BRIGHTNESS _displayBrightness;

	_displayBrightness.ucDisplayPolicy = 0;
	_displayBrightness.ucACBrightness = brightness;
	_displayBrightness.ucDCBrightness = brightness;

	DWORD ret = NULL;

	DWORD nOutBufferSize = sizeof(_displayBrightness);
	HANDLE h = CreateFile(L"\\\\.\\LCD",
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0, NULL);

	if (h == INVALID_HANDLE_VALUE)
	{
		return;
	}

	if (!DeviceIoControl(h, IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS, (DISPLAY_BRIGHTNESS*)&_displayBrightness, nOutBufferSize, NULL, 0, &ret, NULL))
	{
		return;
	}
}

int previousstate = -20;

bool IsRadicallyDifferent(int targetBrightness)
{
	if (abs(targetBrightness - previousstate) >= 20)
	{
		previousstate = targetBrightness;
		UpdateDisplayBrightness(targetBrightness);
		return TRUE;
	}
	return FALSE;
}

int _tmain(int argc, TCHAR* argv[])
{
	SERVICE_TABLE_ENTRY ServiceTable[] =
	{
		{SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
		{NULL, NULL}
	};

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
	{
		return GetLastError();
	}

	return 0;
}


VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
	DWORD Status = E_FAIL;
	HANDLE hThread;

	g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

	if (g_StatusHandle == NULL)
	{
		goto EXIT;
	}

	// Tell the service controller we are starting
	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

	/*
	 * Perform tasks neccesary to start the service here
	 */

	// Create stop event to wait on later.
	g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_ServiceStopEvent == NULL)
	{
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		goto EXIT;
	}

	// Tell the service controller we are started
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

	// Start the thread that will perform the main task of the service
	hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

	// Wait until our worker thread exits effectively signaling that the service needs to stop
	WaitForSingleObject(hThread, INFINITE);

	/*
	 * Perform any cleanup tasks
	 */
	CloseHandle(g_ServiceStopEvent);

	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

EXIT:
	return;
}


VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
	switch (CtrlCode)
	{
	case SERVICE_CONTROL_STOP:

		if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			break;

		/*
		 * Perform tasks neccesary to stop the service here
		 */

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		g_ServiceStatus.dwWin32ExitCode = 0;
		g_ServiceStatus.dwCheckPoint = 4;

		// This will signal the worker thread to start shutting down
		SetEvent(g_ServiceStopEvent);

		break;

	default:
		break;
	}
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
	init_apartment();

	RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\DisplayEnhancementService\\State\\CMO00110_09_07D9_98", 0, KEY_NOTIFY | KEY_READ, &displayKey);

	DWORD type = 0;
	DWORD size = sizeof(IsAutobrightnessOn);

	RegQueryValueEx(displayKey, L"IsAutobrightnessOn", NULL, &type, (LPBYTE)&IsAutobrightnessOn, &size);

	//
	// Get the default light sensor on the system
	//
	LightSensor sensor = LightSensor::GetDefault();

	//
	// If no sensor is found return 1
	//
	if (sensor == NULL)
	{
		return 1;
	}

	//
	// Update current brightness on startup
	//
	float currentlux = sensor.GetCurrentReading().IlluminanceInLux();
	int requestedBrightness = floor(currentlux / 10);

	if (requestedBrightness > 100)
		requestedBrightness = 100;

	previousstate = requestedBrightness;
	UpdateDisplayBrightness(requestedBrightness);

	//
	// Subscribe to sensor events
	//
	sensor.ReadingChanged([](IInspectable const& /*sender*/, LightSensorReadingChangedEventArgs const& args)
		{
			LightSensorReading readings = args.Reading();
			float lux = readings.IlluminanceInLux();

			int targetBrightness = floor(lux / 10);

			if (targetBrightness > 100)
				targetBrightness = 100;

			IsRadicallyDifferent(targetBrightness);
		});

	std::thread t1(CheckForBrightnessControlChange);
	t1.join();

	while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0)
	{
		Sleep(1000);
	}

	return ERROR_SUCCESS;
}
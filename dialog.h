#ifndef DIALOG_H
#define DIALOG_H


#define ID_INPUT_FIELD  0x67
#define ID_LABEL_FIELD  0x66

char g_dialogOutput[256];
char g_userConfirmed[256] = {0};
char g_promptMessage[256] = "Enter something:";
char g_userConfirmed[256] = "";
char g_inputBuffer[256];

bool isWindowsResourceAllocated = false;

HDC g_deviceContext;
HWND g_windowHandle;

#endif // DIALOG_H
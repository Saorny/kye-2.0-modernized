#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <dos.h>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>
#include <cstring>
#include <system_error>
#include <cstddef>
#include <windows.h>
#include <tuple>
#include <stdexcept>
#include "dialog.h"
#include "graph.h"

static char g_inputDialogLabel[0x80];   // at 0x2BE0 (size guessed)
static char g_inputDialogText[0x80];    // at 0x2B90 (size guessed)
static uint8_t g_userConfirmed;         // g_userConfirmed

// Near pointers in original: offsets into DS
static char* dsPtr(uint16_t off) { return reinterpret_cast<char*>(off); }

HINSTANCE g_hInstance = nullptr;

void showDialogBox(void) {
    // Simule une boîte où on demande à l'utilisateur de confirmer ou modifier la chaîne
    printf("%s\n> ", g_inputBuffer);
    fgets(g_dialogOutput, sizeof(g_dialogOutput), stdin);

    // Nettoie le saut de ligne
    size_t len = strlen(g_dialogOutput);
    if (len > 0 && g_dialogOutput[len - 1] == '\n') {
        g_dialogOutput[len - 1] = '\0';
    }

    strcpy(g_userConfirmed, g_dialogOutput);
}

int handleInputDialog(uint16_t inputLabelPtr, uint16_t outputBufferPtr) {
    // Copy label -> global label buffer
    std::strcpy(g_inputDialogLabel, dsPtr(inputLabelPtr));

    // Copy existing text -> global edit buffer
    std::strcpy(g_inputDialogText, dsPtr(outputBufferPtr));

    // Show dialog (DLG_INP1) using DLG_INP1_FUNC
    // (original uses MakeProcInstance / DialogBox / FreeProcInstance)
    showDlgInp1Modal(); // wrapper around DialogBox

    // Copy edited text back to caller buffer
    std::strcpy(dsPtr(outputBufferPtr), g_inputDialogText);

    // Return per g_userConfirmed
    return (g_userConfirmed != 0) ? 0 : 2;
}

int handleInputDialog(uint16_t labelId, char* outputBuffer)
{
    const char* label = getUiStringById(labelId);
    return handleInputDialog(label, outputBuffer);
}

int handleInputDialog(const char* labelInput, char* outputBuffer) {
    // 1. Copier l'entrée vers buffer global (comme movsw vers 2BE0h)
    strncpy(g_inputBuffer, labelInput, sizeof(g_inputBuffer) - 1);
    g_inputBuffer[sizeof(g_inputBuffer) - 1] = '\0';

    // 2. Copier la valeur actuelle vers buffer modifiable (2B90h)
    strncpy(g_dialogOutput, outputBuffer, sizeof(g_dialogOutput) - 1);
    g_dialogOutput[sizeof(g_dialogOutput) - 1] = '\0';

    // 3. Affiche la boîte de dialogue simulée
    showDialogBox();

    // 4. Si utilisateur a confirmé, recopier résultat dans outputBuffer
    if (g_userConfirmed) {
        strncpy(outputBuffer, g_dialogOutput, 256);
        outputBuffer[255] = '\0';
        return 2;
    }

    // Annulé
    return 0;
}

INT_PTR CALLBACK DlgInputProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
        // Met le texte initial
        SetDlgItemTextA(hDlg, ID_LABEL_FIELD, g_promptMessage);
        SetDlgItemTextA(hDlg, ID_INPUT_FIELD, g_userConfirmed);
        // Focus sur le champ de saisie
        SetFocus(GetDlgItem(hDlg, ID_INPUT_FIELD));
        return FALSE; // FALSE = Windows gère le focus

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1:  // OK
            GetDlgItemTextA(hDlg, ID_INPUT_FIELD, g_userConfirmed, sizeof(g_userConfirmed));
            EndDialog(hDlg, IDOK);
            return TRUE;

        case 2:  // Cancel
            // On remet une valeur par défaut (dans l’asm, c’était une chaîne en 0x0443)
            strcpy(g_userConfirmed, "DEFAULT");
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void releaseDialogResources() {
    if (g_hasDeviceContext) {
        ReleaseDC(g_hdc2, g_hwnd);
        g_hasDeviceContext = false;
    }
}

static const char* lookupString(uint16_t id)
{
    return g_stringTable[id];
}


void showFileMessage(uint16_t messageId)
{
    const char* message = lookupString(messageId);
    if (!message) message = "";

    const char* filename = std::strrchr(message, '\\');
    filename = filename ? (filename + 1) : message;

    MessageBoxA(GetDesktopWindow(), message, filename, MB_ICONINFORMATION | MB_OK);
}

void showFinalDialog() {
    FARPROC dlgProc = MAKEPROCINSTANCE(DLG_OK_FUNC, g_hInstance);

    DialogBox(
        g_hInstance,
        "DLG_LAST",
        reinterpret_cast<HWND>(g_hwnd),  // ← probablement une erreur !
        reinterpret_cast<DLGPROC>(dlgProc)
    );

    FREEPROCINSTANCE(dlgProc);
}

void showLevelDoneDialog() {
    FARPROC dlgProc = MAKEPROCINSTANCE(DLG_LVLDUN_FUNC, g_hInstance);

    DialogBox(
        g_hInstance,
        "DLG_DUN1",
        reinterpret_cast<HWND>(g_hwnd),  // ⚠️ attention ici aussi
        reinterpret_cast<DLGPROC>(dlgProc)
    );

    FREEPROCINSTANCE(dlgProc);
}

void showGameOverDialog() {
    FARPROC dlgProc = MAKEPROCINSTANCE(DLG_KYESGONE_FUNC, g_hInstance);

    DialogBox(
        g_hInstance,
        "DLG_GON1",
        reinterpret_cast<HWND>(g_hwnd),  // ⚠️ encore douteux
        reinterpret_cast<DLGPROC>(dlgProc)
    );

    FREEPROCINSTANCE(dlgProc);
}

void showWhatDialog()
{
    bool done = false;
    SDL_Event e;

    while (!done) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                done = true;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                e.type == SDL_EVENT_KEY_DOWN) {
                done = true;
            }
        }

        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 200);
        SDL_RenderFillRect(g_renderer, nullptr);
        drawWhatDialogUI();

        SDL_RenderPresent(g_renderer);
        SDL_Delay(16);
    }
}


const char* getUiStringById(uint16_t id)
{
    switch (id) {
        case 0x88: return "MSG_0088";
        case 0x91: return "MSG_0091";
        case 0xC8: return "MSG_00C8";
        case 0xE7: return "MSG_00E7";
        case 0x385: return "MSG_0385";
        case 0x386: return "MSG_0386";
        case 0x38D: return "MSG_038D";
        default:    return "";
    }
}
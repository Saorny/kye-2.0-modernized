#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <dos.h>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>
#include <system_error>
#include <cstddef>
#include <windows.h>
#include <tuple>
#include <stdexcept>
#include "dialog.h"

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
    if (isWindowsResourceAllocated) {
        ReleaseDC(g_windowHandle, g_deviceContext);
        isWindowsResourceAllocated = false;
    }
}

void showFileMessage(const char* message) {
    // Cherche le dernier backslash pour isoler le nom de fichier
    const char* filename = strrchr(message, '\\');
    if (filename) {
        filename++;  // Avancer après le '\'
    } else {
        filename = message;  // Pas de répertoire dans la chaîne
    }

    // Appel de MessageBoxA (version ANSI, compatible avec const char*)
    MessageBoxA(GetDesktopWindow(), message, filename, MB_ICONINFORMATION | MB_OK);
}

void showFinalDialog() {
    FARPROC dlgProc = MAKEPROCINSTANCE(DLG_OK_FUNC, hInstanceTmp);

    DialogBox(
        hInstanceTmp,
        "DLG_LAST",
        reinterpret_cast<HWND>(g_deviceContext),  // ← probablement une erreur !
        reinterpret_cast<DLGPROC>(dlgProc)
    );

    FREEPROCINSTANCE(dlgProc);
}

void showLevelDoneDialog() {
    FARPROC dlgProc = MAKEPROCINSTANCE(DLG_LVLDUN_FUNC, hInstanceTmp);

    DialogBox(
        hInstanceTmp,
        "DLG_DUN1",
        reinterpret_cast<HWND>(g_deviceContext),  // ⚠️ attention ici aussi
        reinterpret_cast<DLGPROC>(dlgProc)
    );

    FREEPROCINSTANCE(dlgProc);
}

void showGameOverDialog() {
    FARPROC dlgProc = MAKEPROCINSTANCE(DLG_KYESGONE_FUNC, hInstanceTmp);

    DialogBox(
        hInstanceTmp,
        "DLG_GON1",
        reinterpret_cast<HWND>(g_deviceContext),  // ⚠️ encore douteux
        reinterpret_cast<DLGPROC>(dlgProc)
    );

    FREEPROCINSTANCE(dlgProc);
}

void showWhatDialog() {
    FARPROC dlgProc = MAKEPROCINSTANCE(DLG_OK_FUNC, hInstanceTmp);

    // Affiche une boîte de dialogue modale
    DIALOGBOX(hInstanceTmp, "DLG_WHAT", g_deviceContext, dlgProc);

    // Libère la procédure une fois la boîte fermée
    FREEPROCINSTANCE(dlgProc);
}

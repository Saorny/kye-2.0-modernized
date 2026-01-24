#ifndef DIALOG_H
#define DIALOG_H


#define ID_INPUT_FIELD  0x67
#define ID_LABEL_FIELD  0x66

char g_dialogOutput[256];
char g_userConfirmed[256] = {0};
char g_promptMessage[256] = "Enter something:";
char g_userConfirmed[256] = "";
char g_inputBuffer[256];

bool g_hasDeviceContext = false;

HDC g_hwnd = nullptr;
HWND g_hdc2 = nullptr;
bool g_hasDeviceContext = false;

void initializeWindowHandleIfNeeded();
void releaseDialogResources();
void showFinalDialog();
void showLevelDoneDialog();
void showGameOverDialog();
void showWhatDialog();
int handleInputDialog(uint16_t labelId, char* outputBuffer);
int handleInputDialog(const char* labelInput, char* outputBuffer);
int handleInputDialog(uint16_t labelId, std::string& output);
const char* getUiStringById(uint16_t id);
void showFileMessage(uint16_t messageId);

enum class DialogMsg : uint16_t {
    InitDialog = 0x0110,
    Command    = 0x0111,
};

struct OpenDialogState {
    bool accepted = false;

    // buffers / champs logiques (remplacent ds:050C, ds:060C, ds:068C, ds:0699, etc.)
    std::string filenameEdit;     // ce que l’utilisateur tape (edit 0x191)
    std::string currentDir;       // répertoire courant
    std::string selectedMask;     // ex: "*.kye"
    std::string derivedDefaultExt; // ex: ".kye" (ou "kye" selon ton impl)
};

struct OpenFileDialogResult {
    bool accepted = false;
    std::string selectedDir;   // ex: "C:\\GAMES\\KYE\\LEVELS"
    std::string filename;      // ex: "LEVEL1.KYE"
};

#endif // DIALOG_H
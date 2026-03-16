#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <vector>
#include <string>
#include <system_error>
#include <cstddef>
#include <tuple>
#include <stdexcept>
#include <cstring>
#include "file.h"
#include "error.h"
#include "system.h"
#include "util.h"
#include "graph.h"
#include "game.h"

static FileLike* findFreeFileSlot();

static FileLike* prepareAndOpenFile(
    FileLike* file,
    uint16_t adviceType,
    const char* mode,
    const char* filepath
);

static FileOpenMode prepareFileInfo(
    const char* mode,
    uint16_t* attr,
    uint16_t* flags
);

static int dosCreateFile(const char* filename, int attributes);
static int validateFile(FileLike* file, int param);

static int createFile(const char* filename, int attributes);
static int openFile(const char* filename, uint16_t flags);
static int closeFile(int fd);

static int readTextBuffer(int fd, char* buffer, int size);
static int writeTextBuffer(int fd, const char* buffer, uint16_t size);

static int fillTextBuffer(FileLike* file);
static int flushTextBuffer(FileLike* file);

static int readLine(char* outputBuffer, int maxLen, FileLike* file);

static int moveFilePointer(int fd);

static bool testFileWriteability(int fd);
static void cleanupAllOpenFiles();

static uint16_t writeStringAndAdvance(char*, const char*);
static void callWithAudit(uint32_t);
static void copySecondStringIntoFirst(char*, const char*);
static int writeCharToFile(uint8_t, FileLike*);
static uint16_t prepareAndRelease(void* dest, uint16_t value, uint16_t pad);

namespace fs = std::filesystem;

static constexpr int kLines = 0x14;      // 20
static constexpr int kLineSize = 0x23;   // 35 bytes
static constexpr int kOff_0xCD  = 0xCD;
static constexpr int kOff_0x23  = 0x23;
static constexpr int kOff_0x78  = 0x78;
static constexpr int kBlockSize = kOff_0xCD + (kLines * kLineSize);

int findMatchingLineInFile(const char* targetString) {
    if (fileAccessEnabled == 0)
        return -1;

    FileLike* file = openAndPrepareFileFromSlot(g_selectedFilePath, "r");
    if (!file)
        return -1;

    if (resetAndSeekFile(file, 0) != 0) {
        cleanFile(file);
        return -1;
    }

    char outBuf[0x50] = {0};  // 0x4F + 1
    readLineToBuffer(file, outBuf, 0x4F);

    totalBlockCount = static_cast<uint16_t>(parseSignedDecimalString(outBuf));

    int currentIndex = 1;

    while (currentIndex <= totalBlockCount) {
        char entryBuffer[0x400] = {0};  // var_38E

        if (loadLevelMetaData(file, entryBuffer) < 0) {
            cleanFile(file);
            return -1;
        }

        if (strcmp(entryBuffer, targetString) == 0) {
            cleanFile(file);
            return currentIndex;
        }

        ++currentIndex;
    }

    cleanFile(file);
    return -1;
}

FileLike* openAndPrepareFileFromSlot(const char* filepath, const char* mode) {
    cout << "Opening file " << filepath << endl;
    FileLike* slot = findFreeFileSlot();
    if (!slot) return nullptr;
    return prepareAndOpenFile(slot, 0, mode, filepath);
}

FileLike* findFreeFileSlot() {
    for (int i = 0; i < maxFileCount; ++i) {
        FileLike& file = fileTable[i];
        if (static_cast<int8_t>(file.fd) < 0) {
            return &file;
        }
    }
    return nullptr;
}


FileLike* prepareAndOpenFile(FileLike* file, uint16_t openFlags, const char* adviceType, const char* filepath)
{
    if (!file) return nullptr;
    
    uint16_t requestedFlags = 0;
    uint16_t attributeFlags = 0;

    int mode = prepareFileInfo(adviceType, &attributeFlags, &requestedFlags);
    file->flags = static_cast<uint16_t>(mode);

    if (mode == 0)
    {
        file->fd = -1;
        file->flags = 0;
        return nullptr;
    }

    if (file->fd < 0)
    {
        int finalFlags = attributeFlags | openFlags;

        int handle = openFileHandler(filepath, finalFlags);

        file->fd = static_cast<int16_t>(handle);

        if (handle < 0)
        {
            file->fd = -1;
            file->flags = 0;
            return nullptr;
        }
    }

    int fd = static_cast<int>(file->fd);

    if (getAdviceInfo(fd))
    {
        file->flags |= 0x0200;
    }

    int param = (file->flags & 0x0200) ? 1 : 0;

    if (validateFile(file, param) != 0)
    {
        cleanFile(file);
        return nullptr;
    }

    file->_pad0C = 0;

    return file;
}

int validateFile(FileLike* file, int mode)
{
  if (!file) return -1;

  if (file->self != file) return -1;

  if (mode > 2) return -1;

  if (file->bufferSize > 0x7FFF) return -1;
  {
    const auto addr = reinterpret_cast<std::uintptr_t>(file);
    if (!hasValidatedE86FileOnce && addr == 0x0E86) {
      hasValidatedE86FileOnce = true;
    } else if (!hasValidatedFile_E76 && addr == 0x0E76) {
      hasValidatedFile_E76 = true;
    }
  }

  if (file->bytesRead != 0) {
    resetAndSeekFile(file, 1);
  }

  if (file->flags & kFlagOwnsHeapBuffer) {
    freeLocalMemory(file->buffer);
  }

  file->flags = static_cast<uint16_t>(file->flags & kFlagsClearMaskFFF3);
  file->bufferSize = 0;
  file->bytesRead = 0;

  char* inlineBuf = reinterpret_cast<char*>(file) + 5;
  file->buffer = inlineBuf;
  file->bufferCursor = inlineBuf;

  return 0;
}

int openFileHandler(const std::string& filepath, uint16_t flags)
{
    int initialAttr = fileAttrOp(filepath.c_str(), 0, 0);

    if (initialAttr == -1 && err != 2)
    {
        handleDosError(err);
        return -1;
    }

    if (initialAttr == -1 && err == 2)
    {
        initialAttr = (flags & 0x80) ? 0 : 1;
    }
    else
    {
        if (flags & 0x400)
        {
            handleDosError(0x50);
            return -1;
        }
    }

    // Correspond au test ASM : test si,0F0h
    if (flags & 0x00F0)
    {
        int tmpFd = createFile(filepath.c_str(), 0);
        if (tmpFd < 0)
            return -1;

        closeFile(tmpFd);
    }

    int handle = openFile(filepath.c_str(), flags);
    if (handle < 0)
        return -1;

    uint16_t ioctlInfo = ioctl(handle, 0, 0, 0);

    if (ioctlInfo & 0x80)
    {
        flags |= 0x2000;

        if (flags & 0x8000)
        {
            uint16_t modAttr = (ioctlInfo & 0xFF) | 0x20;
            ioctl(handle, 1, modAttr, 0);
        }
    }
    else if (flags & 0x0200)
    {
        testFileWriteability(handle);
    }

    if ((initialAttr & 1) && (flags & 0x0100) && (flags & 0x00F0))
    {
        fileAttrOp(filepath.c_str(), 1, 1);
    }

    uint16_t newFlags = (flags & 0xF8FF);

    if (flags & 0x0300)
        newFlags |= 0x1000;

    if (!(initialAttr & 1))
        newFlags |= 0x0100;

    fdFlagsRef(handle) = newFlags;

    return handle;
}

int createFile(const char* filename, int attributes) {
    int fd = dosCreateFile(filename, attributes);

    if (fd < 0) {
        handleDosError(fd);  // Même nom que dans l'asm
    }

    return fd;
}

int dosCreateFile(const char* filename, int attributes) {
    // À adapter avec des appels système modernes si besoin
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return -1;  // Simule un "erreur DOS"
    }
    file.close();
    return 3;  // Fausse valeur de handle (DOS commence souvent à 3)
}

int readLineToBuffer(FileLike* file, char* outBuf, int maxLen)
{
    char temp[256];

    if (!readLine(temp, 0xFF, file))
        return -1;

    int count = 0;
    char* src = temp;
    char* dst = outBuf;

    while (*src != '\0' &&
           *src != '\n' &&
           *src != '\r' &&
           count < maxLen)
    {
        *dst++ = *src++;
        count++;
    }

    *dst = '\0';
    return count;
}

void flushAllDirtyTextBuffers() {
    FileLike* file = fileTable; // base address of file table
    int count = 0x14; // 20 entries

    while (count--) {
        uint16_t flags = file->flags & 0x0300;
        if (flags == 0x0300) {
            flushTextBuffer(file);  // sub_6AC6
        }
        file++;
    }
}

int readCharFromTextBuffer(FileLike* file)
{
    if (!file)
        return -1;

    if (file->bytesRead > 0)
    {
        file->bytesRead--;
        unsigned char ch = static_cast<unsigned char>(*file->bufferCursor);
        file->bufferCursor++;
        return ch;
    }

    if (file->bytesRead < 0)
    {
        file->flags |= 0x10;
        return -1;
    }

    if ((file->flags & 0x0110) != 0)
    {
        file->flags |= 0x10;
        return -1;
    }

    if ((file->flags & 0x0001) == 0)
    {
        file->flags |= 0x10;
        return -1;
    }

    file->flags |= 0x80;

    if (file->bufferSize != 0)
    {
        if (fillTextBuffer(file) == 0)
        {
            file->bytesRead--;
            unsigned char ch = static_cast<unsigned char>(*file->bufferCursor);
            file->bufferCursor++;
            return ch;
        }

        file->flags |= 0x10;
        return -1;
    }

    if (file->flags & 0x0200)
        flushAllDirtyTextBuffers();

    char one = 0;
    int n = readTextBuffer(static_cast<int>(file->fd), (char*)&g_pendingChar, 1);

    // std::cout << "READ n=" << n
    //       << " char=" << (int)(unsigned char)g_pendingChar
    //       << " '" << (char)g_pendingChar << "'"
    //       << std::endl;
    if (n == 0)
    {
        int ok = moveFilePointer(static_cast<int>(file->fd));

        if (ok == 1)
        {
            file->flags = static_cast<uint16_t>((file->flags & 0xFE7F) | 0x20);
        }
        else
        {
            file->flags |= 0x10;
        }

        return -1;
    }

    if (g_pendingChar == '\r')
    {
        if ((file->flags & 0x40) == 0)
            return readCharFromTextBuffer(file);
    }

    file->flags &= 0xFFDF;

    return static_cast<unsigned char>(g_pendingChar);
}

int fillTextBuffer(FileLike* file)
{
  if (!file) return kEOF;

  if ((file->flags & FILE_FLAG_REPOSITION) != 0) {
    flushAllDirtyTextBuffers();
  }

  file->bufferCursor = file->buffer;

  const int fd = static_cast<int>(static_cast<int8_t>(file->fd));
  const int n  = readTextBuffer(fd, file->buffer, static_cast<int>(file->bufferSize));
  file->bytesRead = static_cast<int16_t>(n);

  if (n > 0) {
    file->flags = static_cast<uint16_t>(file->flags & FILE_MASK_CLEAR_FFDF);
    return 0;
  }

  if (file->bytesRead == 0) {
    uint16_t ax = file->flags;
    ax = static_cast<uint16_t>(ax & FILE_MASK_CLEAR_FE7F);
    ax = static_cast<uint16_t>(ax | FILE_FLAG_EOF);
    file->flags = ax;
  } else {
    file->bytesRead = 0;
    file->flags = static_cast<uint16_t>(file->flags | FILE_FLAG_IO_ERROR);
  }

  return kEOF;
}

int readAndTrackChar(FileLike* file)
{
    ++file->bytesRead;
    return readCharFromTextBuffer(file);
}

int readLine(char* outputBuffer, int maxLen, FileLike* file)
{
    char* dst = outputBuffer;
    int ch = 0;

    while (true)
    {
        if (--maxLen <= 0)
            break;

        if (--file->bytesRead < 0)
            ch = readAndTrackChar(file);
        else
            ch = (unsigned char)*file->bufferCursor++;

        if (ch == -1)
            break;

        *dst++ = (char)ch;

        if (ch == '\n')
            break;
    }

    if (dst == outputBuffer)
        return 0;

    *dst = '\0';

    if (file->flags & 0x10)
        return 0;

    return (int)(intptr_t)outputBuffer;
}

int flushAllTextBuffers() {
    int flushedCount = 0;

    for (int i = 0; i < maxFileCount; ++i) {
        FileLike& file = fileTable[i];

        // Vérifie si le buffer est ouvert (bit 0) ou modifié (bit 1)
        if (file.flags & 0x0003) {
            flushTextBuffer(&file);
            ++flushedCount;
        }
    }

    return flushedCount;
}

static int flushTextBuffer(FileLike* file) {
    if (!file) {
        flushAllTextBuffers();
        return 0;
    }

    if (file->self != file) {
        return -1;
    }

    const char* inlinePos = reinterpret_cast<const char*>(file) + 5;

    if (file->bytesRead >= 0) {
        if ((file->flags & 0x0008u) == 0u) {
            if (file->bufferCursor != const_cast<char*>(inlinePos)) {
                return 0;
            }
        }

        file->bytesRead = 0;

        if (file->bufferCursor == const_cast<char*>(inlinePos)) {
            return 0;
        }

        file->bufferCursor = file->buffer;
        return 0;
    }

    const int size = static_cast<int>(static_cast<int32_t>(file->bufferSize) + file->bytesRead + 1);

    file->bytesRead = static_cast<int16_t>(file->bytesRead - static_cast<int16_t>(size));
    file->bufferCursor = file->buffer;

    const int written = writeTextBuffer(file->fd, file->buffer, size);

    if (written != size) {
        if ((file->flags & 0x0200u) == 0u) {
            file->flags = static_cast<uint16_t>(file->flags | 0x0010u);
            return -1;
        }
    }

    return 0;
}

int readTextBuffer(int fd, char* buffer, int size) {
    if (fd >= maxFileCount) {
        handleDosError(6);
        return -1;
    }

    if (size < 1) return 0;

    if (fdFlagsRef(fd) & 0x0200)
        return 0;

    while (true) {
        int bytesRead = readFile(fd, buffer, size);
        if (bytesRead < 1)
            return bytesRead;

        if (!(fdFlagsRef(fd) & 0x4000))
            return bytesRead;

        char* dst = buffer;
        char* src = buffer;
        int count = bytesRead;

        while (count--) {
            char c = *src++;
            if (c == 0x1A) {
                moveFilePointerExtended(fd, -count - 1, SEEK_CUR);
                fdFlagsRef(fd) |= 0x0200;
                break;
            } else if (c == '\r') {
                continue;
            } else {
                *dst++ = c;
            }
        }

        const int filteredCount = static_cast<int>(dst - buffer);

        if (filteredCount > 0)
            return filteredCount;

        // uniquement des '\r' lus -> on relit
        // std::cout << "[readTextBuffer] skipped CR-only chunk, retrying\n";
    }
}

int readFile(int fd, void* buffer, uint16_t size) {
    // Vérifie le bit 1 (0x0002)
    if (g_fdFlags[fd] & 0x0002) {
        handleDosError(5); // Access Denied
        return -1;
    }

    // Si un gestionnaire personnalisé est présent
    if (g_customReadHandler && getAdviceInfo(fd)) {
        return g_customReadHandler(buffer, size);
    }

    // Lecture standard
    FILE* file = g_openFileHandles[fd].handle;
    if (!file) {
        handleDosError(6); // Invalid handle
        return -1;
    }

    size_t bytesRead = std::fread(buffer, 1, size, file);
    if (bytesRead < size && std::ferror(file)) {
        handleDosError(1); // Erreur de lecture
        return -1;
    }

    return static_cast<int>(bytesRead);
}

int readNonEmptyLine(FileLike* file, char* buffer, int size)
{
    do {
        if (readLineToBuffer(file, buffer, size) < 0)
            return -1;
    } while (buffer[0] == '\0');

    return 0;
}

int loadLevelMetaData(FileLike* file, char* gridBuffer)
{
    if (readLineToBuffer(file, gridBuffer, 0x21) < 0)
        return -1;
    g_levelPassword = gridBuffer;

    if (readLineToBuffer(file, gridBuffer + 0x23, 0x53) < 0)
        return -1;
    g_levelHintText = gridBuffer + 0x23;

    if (readLineToBuffer(file, gridBuffer + 0x78, 0x53) < 0)
        return -1;
    g_levelVictoryText = gridBuffer + 0x78;

    char* ptr = gridBuffer + 0xCD;

    for (int row = 0; row < GRID_ROWS ; ++row)
    {
        if (readLineToBuffer(file, ptr, 0x21) < 0)
            return -1;

        ptr += 0x23;
    }

    return 0;
}

int writeTextBuffer(int fd, const char* buffer, uint16_t size) {
    constexpr int TEMP_BUFFER_SIZE = 128;
    char temp[TEMP_BUFFER_SIZE];
    int tempLen = 0;
    int written = 0;

    // Vérifie file descriptor valide
    if (fd >= maxFileCount) {
        handleDosError(6);
        return -1;
    }

    if (size < 1) return 0;

    // Mode append (flag 0x0800)
    if (g_openFileHandles[fd].flags & 0x0800) {
        moveFilePointerExtended(fd, 0, 2); // seek end
    }

    // Mode texte ? (flag 0x4000)
    if ((g_openFileHandles[fd].flags & 0x4000) == 0) {
        // Mode binaire
        return writeFile(fd, buffer, size);
    }

    // Sinon, mode texte → transforme \n en \r\n avec bufferisation
    const char* src = buffer;
    int remaining = size;

    while (remaining > 0) {
        char c = *src++;

        if (c == '\n') {
            temp[tempLen++] = '\r';
        }

        temp[tempLen++] = c;
        --remaining;

        // Si buffer plein, on écrit
        if (tempLen >= TEMP_BUFFER_SIZE) {
            int w = writeFile(fd, temp, tempLen);
            if (w != tempLen) return -1;
            written += w;
            tempLen = 0;
        }
    }

    // Écriture finale s’il reste du contenu en buffer
    if (tempLen > 0) {
        int w = writeFile(fd, temp, tempLen);
        if (w != tempLen) return -1;
        written += w;
    }

    return written;
}

int getFileAttributes(const char* filepath, uint16_t* outAttributes) {
    try {
        fs::path path(filepath);
        auto perms = fs::status(path).permissions();

        uint16_t dosAttr = 0;

        if ((perms & fs::perms::owner_write) == fs::perms::none)
            dosAttr |= 0x01; // Read-only

        // Bonus : on pourrait simuler Hidden, System, Archive
        // selon conventions UNIX/Windows

        if (outAttributes)
            *outAttributes = dosAttr;

        return 0;
    } catch (...) {
        handleDosErrorAndReturn(2); // simulation d’erreur : file not found
        return -1;
    }
}

int fileAttrOp(const char* filepath, uint8_t mode, uint16_t attr) {
    try {
        fs::path path(filepath);

        if (mode == 0) {
            // GET FILE ATTRIBUTES
            auto perms = fs::status(path).permissions();

            // Simule retour CX → encode en attributs DOS
            uint16_t result = 0;
            if ((perms & fs::perms::owner_write) == fs::perms::none) result |= 0x01; // Read-only
            // (on pourrait ajouter hidden/system/archive avec fs::is_hidden etc.)
            return result;
        } else if (mode == 1) {
            // SET FILE ATTRIBUTES (partiel)
            fs::perms newPerms = fs::perms::owner_all;

            if (attr & 0x01) newPerms &= ~fs::perms::owner_write;

            fs::permissions(path, newPerms, fs::perm_options::replace);
            return 0;
        } else {
            handleDosError(1); // unsupported AL value
            return -1;
        }
    } catch (const std::exception&) {
        handleDosError(2); // File not found or access denied
        return -1;
    }
}

int deleteFile(const char* filepath) {
    if (std::remove(filepath) != 0) {
        // Simule l’erreur DOS en appelant un handler
        handleDosError(2); // fichier introuvable, ou 5 = accès refusé
        return -1;
    }

    return 0;
}

int moveFilePointer(int fd) {
    if (fd < 0 || fd >= maxFileCount) {
        handleDosError(6); // invalid handle
        return -1;
    }

    // Ici on simule : test du flag 0x200 (device spécial ?)
    // On suppose que ce flag est tracké ailleurs
    bool isSpecialDevice = false; // ← à adapter si tu as un flag réel

    if (isSpecialDevice) {
        return 1;
    }

    FILE* file = g_openFileHandles[fd].handle;
    if (!file) {
        handleDosError(6);
        return -1;
    }

    // Obtenir position courante
    long currentPos = ftell(file);
    if (currentPos < 0) {
        handleDosError(1); // erreur fictive
        return -1;
    }

    // Obtenir taille totale
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, currentPos, SEEK_SET); // Revenir à la position d’origine

    if (currentPos < fileSize) {
        return 0;
    } else {
        return 1; // simule le saut vers loc_69FA
    }
}

static int seekOriginFromMethod(uint8_t method)
{
    switch (method) {
        case 0: return SEEK_SET;
        case 1: return SEEK_CUR;
        case 2: return SEEK_END;
        default: return -1;
    }
}

int32_t moveFilePointerExtended(int fd, int32_t offset, uint8_t method)
{
    // asm: clear bit 0x0200 in fdFlagsRef(fd)
    // asm does: shl bx,1 then AND [0x0FB8+bx],0xFDFF (so word indexed by fd)
    fdFlagsRef(fd) = static_cast<uint16_t>(fdFlagsRef(fd) & ~0x0200u);

    const auto it = g_openFileHandles.find(fd);
    if (it == g_openFileHandles.end() || !it->second.handle) {
        handleDosError(6); // invalid handle (DOS-like)
        return -1;
    }

    FILE* f = it->second.handle;

    const int origin = seekOriginFromMethod(method);
    if (origin < 0) {
        handleDosError(1); // “invalid function” proxy (à ajuster selon ton err table)
        return -1;
    }

    if (std::fseek(f, static_cast<long>(offset), origin) != 0) {
        // Dans DOS, AX contient le code erreur.
        // Ici on n'a pas AX, donc tu peux mapper errno -> dos si tu veux.
        handleDosError(5); // accès refusé / seek failed (proxy)
        return -1;
    }

    const long pos = std::ftell(f);
    if (pos < 0) {
        handleDosError(1);
        return -1;
    }

    return static_cast<int32_t>(pos);
}

void closeFileHandle(uint16_t handle)
{
    if (handle >= maxFileCount) {
        handleDosError(6);  // Erreur : handle invalide
        return;
    }

    fdFlagsRef(handle) = 0;
    closeFile(handle);
}

void writeByteToBufferAndDecrementCounter(uint8_t* buffer, uint8_t value, uint8_t* remainingCount) {
    *buffer = value;         // Écrit la valeur dans le buffer
    buffer++;                // Avance le pointeur
    (*remainingCount)--;     // Décrémente le compteur

    if (*remainingCount != 0) {
        return;              // On a encore des octets à écrire
    }

    // Sinon on a fini, la suite du code principal reprendra après ce bloc
}


int closeFile(int fd) {
    auto it = g_openFileHandles.find(fd);
    if (it == g_openFileHandles.end()) {
        handleDosError(6); // 6 = Invalid handle
        return -1;
    }

    FILE* file = it->second.handle;
    if (std::fclose(file) != 0) {
        handleDosError(5); // 5 = Access denied
        return -1;
    }

    g_openFileHandles.erase(it);
    return 0;
}

bool testFileWriteability(int fd) {
    FILE* file = g_openFileHandles[fd].handle;
    if (!file) return false;

    // Tentative de "flush" sans rien écrire
    return (std::fflush(file) == 0);
}

int openFile(const char* filename, uint16_t flags) {
    // Déterminer le mode d'accès
    uint8_t accessMode = 1; // défaut = écriture
    if (!(flags & 0x0002)) accessMode = 2; // lecture + écriture
    if (!(flags & 0x0004)) accessMode = 0; // lecture seule

    // Mapper en mode fopen
    const char* mode = nullptr;
    switch (accessMode) {
        case 0: mode = "rb"; break;
        case 1: mode = "wb"; break;
        case 2: mode = "r+b"; break;
        default:
            handleDosError(1); // erreur invalide
            return -1;
    }

    FILE* file = std::fopen(filename, mode);
    if (!file) {
        handleDosError(2); // fichier introuvable ou refusé
        return -1;
    }

    // Génère un pseudo-handle DOS
    static int nextHandle = 3;
    int handle = nextHandle++;

    g_openFileHandles[handle].handle = file;

    // Nettoyage et masquage des flags, puis stockage
    uint16_t cleanFlags = (flags & 0xB8FF) | 0x8000;
    g_fdFlags[handle] = cleanFlags;

    return handle;
}

int writeFile(int fd, const void* buffer, uint16_t size) {
    // Interdiction d’écrire ?
    if (g_fdFlags[fd] & 0x0001) {
        handleDosError(5); // Access Denied
        return -1;
    }

    // Gestion spéciale device ?
    if (g_customWriteHandler && getAdviceInfo(fd)) {
        int result = g_customWriteHandler(buffer, size);
        return result >= 0 ? size : -1;  // On suppose succès = écrire tout
    }

    FILE* file = g_openFileHandles[fd].handle;
    if (!file) {
        handleDosError(6); // Handle invalide
        return -1;
    }

    size_t written = std::fwrite(buffer, 1, size, file);
    if (written < size && std::ferror(file)) {
        handleDosError(1); // Erreur générique
        return -1;
    }

    // Marque le handle comme "déjà écrit" (bit 0x1000)
    g_fdFlags[fd] |= 0x1000;

    return static_cast<int>(written);
}

FileOpenMode prepareFileInfo(
    const char* adviceTypeStr,
    uint16_t* outAttributeFlags,
    uint16_t* outRequestedFlags
) {
    if (!adviceTypeStr || !outAttributeFlags || !outRequestedFlags)
        return FileOpenMode::Invalid;

    uint16_t attrFlags = 0;
    uint16_t reqFlags = 0;
    FileOpenMode result = FileOpenMode::Invalid;

    char modeChar = adviceTypeStr[0];
    switch (modeChar) {
        case 'r': attrFlags = 0x0001; result = FileOpenMode::Read; break;
        case 'w': attrFlags = 0x0302; reqFlags = 0x0080; result = FileOpenMode::Write; break;
        case 'a': attrFlags = 0x0902; reqFlags = 0x0080; result = FileOpenMode::Append; break;
        default: return FileOpenMode::Invalid;
    }

    char nextChar = adviceTypeStr[1];
    if (nextChar == '+') {
        attrFlags = (attrFlags & 0xFFFC) | 0x0004;
        reqFlags = 0x0180;
        result = FileOpenMode::ReadWrite;

        char thirdChar = adviceTypeStr[2];
        if (thirdChar == 't') {
            attrFlags |= 0x4000;
        } else if (thirdChar == 'b') {
            attrFlags |= 0x8000;
        } else {
            attrFlags |= (globalCompatFlags & 0xC000);
            if (attrFlags & 0x8000)
                result = static_cast<FileOpenMode>(static_cast<int>(result) | static_cast<int>(FileOpenMode::BinaryModeFlag));
        }
    } else {
        if (nextChar == 't') {
            attrFlags |= 0x4000;
        } else if (nextChar == 'b') {
            attrFlags |= 0x8000;
        } else {
            attrFlags |= (globalCompatFlags & 0xC000);
            if (attrFlags & 0x8000)
                result = static_cast<FileOpenMode>(static_cast<int>(result) | static_cast<int>(FileOpenMode::BinaryModeFlag));
        }
    }

    *outAttributeFlags = attrFlags;
    *outRequestedFlags = reqFlags;
    setupFileMode = cleanupAllOpenFiles;
    return result;
}

void cleanupAllOpenFiles() {
    for (int i = 0; i < maxFileCount; ++i) {
        FileLike& file = fileTable[i];
        if (file.flags & (FILE_FLAG_WRITEONLY | FILE_FLAG_READONLY)) {
            cleanFile(&file);  // nettoyage si fichier actif
        }
    }
}

int cleanFile(FileLike* file) {
    int result = -1;

    if (file->buffer != reinterpret_cast<char*>(file)) {
        if (file->bufferSize != 0) {
            if (file->bytesRead < 0) {
                if (flushTextBuffer(file) != 0) {
                    return result;
                }
            }

            if (file->flags & 0x0004) {
                freeLocalMemory(file->buffer);
            }
        }

        if (file->fd >= 0) {
            int handle = static_cast<uint8_t>(file->fd);
            closeFileHandle(handle);
        }

        file->flags = 0;
        file->bufferSize = 0;
        file->bytesRead = 0;
        file->fd = -1;

        // if (file->pad != 0) {
        //     prepareAndRelease(0, 0, file->pad);
        //     deleteFile(file->buffer);
        //     file->pad = 0;
        // }
    }

    return result;
}

int freeLocalMemory(void* ptr) {
     free(ptr);
     return 0;
}

uint16_t prepareAndRelease(void* ptr, uint16_t value, uint16_t fallback) {
    const char* src = (fallback != 0) ? reinterpret_cast<const char*>(fallback) : reinterpret_cast<const char*>(0x2ED2);

    char* dest = reinterpret_cast<char*>(ptr);
    uint16_t result = writeStringAndAdvance(dest, src);

    callWithAudit(result);
    copySecondStringIntoFirst(g_buffer1044, src);

    return reinterpret_cast<uintptr_t>(src) & 0xFFFF;
}

uint16_t writeStringAndAdvance(char* dest, const char* src) {
    size_t len = std::strlen(src);
    copyMemory(dest, src, static_cast<uint16_t>(len + 1));
    return reinterpret_cast<uintptr_t>(dest + len) & 0xFFFF;
}

void copySecondStringIntoFirst(char* dest, const char* src) {
    size_t len = std::strlen(src);
    std::memmove(dest, src, len + 1);
}

void callWithAudit(uint32_t value) {
    formatAndWriteDecimal('a', false, 10, value, g_buffer1044);
}

void formatAndWriteDecimal(
    char baseChar,       // usually 'a'
    bool isNegative,     // from arg_2
    uint16_t radix,      // from arg_4 (e.g., 10)
    uint32_t value,      // composed from arg_8:arg_A
    char* dest           // output destination
) {
    char temp[34];
    int i = 0;

    if (isNegative && static_cast<int32_t>(value) < 0) {
        *dest++ = '-';
        value = -static_cast<int32_t>(value);
    }

    do {
        temp[i++] = value % radix;
        value /= radix;
    } while (value > 0);

    while (i--) {
        char digit = temp[i];
        *dest++ = (digit < 10) ? (digit + '0') : (digit + baseChar);
    }

    *dest = '\0';
}

int fallbackWriteChar(uint8_t c, FileLike* file) {
    file->bytesRead--;
    return writeCharToFile(c, file);
}

int writeCharToFile(uint8_t c, FileLike* file) {
    lastWrittenChar = c;
    if (file->bytesRead != -1) {
        if (file->buffer) {
            *(file->buffer++) = c;
            file->bytesRead++;
        }

        if ((file->flags & 0x08) && (c == '\n' || c == '\r')) {
            if (flushTextBuffer(file) != 0)
                return -1;
        }

        return c;
    }
    if (file->flags & (0x90 | 0x02)) {
        file->flags |= 0x10;
        return -1;
    }
    file->flags |= 0x100;
    if (file->bufferSize > 0) {
        if (file->bytesRead != 0 && flushTextBuffer(file) != 0)
            return -1;

        file->bytesRead = -file->bufferSize;
        *(file->buffer++) = c;

        if ((file->flags & 0x08) && (c == '\n' || c == '\r')) {
            if (flushTextBuffer(file) != 0)
                return -1;
        }

        return c;
    }
    if (fdFlagsRef(file->fd) & 0x800) {
        moveFilePointerExtended(file->fd, 0, 2);
    }

    if (c == '\n' && !(file->flags & 0x40)) {
        if (writeFile(file->fd, "\n", 1) != 1) {
            file->flags |= 0x10;
            return -1;
        }
    }

    if (writeFile(file->fd, &c, 1) != 1) {
        if (!(file->flags & 0x200)) {
            file->flags |= 0x10;
            return -1;
        }
    }

    return c;
}


int writeBufferToFile(FileLike* file, int size, const uint8_t* buffer) {
    int originalSize = size;

    if (file->flags & 0x08) { // Text mode
        while (size-- > 0) {
            if (writeCharToFile(*buffer++, file) == -1)
                return 0;
        }
        return originalSize;
    }

    if (file->flags & 0x40) { // Direct mode
        if (file->bufferSize != 0) {
            if (file->bufferSize >= size) {
                if (file->bytesRead != 0 && flushTextBuffer(file))
                    return 0;

                uint16_t fd = file->fd;
                if (fdFlagsRef(fd) & 0x800) {
                    moveFilePointerExtended(fd, 0, 2); // SEEK_END
                }

                if (writeFile(fd, buffer, size) != size)
                    return 0;

                return originalSize;
            } else {
                int newcount = file->bytesRead + size;
                if (newcount < 0) {
                    file->bytesRead = -file->bufferSize;
                    std::memcpy(file->buffer, buffer, size);
                    file->buffer += size;
                } else {
                    if (file->bytesRead != 0 && flushTextBuffer(file))
                        return 0;
                    std::memcpy(file->buffer, buffer, size);
                    file->bytesRead += size;
                    file->buffer += size;
                }
                return originalSize;
            }
        } else {
            uint16_t fd = file->fd;
            if (fdFlagsRef(fd) & 0x800)
                moveFilePointerExtended(fd, 0, 2); // SEEK_END

            if (writeFile(fd, buffer, size) != size)
                return 0;

            return originalSize;
        }
    }

    while (size-- > 0) {
        if (++file->bytesRead < -1) {
            *file->buffer++ = *buffer++;
        } else {
            file->bytesRead--;
            if (writeCharToFile(*buffer++, file) == -1)
                return 0;
        }
    }

    return originalSize;
}

int writeStringToFile(const char* str, FileLike* file) {
    if (!str)
        return 0;

    size_t len = strlen(str);
    if (len == 0)
        return 0;

    // Écrit la chaîne dans le fichier
    if (writeBufferToFile(file, len, reinterpret_cast<const uint8_t*>(str)) == 0)
        return -1;

    // Renvoie le dernier caractère écrit
    return static_cast<uint8_t>(str[len - 1]);
}

int writeLineToFile(const char* str, FileLike* file)
{
    if (!file) return 0;

    const int last = writeStringToFile(str, file);
    if (last == -1) { // 0xFFFF
        return 0;
    }

    const int rc = writeCharToFile('\n', file);
    if (rc == -1) { // 0xFFFF
        return 0;
    }

    return 1;
}

int writeMultiLineDataFromBlock(const char* base, FileLike* outFile) {
    if (!base || !outFile) return 0;

    const char* field0   = base;                 // +0x00
    const char* field23  = base + 0x23;          // +0x23
    const char* field78  = base + 0x78;          // +0x78
    const char* linesOut = base + 0xCD;          // +0xCD

    if (!writeLineToFile(field0,  outFile)) return 0;
    if (!writeLineToFile(field78, outFile)) return 0;
    if (!writeLineToFile(field23, outFile)) return 0;

    for (int i = 0; i < kLines; ++i) {
        if (!writeLineToFile(linesOut + i * kLineSize, outFile)) return 0;
    }

    return 1;
}

int writeMultiLineData(const char* str, FileLike* file) {
    // Ligne principale
    if (!writeLineToFile(str, file)) return 0;

    // Ligne offsetée de +0x78
    if (!writeLineToFile(str, reinterpret_cast<FileLike*>((uintptr_t)file + 0x78))) return 0;

    // Ligne offsetée de +0x23
    if (!writeLineToFile(str, reinterpret_cast<FileLike*>((uintptr_t)file + 0x23))) return 0;

    // Boucle sur 0x14 (= 20) lignes supplémentaires
    uintptr_t base = (uintptr_t)file + 0xCD;
    for (int i = 0; i < 20; ++i) {
        if (!writeLineToFile(str, reinterpret_cast<FileLike*>(base))) return 0;
        base += 0x23;
    }

    return 1;
}

int findMappedValue(uint16_t seg, uint16_t ofs, uint8_t* outValue)
{
    for (int i = 0;; ++i)
    {
        const SegmentOffsetEntry& entry = mappedTable[i];

        if (entry.segment == 0xFFFF)
            break;

        if (entry.segment == seg && entry.offset == ofs)
        {
            *outValue = entry.value;
            return 1;
        }
    }

    return 0;
}

void generateMappedLine(uint16_t index, uint8_t* outputBuffer) {
    uint16_t offset = 0;
    uint8_t mappedValue = 0;

    uint16_t counter = 0;
    uint16_t entryPointer = index * 2 + 0x127E;

    while (counter < 30) {
        uint16_t dx = *reinterpret_cast<uint16_t*>(entryPointer);

        uint16_t segment;
        if (static_cast<int16_t>(dx) >= 0) {
            // dx is a positive index into a table at 0x172E
            uint16_t indexValue = *reinterpret_cast<uint16_t*>(0x172E + dx * 8);
            offset = indexValue;
            segment = 2;
        } else if (dx == 0xFFFF) {
            segment = 0;
            offset = 0;
        } else if (dx == 0xFFFE) {
            segment = 3;
            offset = 0;
        } else {
            segment = 1;
            offset = dx;
        }

        if (findMappedValue(segment, offset, &mappedValue)) {
            outputBuffer[counter] = mappedValue;
        } else {
            outputBuffer[counter] = ' ';
        }

        entryPointer += 0x28;
        ++counter;
    }

    outputBuffer[30] = 0;
}

static void copyZ(char* dst, size_t dstCap, const char* src)
{
    if (!dst || dstCap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    std::strncpy(dst, src, dstCap - 1);
    dst[dstCap - 1] = '\0';
}

void generateFileFromMappedData()
{
    std::array<char, kBlockSize> block{};
    char* base = block.data();

    char* field0   = base;                 // var_38C
    char* field23  = base + kOff_0x23;      // var_369
    char* field78  = base + kOff_0x78;      // var_314
    char* linesOut = base + kOff_0xCD;      // outputBuffer (= base+0xCD)

    copyZ(field0,  kOff_0x23,                  g_levelName);
    copyZ(field23, kOff_0x78 - kOff_0x23,      g_secondaryTextStr);
    copyZ(field78, kOff_0xCD - kOff_0x78,      g_hintTextStr);

    for (uint16_t i = 0; i < kLines; ++i) {
        generateMappedLine(i, reinterpret_cast<uint8_t*>(linesOut + i * kLineSize));
    }

    const char* adviceType = getStringById(0x48D);
    const char* filepath   = g_selectedFilePath;

    FileLike* file = openAndPrepareFileFromSlot(filepath, adviceType);
    if (!file) {
        showMessage("Cannot write file: ", filepath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    if (resetAndSeekFile(file, 0) != 0) {
        showMessage("Cannot write file: ", filepath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    if (!writeLineToFile("1", file)) {
        showMessage("Cannot write file: ", filepath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    if (!writeMultiLineDataFromBlock(base, file)) {
        showMessage("Cannot write file: ", filepath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    cleanFile(file);
}

static int16_t computeTextSeekAdjustment(FileLike* file)
{
    int16_t diVal;

    if (file->bytesRead < 0)
        diVal = static_cast<int16_t>(file->bufferSize + file->bytesRead + 1);
    else
        diVal = static_cast<int16_t>(std::abs(file->bytesRead));

    if (file->flags & 0x0040)
        return diVal;

    int16_t count = static_cast<int16_t>(std::abs(file->bytesRead));
    if (count == 0)
        return diVal;

    if (file->bytesRead < 0)
    {
        const char* p = file->bufferCursor;
        for (int16_t i = 0; i < count; ++i)
        {
            --p;
            if (*p == '\n')
                ++diVal;
        }
    }
    else
    {
        const char* p = file->bufferCursor;
        for (int16_t i = 0; i < count; ++i)
        {
            if (*p == '\n')
                ++diVal;
            ++p;
        }
    }

    return diVal;
}

int resetAndSeekFile(FileLike* file, int mode)
{
    if (!file)
        return -1;

    flushTextBuffer(file);

    file->flags &= 0xFE7F;
    file->bytesRead = 0;
    file->bufferCursor = file->buffer;

    if (mode == 1 && file->bytesRead > 0)
    {
        moveFilePointerExtended(file->fd, -file->bytesRead, SEEK_CUR);
    }

    return 0;
}

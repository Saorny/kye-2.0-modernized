#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstdint>
#include <ctime>
#include <dos.h>
#include <cstdio>
#include <vector>
#include <string>
#include <system_error>
#include <cstddef>
#include <windows.h>
#include <tuple>
#include <stdexcept>
#include <cstring>
#include "file.h"
#include "error.h"
#include "system.h"
#include "util.h"
#include "graph.h"
#include "dialog.h"
#include "game.h"

namespace fs = std::filesystem;

static constexpr int kLines = 0x14;      // 20
static constexpr int kLineSize = 0x23;   // 35 bytes

int findMatchingLineInFile(const std::string& target)
{
    return findMatchingLineInFile(target.c_str());
}

int findMatchingLineInFile(const char* targetString) {
    if (fileAccessEnabled == 0)
        return -1;

    FileLike* file = openAndPrepareFileFromSlot((const char*)0x01A0, (const char*)0x0476); // ⚠ remplacer si possible par vraie chaîne
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

        if (readStructuredBlock(file, entryBuffer) < 0) {
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


FileLike* prepareAndOpenFile(FileLike* file, uint16_t openFlags, const char* adviceType, const char* filepath) {
    if (!file) return nullptr;

    uint16_t requestedFlags = 0;
    uint16_t attributeFlags = 0;
    int mode = prepareFileInfo(adviceType, &attributeFlags, &requestedFlags); 
    file->flags = mode;

    if (mode == 0 || file->fd >= 0) {
        file->fd = -1;
        file->flags = 0;
        return nullptr;
    }

    // Ouverture du fichier
    int finalFlags = attributeFlags | openFlags;
    int handle = openFileHandler(filepath, finalFlags, (finalFlags & 0x00F0) != 0);
    if (handle < 0) {
        file->fd = -1;
        file->flags = 0;
        return nullptr;
    }

    file->fd = static_cast<uint8_t>(handle);

    if (getAdviceInfo(handle)) {
        file->flags |= 0x0200;
    }

    bool needsInit = (file->flags & 0x0200);
    int param = needsInit ? 1 : 0;

    if (validateFile(file, param) != 0) {
        cleanFile(file);
        return nullptr;
    }

    file->pad = 0;
    return file;
}

int countWrittenCharacters(const FileLike* file) {
    int count = 0;

    // Cas inhabituel : bytesRead est négatif
    if (file->count < 0) {
        count = file->bufferSize + file->count + 1;
    } else {
        count = file->count;
    }

    // Si le fichier est en mode texte (flag 0x40 non activé)
    if ((file->flags & 0x40) == 0) {
        const char* buffer = file->buffer;
        const char* start = file->bufferStart;

        // Détermination de la plage à analyser
        int offset = static_cast<int>(start - buffer);
        if (file->count < 0) {
            for (int i = offset - 1; i >= offset - count; --i) {
                if (buffer[i] == '\n') {
                    ++count;
                }
            }
        } else {
            for (int i = offset; i < offset + count; ++i) {
                if (buffer[i] == '\n') {
                    ++count;
                }
            }
        }
    }
    return count;
}

int validateFile(FileLike* file, int mode) {
    if (!file) return -1;
    char* expectedInlineBuffer = reinterpret_cast<char*>(file) + 5;

    if (file->bufferStart != expectedInlineBuffer || mode > 2 || file->bufferSize > 0x7FFF)
        return -1;

    // Cas spécial : adresses magiques
    if (!hasValidatedE86FileOnce && file == reinterpret_cast<FileLike*>(0xE86))
        hasValidatedE86FileOnce = true;
    else if (hasValidatedFile_E76 == 0 && file == reinterpret_cast<FileLike*>(0xE76))
        hasValidatedFile_E76 = 1;

    // Réinitialiser si besoin
    if (file->count != 0) {
        resetAndSeekFile(file, 1);  // Deuxième paramètre = mode
    }

    // Libération de buffer si alloué dynamiquement
    if (file->flags & 0x04) {
        freeLocalMemory(file->buffer);
    }

    // Reset flags & pointeurs
    file->flags &= ~0x0C; // Clear bits 2 and 3
    file->count = 0;

    // Par défaut, buffer = zone intégrée dans la structure
    char* inlineBuffer = reinterpret_cast<char*>(file) + 5;
    file->buffer = inlineBuffer;
    file->bufferStart = inlineBuffer;

    // Si on doit utiliser un buffer externe
    if (mode != 2 && file->bufferSize > 0) {
        validateBuffer = reinterpret_cast<FileCallback>(0x7F10);

        if (file->fd == 0) {
            file->buffer = static_cast<char*>(allocateLocalMemory(file->bufferSize));
            if (!file->buffer) return -1;
            file->bufferStart = file->buffer;
            file->flags |= 0x04; // marquer comme alloué dynamiquement
        }

        // Simule un buffer plein ?
        file->count = file->bufferSize;
    }

    if (mode == 1)
        file->flags |= 0x08;

    return 0;
}



int openFileHandler(const std::string& filepath, uint16_t flags, bool isNewFile) {
    int initialAttr = fileAttrOp(filepath.c_str(), 0, 0);
    if (initialAttr == -1 && err != 2) {
        handleDosError(err);
        return -1;
    }

    int tmpFd = createFile(filepath.c_str(), isNewFile ? 0 : initialAttr);
    if (tmpFd < 0) return -1;
    closeFile(tmpFd);

    int handle = openFile(filepath.c_str(), flags);
    if (handle < 0) return -1;

    uint16_t fileInfo = ioctl(handle, 0, 0, 0);
    if (fileInfo & 0x80) {
        flags |= 0x2000;
        if (flags & 0x8000) {
            uint16_t modAttr = (fileInfo & 0xFF) | 0x20;
            ioctl(handle, 1, modAttr, 0);
        }
    } else if (flags & 0x0200) {
        testFileWriteability(handle);
    }

    if ((initialAttr & 0x01) && (flags & 0x0100) && (flags & 0x00F0)) {
        fileAttrOp(filepath.c_str(), 1, 0x01);
    }

    uint16_t newFlags = (flags & 0xF8FF);
    if (flags & 0x0300) newFlags |= 0x1000;
    if (!(initialAttr & 0x01)) newFlags |= 0x0100;

    fdFlags[handle] = newFlags;
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

int readLineToBuffer(FileLike *file, char *outBuf, int maxLen) {
    char tempBuffer[256] = {0};  // Stack buffer
    readLine(tempBuffer, 0xFF, file);  // Read at most 255 chars

    int i = 0;
    const char *src = tempBuffer;
    char *dst = outBuf;

    while (*src != '\0' && i < maxLen) {
        if (*src == '\n' || *src == '\r') break;
        *dst++ = *src++;
        i++;
    }

    *dst = '\0';
    return i;
}

void flushAllDirtyTextBuffers() {
    FileLike* file = (FileLike*)0x0E76; // base address of file table
    int count = 0x14; // 20 entries

    while (count--) {
        uint16_t flags = file->flags & 0x0300;
        if (flags == 0x0300) {
            flushTextBuffer(file);  // sub_6AC6
        }
        file++;
    }
}

int readCharFromTextBuffer(FileLike* file) {
    if (!file) return kEOF;

    // Fast path: il reste des chars en buffer
    if (file->bytesRead > 0) {
        file->bytesRead--;
        const unsigned char ch = static_cast<unsigned char>(*file->bufferCursor);
        file->bufferCursor++;
        return static_cast<int>(ch);
    }

    // Si bytesRead == 0 : pas de données en buffer
    // Si bytesRead < 0 ou conditions/flags => erreur/EOF
    if (file->bytesRead < 0) {
        file->flags |= kFlagErrorOrEOF;
        return kEOF;
    }

    // Si flags 0x110 set OU pas read-enabled, etc. => fail
    if ((file->flags & kFlagNoRefillMask) != 0 ||
        (file->flags & kFlagReadEnabled) == 0) {
        file->flags |= kFlagErrorOrEOF;
        return kEOF;
    }

    // Mode "buffered": si bufferSize != 0, on tente de remplir et relire
    if (file->bufferSize != 0) {
        file->flags |= kFlagBufferedActive;
        if (fillTextBuffer(file) == 0) {
            // refill ok => relire (retombe sur fast path logique asm)
            file->bytesRead--;
            const unsigned char ch = static_cast<unsigned char>(*file->bufferCursor);
            file->bufferCursor++;
            return static_cast<int>(ch);
        }
        return kEOF;
    }

    // Mode "unbuffered": lecture 1 byte dans un buffer global (asm: 0x2EE0)
    if (file->flags & kFlagDirtyAutoFlush) {
        flushAllDirtyTextBuffers();
    }

    char one = 0;
    const int n = readTextBuffer(static_cast<int>(file->fd), &one, 1);
    if (n == 0) {
        // asm: si read==0 => moveFilePointer(fd) puis check ax==1 sinon error
        const int ok = moveFilePointer(static_cast<int>(file->fd));
        if (ok == 1) {
            uint16_t f = file->flags;
            f = static_cast<uint16_t>((f & kFlagClearMask_FE7F) | kFlagSomeMode20);
            file->flags = f;
        } else {
            file->flags |= kFlagErrorOrEOF;
        }
        return kEOF;
    }

    // n != 0 : un char dispo (asm: g_pendingChar, CR/LF handling)
    g_pendingChar = static_cast<int16_t>(static_cast<unsigned char>(one));

    if (g_pendingChar == 0x0D) { // '\r'
        // asm: si pas flag 0x40 => boucle vers lecture brute (loc_6F8E)
        if ((file->flags & kFlagLineMode) == 0) {
            // relire un nouveau char (comportement identique au saut asm)
            return readCharFromTextBuffer(file);
        }
    }

    file->flags = static_cast<uint16_t>(file->flags & ~kFlagErrorOrEOF);
    return static_cast<int>(static_cast<unsigned char>(g_pendingChar));
}

int fillTextBuffer(FileLike* file) {
    if (file->flags & 0x0200) {
        flushAllDirtyTextBuffers();
    }

    file->bufferStart = file->buffer;

    int bytesRead = readTextBuffer(file->fd, file->buffer, file->bufferSize);
    file->count = bytesRead;

    if (bytesRead <= 0) {
        if (file->count == 0) {
            file->flags &= ~0x0180;
            file->flags |= 0x0020;  // EOF
        } else {
            file->count = 0;
            file->flags |= 0x0010;  // Error?
        }
        return -1;
    }

    file->flags &= ~0x0020;  // Clear EOF flag
    return 0;
}

int readAndTrackChar(FileLike* file) {
    ++file->count;
    return readCharFromTextBuffer(file);
}

int readLine(char *outputBuffer, int maxLen, FileLike *file) {
    char *dst = outputBuffer;
    int remaining = maxLen;
    int ch = 0;

    while (true) {
        if (ch == '\n') break;
        if (--remaining <= 0) break;

        if (--file->count < 0) {
            ch = readAndTrackChar(file);
        } else {
            ch = static_cast<unsigned char>(*(file->bufferStart)++);
        }

        if (ch == -1) break;

        *dst++ = static_cast<char>(ch);
    }

    if (ch == -1 && dst == outputBuffer) {
        return 0;  // nothing read
    }

    *dst = '\0';

    if (file->flags & 0x10) {
        return 0;  // error: some read failure
    }

    return reinterpret_cast<int>(outputBuffer);
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
    if (fd >= maxFileCount)
        handleDosError(6); // File handle out of range

    if (size < 1)
        return 0;

    // Si fichier ouvert en mode texte spécial (flag 0x200)
    if (fdFlags[fd] & 0x0200)
        return 0;

    int bytesRead = readFile(fd, buffer, size);
    if (bytesRead < 1)
        return bytesRead;

    // Si fichier ouvert en mode texte avec CRLF management (flag 0x4000)
    if (!(fdFlags[fd] & 0x4000))
        return bytesRead;

    char* dst = buffer;
    char* src = buffer;
    int count = bytesRead;

    while (count--) {
        char c = *src++;
        if (c == 0x1A) {  // EOF in DOS text files
            // Rewind file by remaining characters
            moveFilePointerExtended(fd, -count - 1, SEEK_CUR, 0);
            fdFlags[fd] |= 0x200;
            break;
        } else if (c == '\r') {
            // skip CR, but check next
            continue;
        } else {
            *dst++ = c;
        }
    }

    return dst - buffer;
}

int readFile(int fd, void* buffer, uint16_t size) {
    // Vérifie le bit 1 (0x0002)
    if (g_fileFlags[fd] & 0x0002) {
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

int readStructuredBlock(FileLike* file, char* outBuf) {
    // Lecture des 3 lignes d'en-tête
    if (readLineToBuffer(file, outBuf, 0x21) < 0) return -1;
    if (readLineToBuffer(file, outBuf + 0x78, 0x53) < 0) return -1;
    if (readLineToBuffer(file, outBuf + 0x23, 0x53) < 0) return -1;

    // Lecture des 20 blocs : chacun fait 0x23 (35) bytes, mais on lit 0x21 (33) et saute 35
    char* ptr = outBuf + 0xCD;
    for (int i = 0; i < 0x14; ++i) {
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
        moveFilePointerExtended(fd, 0, 0, 2); // seek end
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

int moveFilePointerExtended(int fd, uint16_t offsetLow, uint16_t offsetHigh, uint8_t method) {
    // Efface le bit 0x200 (bit 9)
    g_fileFlags[fd] &= ~0x0200;

    FILE* file = g_openFileHandles[fd].handle;
    if (!file) {
        handleDosError(6); // "invalid handle"
        return -1;
    }

    // Reconstituer l’offset 32-bit
    long offset = static_cast<long>(offsetLow) | (static_cast<long>(offsetHigh) << 16);

    // Mappe la méthode : 0 = SEEK_SET, 1 = SEEK_CUR, 2 = SEEK_END
    int seekOrigin;
    switch (method) {
        case 0: seekOrigin = SEEK_SET; break;
        case 1: seekOrigin = SEEK_CUR; break;
        case 2: seekOrigin = SEEK_END; break;
        default:
            handleDosError(22); // "invalid function"
            return -1;
    }

    // Déplacement
    if (std::fseek(file, offset, seekOrigin) != 0) {
        handleDosError(5); // "access denied" (par défaut)
        return -1;
    }

    return 0;
}

void closeFileHandle(uint16_t handle)
{
    if (handle >= maxFileCount) {
        handleDosError(6);  // Erreur : handle invalide
        return;
    }

    fdFlags[handle] = 0;
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
    g_fileFlags[handle] = cleanFlags;

    return handle;
}

int writeFile(int fd, const void* buffer, uint16_t size) {
    // Interdiction d’écrire ?
    if (g_fileFlags[fd] & 0x0001) {
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
    g_fileFlags[fd] |= 0x1000;

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

    if (file->bufferStart != reinterpret_cast<char*>(file)) {
        if (file->bufferSize != 0) {
            if (file->count < 0) {
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
        file->count = 0;
        file->fd = 0xFF;

        if (file->pad != 0) {
            prepareAndRelease(0, 0, file->pad);  // Libère ou annule ce qui est pointé par pad
            deleteFile(file->bufferStart);     // Supprime fichier temporaire ?
            file->pad = 0;
        }
    }

    return result;
}

int freeLocalMemory(void* ptr) {
    // return LocalFree(ptr);
    // return delete (ptr);
    return 0;
}

uint16_t prepareAndRelease(void* ptr, uint16_t value, uint16_t fallback) {
    const char* src = (fallback != 0) ? reinterpret_cast<const char*>(fallback) : reinterpret_cast<const char*>(0x2ED2);
    uint16_t ax = (value != 0) ? value : 0x1040;

    char* dest = reinterpret_cast<char*>(ptr);
    uint16_t result = writeStringAndAdvance(dest, src);

    callWithAudit(result);
    copySecondStringIntoFirst(g_buffer1044, src);

    return reinterpret_cast<uint16_t>(src);
}

uint16_t writeStringAndAdvance(char* dest, const char* src) {
    size_t len = std::strlen(src);
    copyMemory(dest, src, static_cast<uint16_t>(len + 1));  // +1 pour le '\0'
    return reinterpret_cast<uint16_t>(dest + len);        // adresse après la chaîne
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
    file->count--;  // Corrige la surestimation précédente
    return writeCharToFile(c, file);
}

int writeCharToFile(uint8_t c, FileLike* file) {
    // Sauvegarde globale du dernier caractère (si nécessaire ailleurs)
    lastWrittenChar = c;

    // Cas 1 : Buffer actif et pas plein
    if (file->count != 0xFFFF) {
        if (file->bufferStart) {
            *(file->bufferStart++) = c;
            file->count++;
        }

        if ((file->flags & 0x08) && (c == '\n' || c == '\r')) {
            if (flushTextBuffer(file) != 0)
                return -1;
        }

        return c;
    }

    // Cas 2 : Flags interdisent l'écriture → erreur immédiate
    if (file->flags & (0x90 | 0x02)) {
        file->flags |= 0x10;
        return -1;
    }

    // Marquer le tampon comme modifié
    file->flags |= 0x100;

    // Cas 3 : Buffer activé, mais plein ou désactivé temporairement
    if (file->bufferSize > 0) {
        if (file->count != 0 && flushTextBuffer(file) != 0)
            return -1;

        file->count = -file->bufferSize;
        *(file->bufferStart++) = c;

        if ((file->flags & 0x08) && (c == '\n' || c == '\r')) {
            if (flushTextBuffer(file) != 0)
                return -1;
        }

        return c;
    }

    // Cas 4 : Pas de buffer → écriture directe
    if (fdFlags[file->fd] & 0x800) {
        moveFilePointerExtended(file->fd, 0, 0, 2); // SEEK_END
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
                if (file->count != 0 && flushTextBuffer(file))
                    return 0;

                uint16_t fd = file->fd;
                if (fdFlags[fd] & 0x800) {
                    moveFilePointerExtended(fd, 0, 0, 2); // SEEK_END
                }

                if (writeFile(fd, buffer, size) != size)
                    return 0;

                return originalSize;
            } else {
                int newcount = file->count + size;
                if (newcount < 0) {
                    file->count = -file->bufferSize;
                    std::memcpy(file->bufferStart, buffer, size);
                    file->bufferStart += size;
                } else {
                    if (file->count != 0 && flushTextBuffer(file))
                        return 0;
                    std::memcpy(file->bufferStart, buffer, size);
                    file->count += size;
                    file->bufferStart += size;
                }
                return originalSize;
            }
        } else {
            uint16_t fd = file->fd;
            if (fdFlags[fd] & 0x800)
                moveFilePointerExtended(fd, 0, 0, 2); // SEEK_END

            if (writeFile(fd, buffer, size) != size)
                return 0;

            return originalSize;
        }
    }

    // Fallback buffered mode (not text, not direct)
    while (size-- > 0) {
        if (++file->count < 0xFFFF) {
            *file->bufferStart++ = *buffer++;
        } else {
            file->count--;
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

int writeLineToFile(const char* str, FileLike* file) {
    int lastChar = writeStringToFile(str, file);
    if (lastChar == -1)
        return 0;

    int result = writeCharToFile('\n', file);
    if (result == -1)
        return 0;

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

int findMappedValue(uint16_t seg, uint16_t ofs, uint8_t* outValue) {
    for (int i = 0;; ++i) {
        // Calcul de l'adresse de l'entrée i dans la table
        uintptr_t base = reinterpret_cast<uintptr_t>(mappedTable) + i * 5;

        uint16_t entrySeg = *reinterpret_cast<uint16_t*>(base + 0x2C0);
        if (entrySeg == 0xFFFF)
            break;

        uint16_t entryOfs = *reinterpret_cast<uint16_t*>(base + 0x2C2);
        if (entrySeg == seg && entryOfs == ofs) {
            *outValue = *reinterpret_cast<uint8_t*>(base + 0x2C4);
            return 1;
        }
    }

    return 0;
}

void generateMappedLine(uint16_t index, uint8_t* outputBuffer) {
    uint16_t offset = 0;
    uint16_t value = 0;
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

void generateFileFromMappedData() {
    // Local copies (exact sizes not shown in snippet)
    char nameCopy[0x100];
    char otherCopy[0x100];
    char hintCopy[0x200];

    std::strcpy(nameCopy,  g_levelName);
    std::strcpy(otherCopy, g_secondaryText);
    std::strcpy(hintCopy,  g_hintText);

    // Build mapped lines buffer (20 * 0x23)
    std::array<char, kLines * kLineSize> mapped{};
    for (int i = 0; i < kLines; i++) {
        generateMappedLine(&mapped[i * kLineSize], i);
    }

    // Open target file (slot/path)
    auto file = openAndPrepareFileFromSlot(/*adviceType*/0x48D, /*path*/g_selectedFilePath);
    if (!file) {
        showMessage(/*caption*/0x491, g_selectedFilePath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    if (!resetAndSeekFile(file, /*mode*/0)) {
        showMessage(/*caption*/0x4A4, g_selectedFilePath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    // Header line from string id 0x48F (probablement une signature/version)
    if (!writeLineToFile(file, getStringById(0x48F))) {
        showMessage(/*caption*/0x4A4, g_selectedFilePath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    // Write full content (name/hint + mapped lines, etc.)
    if (!writeMultiLineData(file, nameCopy /*points to a structure of blocks in original*/)) {
        showMessage(/*caption*/0x4A4, g_selectedFilePath);
        resetLevelStateMemory();
        fileAccessEnabled = 0;
        return;
    }

    cleanFile(file);
}

static int16_t computeTextSeekAdjustment(const FileLike& file) {
    int16_t diVal = 0;

    if (file.bytesRead < 0) {
        int16_t dx = static_cast<int16_t>(static_cast<int32_t>(file.bufferSize) + file.bytesRead + 1);
        diVal = dx;
    } else {
        diVal = static_cast<int16_t>(std::abs(file.bytesRead));
    }
    if ((file.flags & 0x0040u) != 0) {
        return diVal;
    }

    const int16_t count = static_cast<int16_t>(std::abs(file.bytesRead));
    if (count == 0) return diVal;

    if (file.bytesRead < 0) {
        const char* p = file.bufferCursor;
        for (int16_t i = 0; i < count; ++i) {
            --p;
            if (*p == '\n') {
                ++diVal;
            }
        }
    } else {
        const char* p = file.bufferCursor;
        for (int16_t i = 0; i < count; ++i) {
            if (*p == '\n') {
                ++diVal;
            }
            ++p;
        }
    }

    return diVal;
}


static int resetAndSeekFile(FileLike& file, int32_t offset, int origin) {
    if (flushTextBuffer(&file) != 0) {
        return -1;
    }

    if (origin == 1 && file.bytesRead > 0) {
        offset -= static_cast<int32_t>(computeTextSeekAdjustment(file));
    }

    file.flags = static_cast<uint16_t>(file.flags & 0xFE5Fu);
    file.bytesRead = 0;
    file.bufferCursor = file.buffer;

    const int rc = moveFilePointerExtended(static_cast<int>(file.fd), offset, origin);
    return (rc == -1) ? -1 : 0;
}

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
#include "file.h"
#include "error.h"
#include "system.h"

namespace fs = std::filesystem;

int openFileHandler(const char* filename, uint16_t flags, uint16_t attrFlags) {
    // Appliquer les flags globaux de compatibilité s'ils ne sont pas déjà présents
    if ((flags & 0xC000) == 0) {
        flags |= (globalCompatFlags & 0xC000);
    }

    // Vérifie les attributs existants du fichier
    int attrResult = fileAttrOp(filename, 0);  // mode = 0
    bool fileExists = (attrResult != -1);
    int fd = -1;

    // Si le flag 0x0100 est présent, il faut s'assurer que le fichier existe, ou le créer
    if (flags & 0x0100) {
        uint16_t filteredAttrs = attrFlags & allowedAttributes;

        if ((filteredAttrs & 0x180) == 0) {
            handleDosError(1); // Erreur d'attributs manquants
        }

        if (!fileExists) {
            if (err != 2) { // err == 2 → "File not found"
                handleDosError(err);
                return -1;
            }

            // Si flag 0x80, on saute la création
            if ((attrFlags & 0x80) == 0) {
                int tempFd = createFile(attrFlags, filename);
                if (tempFd < 0) return -1;
                closeFile(tempFd);
            }
        }
    }

    // Ouvre le fichier
    fd = openFile(filename, flags);
    if (fd < 0) return fd;

    // Appel ioctl(fd, 0)
    int result = ioctl(fd, 0);
    if (result & 0x80) {
        flags |= 0x2000;

        if (flags & 0x8000) {
            int newVal = (result & 0x00FF) | 0x20;
            ioctl(fd, 1, newVal); // subfunction 1
        }
    } else if (flags & 0x0200) {
        testFileWriteability(fd);
    }

    // Mise à jour des attributs si nécessaires
    if ((attrResult & 0x0001) && (flags & 0x0100) && (flags & 0x00F0)) {
        fileAttrOp(filename, 1); // mode = 1 → mise à jour
    }

    // Calcul des flags à injecter dans fdFlags
    uint16_t finalFlags = (flags & 0xF8FF); // On efface les bits 8 et 9

    if (flags & 0x0300) {
        finalFlags |= 0x1000;
    }

    if (attrResult & 0x0001) {
        finalFlags |= 0x0100;
    }

    // Mise à jour dans la table des flags
    g_fileFlags[fd] = finalFlags;

    return fd;
}

int createFile(int attributes, const char* filename) {
    int fd = -1;

    fd = dosCreateFile(attributes, filename);

    if (fd < 0) {
        handleDosError(fd);  // Même nom que dans l'asm
    }

    return fd;
}

int dosCreateFile(int attributes, const char* filename) {
    // À adapter avec des appels système modernes si besoin
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return -1;  // Simule un "erreur DOS"
    }
    file.close();
    return 3;  // Fausse valeur de handle (DOS commence souvent à 3)
}

void readLineToBuffer(FileLike *file, char *outBuf, int maxLen) {
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
    if (!file) {
        return -1;
    }

    // Fast path: buffer has unread data
    if (file->bytesRead > 0) {
        file->bytesRead--;
        char c = *file->bufferStart;
        file->bufferStart++;
        return static_cast<unsigned char>(c);
    }

    // EOF or error or need refill
    if (file->bytesRead < 0 || (file->flags & 0x110) || !(file->flags & 0x01)) {
        file->flags |= 0x10; // Set error/EOF flag
        return -1;
    }

    // Mark as dirty if needed
    file->flags |= 0x80;

    if (file->bufferSize != 0) {
        if (fillTextBuffer(file) == 0) {
            // refill successful, try again
            return readCharFromTextBuffer(file);
        } else {
            return -1;
        }
    } else {
        // No buffer: must read one char into a temporary buffer
        if (file->flags & 0x200) {
            flushAllDirtyTextBuffers();
        }

        char tmpBuf;
        int result = readTextBuffer(file->fd, &tmpBuf, 1);

        if (result != 0) {
            // Maybe CRLF translation
            if (g_pendingChar == '\r' && !(file->flags & 0x40)) {
                return readCharFromTextBuffer(file);
            }
            file->flags &= ~0x20;
            return static_cast<unsigned char>(g_pendingChar);
        } else {
            if (moveFilePointer(file->fd) == 1) {
                file->flags &= ~0x180;
                file->flags |= 0x20;
            } else {
                file->flags |= 0x10; // set error flag
            }
            return -1;
        }
    }
}


int fillTextBuffer(FileLike* file) {
    if (file->flags & 0x0200) {
        flushAllDirtyTextBuffers();
    }

    file->bufferStart = file->buffer;

    int bytesRead = readTextBuffer(file->fd, file->buffer, file->bufferSize);
    file->bytesRead = bytesRead;

    if (bytesRead <= 0) {
        if (file->bytesRead == 0) {
            file->flags &= ~0x0180;
            file->flags |= 0x0020;  // EOF
        } else {
            file->bytesRead = 0;
            file->flags |= 0x0010;  // Error?
        }
        return -1;
    }

    file->flags &= ~0x0020;  // Clear EOF flag
    return 0;
}

int readAndTrackChar(FileLike* file) {
    ++file->bytesRead;
    return readCharFromTextBuffer(file);
}

int readLine(char *outputBuffer, int maxLen, FileLike *file) {
    char *dst = outputBuffer;
    int remaining = maxLen;
    int ch = 0;

    while (true) {
        if (ch == '\n') break;
        if (--remaining <= 0) break;

        if (--file->bytesRead < 0) {
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

int flushTextBuffer(FileLike* file) {
    if (!file) {
        flushAllTextBuffers();  // cas particulier : flush global
        return 0;
    }

    // vérifie si le pointeur du buffer est correct
    if (file->bufferStart != reinterpret_cast<char*>(file)) {
        return -1;
    }

    // Si des données ont été lues dans le tampon
    if (file->bytesRead >= 0) {
        if (!(file->flags & 0x0008)) {  // pas un fichier ouvert en écriture ?
            char* expectedPtr = reinterpret_cast<char*>(file) + 5;
            if (file->bufferStart != expectedPtr) {
                return 0;
            }
        }
        file->bytesRead = 0;

        char* expectedPtr = reinterpret_cast<char*>(file) + 5;
        if (file->bufferStart != expectedPtr) {
            return 0;
        }

        file->bufferStart = file->buffer;
        return 0;
    }

    // On va devoir écrire
    int size = file->bufferSize + file->bytesRead + 1;
    file->bytesRead -= size;
    file->bufferStart = file->buffer;

    int written = writeTextBuffer(file->fd, file->buffer, size);

    if (written == size) {
        return 0;
    }

    if (!(file->flags & 0x0200)) {
        file->flags |= 0x0010; // erreur
    }

    return -1;
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
            moveFilePointerExtended(fd, -count - 1, SEEK_CUR);
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

void readStructuredBlock(FileLike* file, char* outBuf) {
    // Read 3 header lines
    readLineToBuffer(file, outBuf,     0x21); // 33 bytes
    readLineToBuffer(file, outBuf + 0x78, 0x53); // 83 bytes
    readLineToBuffer(file, outBuf + 0x23, 0x53); // 83 bytes

    // Start reading 20 blocks of 0x23 bytes
    char* ptr = outBuf + 0xCD;
    for (int i = 0; i < 0x14; ++i) {
        readLineToBuffer(file, ptr, 0x21); // 33 bytes
        ptr += 0x23; // skip 35 bytes per slot
    }
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
        // Handle invalide
        handleDosError(6); // 6 = "Invalid handle" en DOS
        return -1;
    }

    FILE* file = it->second;
    if (std::fclose(file) != 0) {
        handleDosError(5); // 5 = "Access denied"
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
#ifndef UTIL_H
#define UTIL_H

#include <cstddef>

std::size_t strlen16(const char* str);
char* strncpyPad(char* dest, const char* src, std::size_t maxLen);
const char* strrchr16(const char* str, char ch);
char* strupr16(char* str);
int strlen16_di(const char* str);

#endif // UTIL_H
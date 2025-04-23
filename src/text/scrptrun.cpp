/*
 *******************************************************************************
 *
 * Copyright (C) 1999-2001, International Business Machines
 * Corporation and others.  All Rights Reserved.
 *
 *******************************************************************************
 * file name:  scrptrun.cpp
 *
 * created on: 10/17/2001
 * created by: Eric R. Mader
 *
 * NOTE: This file is copied from ICU.
 * http://source.icu-project.org/repos/icu/icu/trunk/license.html
 */

#pragma GCC diagnostic push
#include <mapnik/warning_ignore.hpp>
#include <unicode/utypes.h>
#include <unicode/uscript.h>
#pragma GCC diagnostic pop

#include <mapnik/text/scrptrun.hpp>

// --- Added includes for diagnostics ---
#include <cstdio>    // For fprintf, stderr, fflush
#include <cstdlib>   // For abort
#include <vector>    // Assuming parenStack uses std::vector based on emplace_back
#include <stdexcept> // Optional: For throwing instead of aborting

// --- Assuming StackElement struct is defined in scrptrun.hpp or accessible ---
// (If not, you might need struct StackElement { int32_t pairIndex; UScriptCode scriptCode; }; here)
// Assuming parenStack is declared in ScriptRun class as std::vector<StackElement> parenStack;

template <class T, std::size_t N>
constexpr std::size_t ARRAY_SIZE(const T (&array)[N]) noexcept
{
    return N;
}

const char ScriptRun::fgClassID=0;

UChar32 ScriptRun::pairedChars[] = {
    0x0028, 0x0029, // ascii paired punctuation
    0x003c, 0x003e,
    0x005b, 0x005d,
    0x007b, 0x007d,
    0x00ab, 0x00bb, // guillemets
    0x2018, 0x2019, // general punctuation
    0x201c, 0x201d,
    0x2039, 0x203a,
    0x3008, 0x3009, // chinese paired punctuation
    0x300a, 0x300b,
    0x300c, 0x300d,
    0x300e, 0x300f,
    0x3010, 0x3011,
    0x3014, 0x3015,
    0x3016, 0x3017,
    0x3018, 0x3019,
    0x301a, 0x301b
};

const int32_t ScriptRun::pairedCharCount = ARRAY_SIZE(pairedChars);
const int32_t ScriptRun::pairedCharPower = 1 << highBit(pairedCharCount);
const int32_t ScriptRun::pairedCharExtra = pairedCharCount - pairedCharPower;

int8_t ScriptRun::highBit(int32_t value)
{
    if (value <= 0) {
        return -32;
    }
    int8_t bit = 0;
    if (value >= 1 << 16) { value >>= 16; bit += 16; }
    if (value >= 1 << 8)  { value >>= 8;  bit += 8;  }
    if (value >= 1 << 4)  { value >>= 4;  bit += 4;  }
    if (value >= 1 << 2)  { value >>= 2;  bit += 2;  }
    if (value >= 1 << 1)  { value >>= 1;  bit += 1;  }
    return bit;
}

int32_t ScriptRun::getPairIndex(UChar32 ch)
{
    int32_t probe = pairedCharPower;
    int32_t index = 0;

    if (ch >= pairedChars[pairedCharExtra]) {
        index = pairedCharExtra;
    }

    while (probe > (1 << 0)) {
        probe >>= 1;

        if (ch >= pairedChars[index + probe]) {
            index += probe;
        }
    }

    if (pairedChars[index] != ch) {
        index = -1;
    }

    return index;
}

UBool ScriptRun::sameScript(int32_t scriptOne, int32_t scriptTwo)
{
    return scriptOne <= USCRIPT_INHERITED || scriptTwo <= USCRIPT_INHERITED || scriptOne == scriptTwo;
}


// --- Diagnostic Macro Definition ---
// Checks condition, prints context and aborts if true.
// Ensure scriptStart is declared before using this inside the loop.
#define CHECK_FATAL(condition, ...) \
    do { \
        if (condition) { \
            fprintf(stderr, "[ScriptRun::next PRE-CRASH DETECTED] "); \
            fprintf(stderr, __VA_ARGS__); \
            /* Print context state just before aborting */ \
            fprintf(stderr, "\n  --> State: scriptStart=%d, scriptEnd=%d, charLimit=%d, parenSP=%d, startSP=%d, parenStack.size()=%zu, charArray=%p\n", \
                    (int)scriptStart, (int)scriptEnd, (int)charLimit, (int)parenSP, (int)startSP, parenStack.size(), (void*)charArray); \
            fflush(stderr); \
            abort(); /* Force stop */ \
        } \
    } while (false)


// --- Modified ScriptRun::next() method ---
UBool ScriptRun::next()
{
    // Check initial state only if obviously fatal
    if (charArray == nullptr) {
         // Need to handle context vars potentially not being initialized here
         fprintf(stderr, "[ScriptRun::next PRE-CRASH DETECTED] FATAL AT ENTRY: charArray is NULL! charLimit=%d, parenSP=%d\n",
                 (int)charLimit, (int)parenSP);
         fflush(stderr);
         abort();
    }

    int32_t startSP  = parenSP;
    UErrorCode error = U_ZERO_ERROR;

    if (scriptEnd >= charLimit) {
        return false;
    }

    scriptCode = USCRIPT_COMMON;

    // Declare scriptStart here for use in CHECK_FATAL context inside loop
    int32_t scriptStart = scriptEnd;

    for (scriptStart = scriptEnd; scriptEnd < charLimit; scriptEnd += 1) {

        // --- Check conditions JUST before the primary suspected crash site ---
        CHECK_FATAL(charArray == nullptr, "FATAL: charArray became NULL!");
        CHECK_FATAL(scriptEnd >= charLimit, "FATAL: Index OOB! scriptEnd=%d >= charLimit=%d", (int)scriptEnd, (int)charLimit);

        // --- Original Crash Line ---
        // If it crashes here WITHOUT a message above, charArray is likely a non-null, invalid pointer (dangling/corrupted).
        UChar high = charArray[scriptEnd];
        UChar32 ch = high;

        // --- Check before reading low surrogate ---
        if (high >= 0xD800 && high <= 0xDBFF && scriptEnd < charLimit - 1)
        {
            int32_t lowSurrogateIndex = scriptEnd + 1;
            // Check pointer and index for the NEXT character read
            CHECK_FATAL(charArray == nullptr, "FATAL: charArray NULL before low surrogate read!");
            CHECK_FATAL(lowSurrogateIndex >= charLimit, "FATAL: Index OOB for low surrogate! index=%d >= charLimit=%d", (int)lowSurrogateIndex, (int)charLimit);

            UChar low = charArray[lowSurrogateIndex]; // Potential crash site #2

            if (low >= 0xDC00 && low <= 0xDFFF) {
                ch = (high - 0xD800) * 0x0400 + low - 0xDC00 + 0x10000;
                scriptEnd += 1; // Increment scriptEnd only AFTER successful read
            }
        }

        UScriptCode sc = uscript_getScript(ch, &error);
        int32_t pairIndex = getPairIndex(ch);

        if (pairIndex >= 0) {
            if ((pairIndex & 1) == 0) { // Open character
                ++parenSP;
                parenStack.emplace_back(pairIndex, scriptCode); // Assumes this is safe
                startSP = parenSP; // Matches user provided code
            } else if (parenSP >= 0) { // Close character
                int32_t pi = pairIndex & ~1;

                // Check accesses inside the close-pair search loop
                while (parenSP >= 0) {
                    // Check index BEFORE accessing parenStack[parenSP] in condition/body
                    CHECK_FATAL(static_cast<size_t>(parenSP) >= parenStack.size(), "FATAL: Index OOB in close-pair search! parenSP=%d >= size=%zu", (int)parenSP, parenStack.size());
                    if(parenStack[parenSP].pairIndex != pi) {
                         parenSP -= 1;
                    } else {
                         break; // Found
                    }
                }

                if (parenSP < startSP) {
                    startSP = parenSP;
                }

                // Check index BEFORE accessing parenStack[parenSP] to get script code
                if (parenSP >= 0) {
                    CHECK_FATAL(static_cast<size_t>(parenSP) >= parenStack.size(), "FATAL: Index OOB getting script code! parenSP=%d >= size=%zu", (int)parenSP, parenStack.size());
                    sc = parenStack[parenSP].scriptCode;
                }
            }
        } // End paired character handling

        if (sameScript(scriptCode, sc)) {
            if (scriptCode <= USCRIPT_INHERITED && sc > USCRIPT_INHERITED) {
                scriptCode = sc;

                // Check access in the script fixup loop
                // Pre-check loop: Verify indices that will be accessed
                 int32_t temp_startSP = startSP;
                 while(temp_startSP < parenSP) {
                    int32_t index_to_access = temp_startSP + 1;
                    CHECK_FATAL(static_cast<size_t>(index_to_access) >= parenStack.size(), "FATAL: Index OOB pre-check in script fixup! index=%d >= size=%zu", (int)index_to_access, parenStack.size());
                    temp_startSP++;
                 }
                 // Original loop - relies on pre-check. Accesses index startSP+1 up to parenSP
                 while (startSP < parenSP) {
                    parenStack[++startSP].scriptCode = scriptCode;
                 }
            } // End inherited script handling

            // Pop stack for close paired character
            if (pairIndex >= 0 && (pairIndex & 1) != 0 && parenSP >= 0) {
                parenSP -= 1;
                startSP -= 1;
                 // Optional: check if startSP becomes excessively negative?
                 // CHECK_FATAL(startSP < -10, "WARNING: startSP unusually low (%d) after pop", (int)startSP);
            }
        } else { // Script break
            if (ch >= 0x10000) { // Broke on surrogate pair
                scriptEnd -= 1;
            }
            break; // Exit for loop
        }
    } // End for loop

    return true;
} // End function
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "manifest.h"

void nyx_manifest_init(NyxManifest* manifest) {
    memset(manifest, 0, sizeof(NyxManifest));
    strcpy(manifest->entry, "lib.nyx"); // default entry point
}

// trim whitespace — the eternal struggle
static char* trimWhitespace(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// strip surrounding quotes
static void stripQuotes(char* dest, const char* src, size_t maxLen) {
    size_t len = strlen(src);
    if (len >= 2 && src[0] == '"' && src[len - 1] == '"') {
        size_t copyLen = len - 2;
        if (copyLen >= maxLen) copyLen = maxLen - 1;
        memcpy(dest, src + 1, copyLen);
        dest[copyLen] = '\0';
    } else {
        size_t copyLen = len;
        if (copyLen >= maxLen) copyLen = maxLen - 1;
        memcpy(dest, src, copyLen);
        dest[copyLen] = '\0';
    }
}

bool nyx_manifest_parse(const char* path, NyxManifest* manifest) {
    FILE* f = fopen(path, "r");
    if (!f) return false;

    nyx_manifest_init(manifest);

    enum { SEC_NONE, SEC_PACKAGE, SEC_PROJECT, SEC_DEPS } section = SEC_NONE;
    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        char* trimmed = trimWhitespace(line);

        // skip blanks and comments
        if (*trimmed == '\0' || *trimmed == '#') continue;

        // section headers like [package], [deps]
        if (*trimmed == '[') {
            if (strncmp(trimmed, "[package]", 9) == 0) {
                section = SEC_PACKAGE;
            } else if (strncmp(trimmed, "[project]", 9) == 0) {
                section = SEC_PROJECT;
                manifest->isProject = true;
            } else if (strncmp(trimmed, "[dependencies]", 14) == 0) {
                section = SEC_DEPS;
            } else {
                section = SEC_NONE; // unknown section, skip
            }
            continue;
        }

        // key = value pairs
        char* eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = trimWhitespace(trimmed);
        char* val = trimWhitespace(eq + 1);

        if (section == SEC_PACKAGE || section == SEC_PROJECT) {
            if (strcmp(key, "name") == 0) {
                stripQuotes(manifest->name, val, sizeof(manifest->name));
            } else if (strcmp(key, "version") == 0) {
                stripQuotes(manifest->version, val, sizeof(manifest->version));
            } else if (strcmp(key, "entry") == 0) {
                stripQuotes(manifest->entry, val, sizeof(manifest->entry));
            } else if (strcmp(key, "description") == 0) {
                stripQuotes(manifest->description, val, sizeof(manifest->description));
            } else if (strcmp(key, "author") == 0) {
                stripQuotes(manifest->author, val, sizeof(manifest->author));
            } else if (strcmp(key, "repository") == 0) {
                stripQuotes(manifest->repository, val, sizeof(manifest->repository));
            }
        } else if (section == SEC_DEPS) {
            if (manifest->depCount < NYX_MAX_DEPS) {
                NyxDependency* dep = &manifest->deps[manifest->depCount++];
                stripQuotes(dep->name, key, sizeof(dep->name));
                stripQuotes(dep->version, val, sizeof(dep->version));
            }
        }
    }

    fclose(f);
    return true;
}

const char* nyx_manifest_dep_version(const NyxManifest* manifest, const char* name) {
    for (int i = 0; i < manifest->depCount; i++) {
        if (strcmp(manifest->deps[i].name, name) == 0) {
            return manifest->deps[i].version;
        }
    }
    return NULL;
}

#ifndef NYX_MANIFEST_H
#define NYX_MANIFEST_H

#include <stdbool.h>

// Maximum number of dependencies in a project manifest
#define NYX_MAX_DEPS 64

typedef struct {
    char name[128];
    char version[64];
} NyxDependency;

typedef struct {
    // [package] section (for library manifests)
    char name[128];
    char version[64];
    char entry[256];          // entry point file (default: "lib.nyx")
    char description[512];
    char author[128];
    char repository[512];

    // [project] section (for project manifests)
    bool isProject;           // true if [project] section found

    // [dependencies] section
    NyxDependency deps[NYX_MAX_DEPS];
    int depCount;
} NyxManifest;

// Initialize manifest with defaults
void nyx_manifest_init(NyxManifest* manifest);

// Parse a nyx.toml file. Returns true on success.
bool nyx_manifest_parse(const char* path, NyxManifest* manifest);

// Find a dependency version by name. Returns NULL if not found.
const char* nyx_manifest_dep_version(const NyxManifest* manifest, const char* name);

#endif

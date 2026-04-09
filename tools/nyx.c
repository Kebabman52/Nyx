#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir_p(path) _mkdir(path)
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <dirent.h>
#define mkdir_p(path) mkdir(path, 0755)
#define PATH_SEP '/'
#endif

#include "common.h"
#include "manifest.h"
#include "repl.h"
#include "vm.h"

// from api.c
extern int nyx_compile_to_file(const char* sourcePath, const char* outputPath);
extern int nyx_build(const char* inputDir, const char* outputPath);
extern NyxResult nyx_run_compiled(const char* path);

// version string comes from common.h

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "nyx: could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "nyx: not enough memory to read \"%s\".\n", path);
        fclose(file);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    nyx_vm_set_file(path);
    NyxResult result = nyx_vm_interpret(source);
    free(source);

    if (result == NYX_COMPILE_ERROR) exit(65);
    if (result == NYX_RUNTIME_ERROR) exit(70);
}

// ─── Package Management (the mini package manager nobody asked for) ─────────

static char detectedNyxHome[1024] = "";

static const char* getNyxHome(void) {
    // 1. env var wins
    const char* home = getenv("NYX_HOME");
    if (home != NULL && home[0] != '\0') return home;

    // 2. auto-detect from where the exe lives
    if (detectedNyxHome[0] != '\0') return detectedNyxHome;

    return NULL;
}

static void detectNyxHome(const char* argv0) {
    // if they set it manually, respect that
    const char* env = getenv("NYX_HOME");
    if (env != NULL && env[0] != '\0') return;

#ifdef _WIN32
    // Windows: GetModuleFileName for the exe path
    char exePath[1024];
    DWORD len = GetModuleFileNameA(NULL, exePath, sizeof(exePath));
    if (len > 0 && len < sizeof(exePath)) {
        // strip the exe name
        for (int i = (int)len - 1; i >= 0; i--) {
            if (exePath[i] == '\\' || exePath[i] == '/') {
                exePath[i] = '\0';
                break;
            }
        }
        snprintf(detectedNyxHome, sizeof(detectedNyxHome), "%s", exePath);
    }
#else
    // POSIX: try /proc/self/exe, fall back to argv[0]
    char exePath[1024];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        // strip exe name
        for (int i = (int)len - 1; i >= 0; i--) {
            if (exePath[i] == '/') {
                exePath[i] = '\0';
                break;
            }
        }
        snprintf(detectedNyxHome, sizeof(detectedNyxHome), "%s", exePath);
    } else if (argv0 != NULL) {
        // argv[0] fallback — sketchy but sometimes it's all we've got
        const char* lastSlash = NULL;
        for (const char* p = argv0; *p; p++) {
            if (*p == '/') lastSlash = p;
        }
        if (lastSlash != NULL) {
            int dirLen = (int)(lastSlash - argv0);
            snprintf(detectedNyxHome, sizeof(detectedNyxHome), "%.*s", dirLen, argv0);
        }
    }
#endif
    (void)argv0;
}

static void ensureDir(const char* path) {
    // mkdir -p, hand-rolled because C doesn't have nice things
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            mkdir_p(tmp);
            *p = PATH_SEP;
        }
    }
    mkdir_p(tmp);
}

static bool isGitUrl(const char* str) {
    return strstr(str, "://") != NULL || strstr(str, "git@") != NULL;
}

// look up a package in the registry (NYX_HOME/registry.txt)
// format: name=https://github.com/user/repo.git
static bool lookupRegistry(const char* nyxHome, const char* name, char* urlOut, size_t urlMax) {
    char regPath[1024];
    snprintf(regPath, sizeof(regPath), "%s/registry.txt", nyxHome);
    FILE* f = fopen(regPath, "r");
    if (!f) return false;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        // Trim
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        // Trim key
        char* key = line;
        while (*key == ' ') key++;
        char* keyEnd = eq - 1;
        while (keyEnd > key && *keyEnd == ' ') *keyEnd-- = '\0';

        if (strcmp(key, name) == 0) {
            char* val = eq + 1;
            while (*val == ' ') val++;
            snprintf(urlOut, urlMax, "%s", val);
            fclose(f);
            return true;
        }
    }
    fclose(f);
    return false;
}

static int cmdInstall(int argc, const char* argv[]) {
    // nyx install <name|url> [version]
    if (argc < 3) {
        fprintf(stderr, "Usage: nyx install <package|git-url> [version]\n");
        return 1;
    }

    const char* nyxHome = getNyxHome();
    if (!nyxHome) return 1;

    const char* target = argv[2];
    const char* version = (argc >= 4) ? argv[3] : NULL;

    char gitUrl[1024];
    char packageName[128];

    if (isGitUrl(target)) {
        // direct git URL — just clone it
        snprintf(gitUrl, sizeof(gitUrl), "%s", target);
        // extract package name from URL
        const char* lastSlash = target;
        for (const char* p = target; *p; p++) {
            if (*p == '/') lastSlash = p;
        }
        snprintf(packageName, sizeof(packageName), "%s", lastSlash + 1);
        // strip .git suffix
        char* dotGit = strstr(packageName, ".git");
        if (dotGit) *dotGit = '\0';
    } else {
        // look up in the registry
        snprintf(packageName, sizeof(packageName), "%s", target);
        if (!lookupRegistry(nyxHome, target, gitUrl, sizeof(gitUrl))) {
            fprintf(stderr, "nyx: package '%s' not found in registry.\n", target);
            fprintf(stderr, "     Use a git URL or run 'nyx registry update'.\n");
            return 1;
        }
    }

    // clone to a temp directory first
    char tempDir[1024];
    snprintf(tempDir, sizeof(tempDir), "%s/libs/.tmp_%s", nyxHome, packageName);

    // nuke any leftover temp dir
    char rmCmd[1200];
#ifdef _WIN32
    snprintf(rmCmd, sizeof(rmCmd), "rmdir /S /Q \"%s\" 2>NUL", tempDir);
#else
    snprintf(rmCmd, sizeof(rmCmd), "rm -rf \"%s\" 2>/dev/null", tempDir);
#endif
    system(rmCmd);

    // Git clone
    char cmd[2048];
    printf("Installing %s...\n", packageName);
    if (version != NULL) {
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 --branch \"%s\" \"%s\" \"%s\" 2>&1",
                 version, gitUrl, tempDir);
    } else {
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 \"%s\" \"%s\" 2>&1", gitUrl, tempDir);
    }

    int gitResult = system(cmd);
    if (gitResult != 0) {
        fprintf(stderr, "nyx: git clone failed.\n");
        return 1;
    }

    // read nyx.toml to figure out the version
    char manifestPath[1024];
    snprintf(manifestPath, sizeof(manifestPath), "%s/nyx.toml", tempDir);
    NyxManifest manifest;
    nyx_manifest_init(&manifest);

    if (nyx_manifest_parse(manifestPath, &manifest)) {
        if (version == NULL && manifest.version[0] != '\0') {
            version = manifest.version;
        }
    }

    if (version == NULL || version[0] == '\0') {
        version = "0.0.0"; // fallback
    }

    // move to final resting place: NYX_HOME/libs/<name>/<version>/
    char installDir[1024];
    snprintf(installDir, sizeof(installDir), "%s/libs/%s/%s", nyxHome, packageName, version);
    ensureDir(installDir);

    // out with the old, in with the new
#ifdef _WIN32
    snprintf(rmCmd, sizeof(rmCmd), "rmdir /S /Q \"%s\" 2>NUL", installDir);
    system(rmCmd);
    snprintf(cmd, sizeof(cmd), "move \"%s\" \"%s\"", tempDir, installDir);
#else
    snprintf(rmCmd, sizeof(rmCmd), "rm -rf \"%s\"", installDir);
    system(rmCmd);
    snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s\"", tempDir, installDir);
#endif
    system(cmd);

    printf("Installed %s@%s -> %s\n", packageName, version, installDir);
    return 0;
}

static int cmdUninstall(int argc, const char* argv[]) {
    // nyx uninstall <name> [version]
    if (argc < 3) {
        fprintf(stderr, "Usage: nyx uninstall <package> [version]\n");
        return 1;
    }

    const char* nyxHome = getNyxHome();
    if (!nyxHome) return 1;

    const char* name = argv[2];
    const char* version = (argc >= 4) ? argv[3] : NULL;

    char targetDir[1024];
    if (version != NULL) {
        snprintf(targetDir, sizeof(targetDir), "%s/libs/%s/%s", nyxHome, name, version);
    } else {
        snprintf(targetDir, sizeof(targetDir), "%s/libs/%s", nyxHome, name);
    }

    char cmd[1200];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "rmdir /S /Q \"%s\" 2>NUL", targetDir);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", targetDir);
#endif
    int result = system(cmd);
    if (result == 0) {
        if (version)
            printf("Uninstalled %s@%s\n", name, version);
        else
            printf("Uninstalled %s (all versions)\n", name);
    } else {
        fprintf(stderr, "nyx: could not uninstall '%s'.\n", name);
    }
    return result;
}

static int cmdList(void) {
    const char* nyxHome = getNyxHome();
    if (!nyxHome) return 1;

    char libsDir[1024];
    snprintf(libsDir, sizeof(libsDir), "%s/libs", nyxHome);

    printf("Installed packages (%s/libs/):\n\n", nyxHome);

#ifdef _WIN32
    // Windows: FindFirstFile/FindNextFile — the Win32 API experience
    char searchPath[1024];
    snprintf(searchPath, sizeof(searchPath), "%s/*", libsDir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("  (none)\n");
        return 0;
    }
    int count = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0] == '.') continue;
            // package dir — list its versions
            char pkgDir[1024];
            snprintf(pkgDir, sizeof(pkgDir), "%s/%s/*", libsDir, fd.cFileName);
            WIN32_FIND_DATAA vfd;
            HANDLE vFind = FindFirstFileA(pkgDir, &vfd);
            if (vFind != INVALID_HANDLE_VALUE) {
                do {
                    if ((vfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && vfd.cFileName[0] != '.') {
                        printf("  %s@%s\n", fd.cFileName, vfd.cFileName);
                        count++;
                    }
                } while (FindNextFileA(vFind, &vfd));
                FindClose(vFind);
            }
            if (count == 0) {
                // might be a flat lib without version dirs
                char manifestPath[1024];
                snprintf(manifestPath, sizeof(manifestPath), "%s/%s/nyx.toml", libsDir, fd.cFileName);
                NyxManifest m;
                if (nyx_manifest_parse(manifestPath, &m) && m.version[0]) {
                    printf("  %s@%s\n", fd.cFileName, m.version);
                } else {
                    printf("  %s\n", fd.cFileName);
                }
                count++;
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    // POSIX: opendir/readdir — at least it's readable
    DIR* dir = opendir(libsDir);
    if (!dir) {
        printf("  (none)\n");
        return 0;
    }
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char pkgDir[1024];
        snprintf(pkgDir, sizeof(pkgDir), "%s/%s", libsDir, entry->d_name);
        DIR* vdir = opendir(pkgDir);
        if (vdir) {
            struct dirent* ventry;
            while ((ventry = readdir(vdir)) != NULL) {
                if (ventry->d_name[0] == '.') continue;
                printf("  %s@%s\n", entry->d_name, ventry->d_name);
                count++;
            }
            closedir(vdir);
        }
    }
    closedir(dir);
#endif

    if (count == 0) printf("  (none)\n");
    return 0;
}

static int cmdRegistryUpdate(void) {
    const char* nyxHome = getNyxHome();
    if (!nyxHome) return 1;

    char regPath[1024];
    snprintf(regPath, sizeof(regPath), "%s/registry.txt", nyxHome);

    // make sure libs dir exists
    char libsDir[1024];
    snprintf(libsDir, sizeof(libsDir), "%s/libs", nyxHome);
    ensureDir(libsDir);

    printf("Updating registry...\n");

    // try every download method we can think of
    char cmd[2048];
    int result = -1;

    // Try curl first
    snprintf(cmd, sizeof(cmd),
        "curl -sL \"https://nyx.nemesistech.ee/registry.txt\" -o \"%s\"",
        regPath);
    result = system(cmd);

#ifdef _WIN32
    // Windows fallback: PowerShell Invoke-WebRequest. desperate times
    if (result != 0) {
        snprintf(cmd, sizeof(cmd),
            "powershell -NoProfile -Command \""
            "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
            "Invoke-WebRequest -Uri 'https://nyx.nemesistech.ee/registry.txt' -OutFile '%s'"
            "\"", regPath);
        result = system(cmd);
    }
#else
    // Linux/Mac fallback: wget
    if (result != 0) {
        snprintf(cmd, sizeof(cmd),
            "wget -q \"https://nyx.nemesistech.ee/registry.txt\" -O \"%s\"",
            regPath);
        result = system(cmd);
    }
#endif

    if (result == 0) {
        printf("Registry updated: %s\n", regPath);
    } else {
        fprintf(stderr, "nyx: could not fetch registry. Check your network.\n");
        fprintf(stderr, "     Tried: curl, %s\n",
#ifdef _WIN32
            "powershell"
#else
            "wget"
#endif
        );
    }
    return result;
}

static void printUsage(void) {
    printf("Nyx %s - Nemesis Technologies\n\n", NYX_VERSION_STRING);
    printf("Usage:\n");
    printf("  nyx                         Start the REPL\n");
    printf("  nyx <script.nyx>            Run a script\n");
    printf("  nyx build <dir> -o <out>    Compile project to bytecode\n");
    printf("  nyx compile <file> -o <out> Compile a file to bytecode\n");
    printf("  nyx run <file.nyxc>         Run compiled bytecode\n");
    printf("\n");
    printf("Package management:\n");
    printf("  nyx install <pkg>           Install latest from registry\n");
    printf("  nyx install <pkg> <ver>     Install specific version\n");
    printf("  nyx install <git-url>       Install from git URL\n");
    printf("  nyx uninstall <pkg>         Remove all versions\n");
    printf("  nyx uninstall <pkg> <ver>   Remove specific version\n");
    printf("  nyx list                    Show installed packages\n");
    printf("  nyx registry update         Update package registry\n");
    printf("\n");
    printf("Flags:\n");
    printf("  --profile                   Show profiler report after execution\n");
}

int main(int argc, const char* argv[]) {
    detectNyxHome(argv[0]);
    nyx_vm_init();
    // tell the VM where home is
    if (detectedNyxHome[0] != '\0' && vm.nyxHome[0] == '\0') {
        nyx_vm_set_home(detectedNyxHome);
    }

    // --profile can appear anywhere in args
    bool profiling = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0) {
            profiling = true;
            vm.profilingEnabled = true;
            nyx_profiler_init();
            // Remove from argv so it doesn't interfere with command parsing
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
            argc--;
            i--;
        }
    }

    if (argc == 1) {
        nyx_repl_run();
    } else if (argc >= 2 && strcmp(argv[1], "install") == 0) {
        int result = cmdInstall(argc, argv);
        nyx_vm_free();
        return result;
    } else if (argc >= 2 && strcmp(argv[1], "uninstall") == 0) {
        int result = cmdUninstall(argc, argv);
        nyx_vm_free();
        return result;
    } else if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        int result = cmdList();
        nyx_vm_free();
        return result;
    } else if (argc >= 3 && strcmp(argv[1], "registry") == 0 && strcmp(argv[2], "update") == 0) {
        int result = cmdRegistryUpdate();
        nyx_vm_free();
        return result;
    } else if (argc >= 3 && strcmp(argv[1], "run") == 0) {
        NyxResult result = nyx_run_compiled(argv[2]);
        if (result != NYX_OK) exit(70);
    } else if (argc >= 4 && strcmp(argv[1], "compile") == 0) {
        const char* input = argv[2];
        const char* output = NULL;

        for (int i = 3; i < argc - 1; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                output = argv[i + 1];
                break;
            }
        }

        if (output == NULL) {
            // Default output: replace .nyx with .nyxc
            static char outBuf[1024];
            strncpy(outBuf, input, sizeof(outBuf) - 2);
            size_t len = strlen(outBuf);
            if (len > 4 && strcmp(outBuf + len - 4, ".nyx") == 0) {
                strcat(outBuf, "c"); // .nyx -> .nyxc
            } else {
                strcat(outBuf, ".nyxc");
            }
            output = outBuf;
        }

        int result = nyx_compile_to_file(input, output);
        if (result == 0) {
            printf("Compiled: %s -> %s\n", input, output);
        }
        nyx_vm_free();
        return result;
    } else if (argc >= 4 && strcmp(argv[1], "build") == 0) {
        const char* dir = argv[2];
        const char* output = NULL;

        for (int i = 3; i < argc - 1; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                output = argv[i + 1];
                break;
            }
        }

        if (output == NULL) {
            output = "out.nyxc";
        }

        int result = nyx_build(dir, output);
        if (result == 0) {
            printf("Built: %s -> %s\n", dir, output);
        }
        nyx_vm_free();
        return result;
    } else if (argc == 2) {
        const char* arg = argv[1];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            printUsage();
        } else if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            printf("Nyx %s\n", NYX_VERSION_STRING);
        } else {
            // Check if it's a .nyxc file
            size_t len = strlen(arg);
            if (len > 5 && strcmp(arg + len - 5, ".nyxc") == 0) {
                NyxResult result = nyx_run_compiled(arg);
                if (result != NYX_OK) exit(70);
            } else {
                runFile(arg);
            }
        }
    } else {
        printUsage();
        nyx_vm_free();
        return 64;
    }

    if (profiling) nyx_profiler_report();

    nyx_vm_free();
    return 0;
}

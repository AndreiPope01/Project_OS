#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#define MAX_PATH_LENGTH 1024
#define MAX_ENTRIES 1000
#define MAX_DIRECTORIES 10

struct Entry {
    char name[MAX_PATH_LENGTH];
    mode_t mode;
    off_t size;
    time_t last_modified;
};

struct DirectorySnapshot {
    char directory[MAX_PATH_LENGTH];
    struct Entry entries[MAX_ENTRIES];
    int num_entries;
};

struct DirectorySnapshot snapshots[MAX_DIRECTORIES];

int num_snapshots = 0;

void recordEntry(struct DirectorySnapshot *snapshot, const char *name, const struct stat *statbuf) {
    if (snapshot->num_entries >= MAX_ENTRIES) {
        char errMsg[MAX_PATH_LENGTH + 50];
        snprintf(errMsg, sizeof(errMsg), "Too many entries in directory %s\n", snapshot->directory);
        fputs(errMsg, stderr);
        return;
    }

    struct Entry *entry = &snapshot->entries[snapshot->num_entries++];
    strncpy(entry->name, name, MAX_PATH_LENGTH);
    entry->mode = statbuf->st_mode;
    entry->size = statbuf->st_size;
    entry->last_modified = statbuf->st_mtime;
}

void traverseDirectory(struct DirectorySnapshot *snapshot, const char *path, const char *isolatedDir) {
    DIR *dir;
    struct dirent *entry;
    struct stat fileStat;
    
    dir = opendir(path);
    
    if (!dir) {
        char errMsg[MAX_PATH_LENGTH + 50];
        snprintf(errMsg, sizeof(errMsg), "Cannot open directory %s\n", path);
        fputs(errMsg, stderr);
        return;
    }

    recordEntry(snapshot, path, &fileStat);

    while ((entry = readdir(dir)) != NULL) {
        char filePath[MAX_PATH_LENGTH];
        snprintf(filePath, sizeof(filePath), "%s/%s", path, entry->d_name);

        if (lstat(filePath, &fileStat) < 0) {
            char errMsg[MAX_PATH_LENGTH + 50];
            snprintf(errMsg, sizeof(errMsg), "Failed to get file status for %s\n", filePath);
            fputs(errMsg, stderr);
            continue;
        }

        if (S_ISDIR(fileStat.st_mode)) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                traverseDirectory(snapshot, filePath, isolatedDir);
            }
        } else {
            recordEntry(snapshot, entry->d_name, &fileStat);
            verifyPermissionsAndIsolate(filePath, isolatedDir);
        }
    }
    closedir(dir);
}

void verifyPermissionsAndIsolate(const char *filePath, const char *isolatedDir) {
    struct stat fileStat;
    if (lstat(filePath, &fileStat) < 0) {
        perror("Failed to get file status");
        return;
    }

    // Check if all permissions are missing
    if ((fileStat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execl("/bin/bash", "/bin/bash", "verify_for_malicious.sh", filePath, isolatedDir, NULL);
            // If execl fails, handle it accordingly
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            // Parent process
            int status;
            waitpid(pid, &status, 0);
            // Move the file to isolated directory if deemed dangerous
            if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
                char isolatedPath[MAX_PATH_LENGTH];
                snprintf(isolatedPath, sizeof(isolatedPath), "%s/%s", isolatedDir, basename(filePath));
                if (rename(filePath, isolatedPath) == -1) {
                    perror("Failed to move file to isolated directory");
                }
            }
        } else {
            perror("Failed to fork process");
        }
    }
}

void printSnapshot(struct DirectorySnapshot *snapshot, const char *outputFileName) {
    FILE *outputFile = fopen(outputFileName, "w");
    if (!outputFile) {
        char errMsg[MAX_PATH_LENGTH + 50];
        snprintf(errMsg, sizeof(errMsg), "Failed to open output file %s\n", outputFileName);
        fputs(errMsg, stderr);
        return;
    }

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "Snapshot of directory: %s\n", snapshot->directory);
    fputs(buffer, outputFile);
    
    for (int i = 0; i < snapshot->num_entries; ++i) {
        struct Entry *entry = &snapshot->entries[i];
        snprintf(buffer, sizeof(buffer), "Name: %s, Mode: %o, Size: %lld bytes, Last Modified: %s", 
            entry->name, entry->mode, (long long)entry->size, ctime(&entry->last_modified));
        fputs(buffer, outputFile);
    }

    fclose(outputFile);
}

void updateSnapshot(const char *directory, const char *isolatedDir) {
    int found = 0;
    for (int i = 0; i < num_snapshots; ++i) {
        if (strcmp(snapshots[i].directory, directory) == 0) {
            found = 1;
            traverseDirectory(&snapshots[i], directory, isolatedDir);
            printSnapshot(&snapshots[i], "snapshot.txt");
            break;
        }
    }

    if (!found) {
        if (num_snapshots >= MAX_DIRECTORIES) {
            char errMsg[MAX_PATH_LENGTH + 50];
            snprintf(errMsg, sizeof(errMsg), "Maximum number of directories reached\n");
            fputs(errMsg, stderr);
            return;
        }
        
        strncpy(snapshots[num_snapshots].directory, directory, MAX_PATH_LENGTH);
        snapshots[num_snapshots].num_entries = 0;
        traverseDirectory(&snapshots[num_snapshots], directory, isolatedDir);
        printSnapshot(&snapshots[num_snapshots], "snapshot.txt");
        num_snapshots++;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > MAX_DIRECTORIES + 4) {
        char errMsg[MAX_PATH_LENGTH + 50];
        snprintf(errMsg, sizeof(errMsg), "Usage: %s -o <output_dir> -s <isolated_space_dir> <dir1> <dir2> ... <dir%d>\n", argv[0], MAX_DIRECTORIES);
        fputs(errMsg, stderr);
        return 1;
    }

    const char *outputDir;
    const char *isolatedDir;
    int directoriesStartIndex = 3;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                outputDir = argv[i + 1];
                i++; // Skip next argument
            } else {
                fputs("Missing argument for output directory\n", stderr);
                return 1;
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) {
                isolatedDir = argv[i + 1];
                i++; // Skip next argument
            } else {
                fputs("Missing argument for isolated space directory\n", stderr);
                return 1;
            }
        } else {
            directoriesStartIndex = i;
            break;
        }
    }

    if (access(outputDir, F_OK) == -1) {
        perror("Output directory does not exist");
        return 1;
    }

    if (access(isolatedDir, F_OK) == -1) {
        perror("Isolated space directory does not exist");
        return 1;
    }

    for (int i = directoriesStartIndex; i < argc; ++i) {
        updateSnapshot(argv[i], isolatedDir);
    }

    return 0;
}

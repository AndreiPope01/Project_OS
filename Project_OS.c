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

void traverseDirectory(struct DirectorySnapshot *snapshot, const char *path) {
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
                traverseDirectory(snapshot, filePath);
            }
        } else {
            recordEntry(snapshot, entry->d_name, &fileStat);
        }
    }
    closedir(dir);
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

void updateSnapshot(const char *directory) {
    int found = 0;
    for (int i = 0; i < num_snapshots; ++i) {
        if (strcmp(snapshots[i].directory, directory) == 0) {
            found = 1;
            traverseDirectory(&snapshots[i], directory);
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
        traverseDirectory(&snapshots[num_snapshots], directory);
        printSnapshot(&snapshots[num_snapshots], "snapshot.txt");
        num_snapshots++;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > MAX_DIRECTORIES + 1) {
        char errMsg[MAX_PATH_LENGTH + 50];
        snprintf(errMsg, sizeof(errMsg), "Usage: %s <directory1> <directory2> ... <directory%d>\n", argv[0], MAX_DIRECTORIES);
        fputs(errMsg, stderr);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        updateSnapshot(argv[i]);
    }

    return 0;
}

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
        perror("Cannot open directory");
        return;
    }

    recordEntry(snapshot, path, &fileStat);

    while ((entry = readdir(dir)) != NULL) {
        char filePath[MAX_PATH_LENGTH];
        snprintf(filePath, sizeof(filePath), "%s/%s", path, entry->d_name);

        if (lstat(filePath, &fileStat) < 0) {
            perror("Failed to get file status");
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
        perror("Failed to open output file");
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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <directory_path>\n", argv[0]);
        return 1;
    }

    struct DirectorySnapshot snapshot;
    strncpy(snapshot.directory, argv[1], MAX_PATH_LENGTH);
    snapshot.num_entries = 0;

    traverseDirectory(&snapshot, argv[1]);
    printSnapshot(&snapshot, "snapshot.txt");

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

struct File {
    char name[256];
    mode_t mode;
    off_t size;
};

void traverseDirectory(const char *path) {
    DIR *dir;
    struct dirent *entry;
    struct stat fileStat;
    
    dir = opendir(path);
    
    if (!dir) {
        fprintf(stderr, "Cannot open directory %s\n", path);
        return;
    }
    
    printf("Snapshot of directory: %s\n", path);
    while ((entry = readdir(dir)) != NULL) {
        char filePath[512];
        struct File file;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(filePath, sizeof(filePath), "%s/%s", path, entry->d_name);

        if (lstat(filePath, &fileStat) < 0) {
            fprintf(stderr, "Failed to get file status for %s\n", filePath);
            continue;
        }

        strncpy(file.name, entry->d_name, sizeof(file.name));
        file.mode = fileStat.st_mode;
        file.size = fileStat.st_size;

        printf("Name: %s, Mode: %o, Size: %lld bytes\n", file.name, file.mode, (long long)file.size);

        if (S_ISDIR(file.mode)) {
            traverseDirectory(filePath);
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <directory_path>\n", argv[0]);
        return 1;
    }

    char *path = argv[1];
    traverseDirectory(path);

    return 0;
}

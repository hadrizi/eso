#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#define PVD_OFFSET 0x8000
#define DIRECTORY_SECTOR_OFFSET 68

int exit_code = EXIT_SUCCESS;

void err_quit(const char *msg, ...) {
    va_list args;
    va_start(args, msg);

    vprintf(msg, args);
    exit(EXIT_FAILURE);
}

struct pvd {
    char system_id[32];
    char volume_id[32];
    char volume_set_id[128];

    u_int32_t volume_space_size;
    u_int16_t volume_set_size;
    u_int16_t volume_number;

    u_int16_t logical_block_size;
};

struct data {
    char* id;

    char datetime[256];
    u_int8_t flags;

    u_int8_t entry_size;
    u_int32_t extent_size;
    u_int32_t extent_location;
};

struct directory {
    struct data data;
    struct directory* next;
    struct directory* extent;
};

typedef struct pvd pvd;
typedef struct directory directory;
typedef struct data data;

directory* new_directory(data data) {
    directory* new_directory = malloc(sizeof(directory));

    if (new_directory) {
        new_directory->next = NULL;
        new_directory->extent = NULL;
        new_directory->data = data;
    }

    return new_directory;
}

directory* add_next(directory* d, data data) {
    if (d == NULL)
        return NULL;

    while (d->next)
        d = d->next;

    return (d->next = new_directory(data));
}

directory* add_extent(directory* d, data data) {
    if (d == NULL)
        return NULL;

    if (d->extent)
        return add_next(d->extent, data);
    else
        return (d->extent = new_directory(data));
}

int load_pvd(char* iso, pvd* buff) {
    int8_t descriptor_type = iso[PVD_OFFSET];
    if (descriptor_type != 1)
        return -1;

    int idx = 31;
    memcpy(buff->system_id, (iso + PVD_OFFSET + 8), 32);
    while (buff->system_id[idx] == ' ' && idx > 0)
        idx--;
    buff->system_id[idx+1] = '\0';

    idx = 31;
    memcpy(buff->volume_id, (iso + PVD_OFFSET + 40), 32);
    while (buff->volume_id[idx] == ' ' && idx > 0)
        idx--;
    buff->volume_id[idx+1] = '\0';

    idx = 127;
    memcpy(buff->volume_set_id, (iso + PVD_OFFSET + 190), 128);
    while (buff->volume_set_id[idx] == ' ' && idx > 0)
        idx--;
    buff->volume_set_id[idx+1] = '\0';

    memcpy(&buff->volume_space_size, (iso + PVD_OFFSET + 80), 4);
    memcpy(&buff->volume_set_size, (iso + PVD_OFFSET + 120), 2);
    memcpy(&buff->volume_number, (iso + PVD_OFFSET + 124), 2);

    memcpy(&buff->logical_block_size, (iso + PVD_OFFSET + 128), 2);
    
    return 0;
}

void traverse(directory* root) {
    if (root == NULL)
        return;

    while(root) {
        printf("%s -> ", root->data.id);
        if (root->extent) {
            printf("\n");
            traverse(root->extent);
        }
        root = root->next;
    }
}

bool test_bit(u_int8_t v, int p) {
    return ((v) & (1<<p));
}

bool is_folder(u_int8_t flags) {
    test_bit(flags, 1);
}

data parse_data(char* iso, pvd* pvdbuff, u_int32_t offset) {
    data new_data;
    
    size_t id_length = *(iso + offset + 32);
    new_data.id = malloc((id_length+1)*sizeof(char));
    memcpy(new_data.id, iso + offset + 33, id_length);
    new_data.id[id_length] = '\0';
    if (id_length == 1 && new_data.id[0] == 0x00)
        new_data.id[0] = '/';

    memcpy(&new_data.extent_location, iso + offset + 2, 4);

    new_data.flags = *(iso + offset + 25);
    new_data.entry_size = *(iso + offset);
    
    int year = 1900 + *(iso + offset + 18);
    int month = *(iso + offset + 18 + 1);
    int day = *(iso + offset + 18 + 2);
    int hour = *(iso + offset + 18 + 3);
    int minute = *(iso + offset + 18 + 4);
    int second = *(iso + offset + 18 + 5);

    snprintf(
        new_data.datetime, 
        256, "%d.%d.%d %d:%d:%d",
        month, day, year,
        hour, minute, second
    );

    return new_data;
}

void build_extent(char* iso, pvd* pvdbuff, directory* d, u_int32_t offset) {
    if (d == NULL)
        return;


    if (is_folder(d->data.flags)) {
        u_int32_t extent_offset 
            = pvdbuff->logical_block_size * d->data.extent_location + DIRECTORY_SECTOR_OFFSET;
        data data = parse_data(iso, pvdbuff, extent_offset);
        add_extent(d, data);
        build_extent(iso, pvdbuff, d->extent, extent_offset + d->extent->data.entry_size);
    }

    if (*(iso + offset) == 0)
        return;

    data data = parse_data(iso, pvdbuff, offset);
    add_next(d, data);
    build_extent(iso, pvdbuff, d->next, offset + d->next->data.entry_size);
}

directory* build_directory_tree(char* iso, pvd* pvdbuff) {
    u_int32_t root_lda;
    memcpy(&root_lda, iso + PVD_OFFSET + 156 + 2, 4);

    u_int32_t root_offset 
        = pvdbuff->logical_block_size * root_lda + DIRECTORY_SECTOR_OFFSET;

    data directory_data = parse_data(iso, pvdbuff, PVD_OFFSET+156);
    directory* root = new_directory(directory_data);

    if (*(iso + root_offset) == 0)
        return root;
    
    directory_data = parse_data(iso, pvdbuff, root_offset);
    add_extent(root, directory_data);
    build_extent(iso, pvdbuff, root->extent, root_offset + root->extent->data.entry_size);

    return root;
}

void free_directory_tree(directory* d) {
    if (d == NULL)
        return;
    
    free_directory_tree(d->next);
    free_directory_tree(d->extent);

    free(d->data.id);
    free(d);
}

directory* search_tree_by_id(directory* d, char* id) {
    if (d == NULL)
        return NULL;

    if (strcmp(d->data.id, id) == 0)
        return d;

    directory* result = search_tree_by_id(d->next, id);
    if (result)
        return result;

    result = search_tree_by_id(d->extent, id);
    return result;
}

char* flags_to_str(u_int8_t flags) {
    char* flags_str = malloc(7*sizeof(char));
    
    flags_str[0] = '.';
    if(test_bit(flags, 0))
        flags_str[0] = 'h';

    flags_str[1] = '.';
    if(test_bit(flags, 1))
        flags_str[1] = 'f';

    flags_str[2] = '.';
    if(test_bit(flags, 2))
        flags_str[2] = 'a';

    flags_str[3] = '.';
    if(test_bit(flags, 3))
        flags_str[3] = 'f';

    flags_str[4] = '.';
    if(test_bit(flags, 4))
        flags_str[4] = 'o';

    flags_str[5] = '.';
    if(test_bit(flags, 7))
        flags_str[5] = 'n';

    flags_str[6] = '\0';

    return flags_str;
}

int main(int argc, char *argv[]) {
    int f;
    struct stat statbuf;
    char* iso;

    if (argc != 4)
        err_quit("usage: exo <isofile> <action>\n");

    if((f = open(argv[1], O_RDONLY)) < 0)
        err_quit("can't open %s for reading\n", argv[1]);

    if (fstat(f, &statbuf) < 0)
        err_quit("fstat error\n");

    if ((iso = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, f, 0)) == (__caddr_t) -1)
        err_quit("mmap error\n");

    close(f);

    pvd pvdbuff;
    if (load_pvd(iso, &pvdbuff) < 0)
        err_quit("load_pvd error\n");
    
    directory* root = build_directory_tree(iso, &pvdbuff);

    if (strcmp(argv[2], "list") == 0) {
        directory* target = search_tree_by_id(root, argv[3]);
        if (target == NULL) {
            printf("specified folder doesn't exist\n");
            goto defer;
        }
        
        if (!is_folder(target->data.flags)) {
            printf("specified folder is not a folder\n");
            goto defer;
        }

        if (target->extent == NULL) {
            printf("specified folder is empty\n");
            goto defer;
        }
        
        char* flag_str;
        target = target->extent;
        while (target != NULL) {
            flag_str = flags_to_str(target->data.flags);
            printf(
                "%-6s - %-10s %s\n", 
                flag_str,
                target->data.datetime,
                target->data.id
            );
            target = target->next;
        }
        free(flag_str);
    } else if (strcmp(argv[2], "cat") == 0) {
        directory* target = search_tree_by_id(root, argv[3]);
        if (target == NULL) {
            printf("specified file doesn't exist\n");
            goto defer;
        }
        
        if (is_folder(target->data.flags)) {
            printf("specified file is a folder\n");
            goto defer;
        }

        fputs(iso + (target->data.extent_location * pvdbuff.logical_block_size), stdout);
    }else {
        printf("usage: exo <isofile> <action>\n");
    }

defer:
    free_directory_tree(root);
    munmap(iso, statbuf.st_size);

    return exit_code;
}

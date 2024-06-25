
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "commands.h"
#include "fat16.h"
#include "support.h"

off_t fsize(const char *filename){
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}

struct fat_dir find(struct fat_dir *dirs, char *filename, struct fat_bpb *bpb){
    struct fat_dir curdir;
    int dirs_len = sizeof(struct fat_dir) * bpb->possible_rentries;
    int i;

    for (i=0; i < dirs_len; i++){
        if (strcmp((char *) dirs[i].name, filename) == 0){
            curdir = dirs[i];
        break;
        }
    }
    return curdir;
}

struct fat_dir *ls(FILE *fp, struct fat_bpb *bpb){
    int i;
    struct fat_dir *dirs = malloc(sizeof (struct fat_dir) * bpb->possible_rentries);

    for (i=0; i < bpb->possible_rentries; i++){
        uint32_t offset = bpb_froot_addr(bpb) + i * 32;
        read_bytes(fp, offset, &dirs[i], sizeof(dirs[i]));
    }
    return dirs;
}

int write_dir(FILE *fp, char *fname, struct fat_dir *dir){
    char* name = padding(fname);
    strcpy((char *) dir->name, (char *) name);
    if (fwrite(dir, 1, sizeof(struct fat_dir), fp) <= 0)
        return -1;
    return 0;
}

int write_data(FILE *fp, char *fname, struct fat_dir *dir, struct fat_bpb *bpb){

    FILE *localf = fopen(fname, "r");
    int c;

    while ((c = fgetc(localf)) != EOF){
        if (fputc(c, fp) != c)
            return -1;
    }
    return 0;
}

int wipe(FILE *fp, struct fat_dir *dir, struct fat_bpb *bpb){
    int start_offset = bpb_froot_addr(bpb) + (bpb->bytes_p_sect * \
            dir->starting_cluster);
    int limit_offset = start_offset + dir->file_size;

    while (start_offset <= limit_offset){
        fseek(fp, ++start_offset, SEEK_SET);
        if(fputc(0x0, fp) != 0x0)
            return 01;
    }
    return 0;
}

void mv(FILE *fp, char *filename, struct fat_bpb *bpb){
    ;; /* TODO */
}

void rm(FILE *fp, char *filename, struct fat_bpb *bpb) {
    struct fat_dir *dirs = ls(fp, bpb);
    struct fat_dir cur = find(dirs, filename, bpb);
    
    if (cur.name[0] == '\0') {
        fprintf(stderr, "File not found.\n");
        return;
    }

    // Marcar a entrada do diretório como livre
    cur.name[0] = DIR_FREE_ENTRY;
    uint32_t dir_offset = bpb_froot_addr(bpb) + (&cur - dirs) * sizeof(struct fat_dir);
    fseek(fp, dir_offset, SEEK_SET);
    fwrite(&cur, sizeof(struct fat_dir), 1, fp);

    // Liberar os clusters na tabela FAT
    uint16_t cluster = cur.starting_cluster;
    while (cluster < 0xFFF8) {  // 0xFFF8 é o valor para o último cluster na cadeia
        uint16_t next_cluster;
        uint32_t fat_offset = bpb_faddress(bpb) + cluster * 2;
        fseek(fp, fat_offset, SEEK_SET);
        fread(&next_cluster, sizeof(uint16_t), 1, fp);
        
        fseek(fp, fat_offset, SEEK_SET);
        uint16_t free_cluster = 0x0000;
        fwrite(&free_cluster, sizeof(uint16_t), 1, fp);
        
        if (next_cluster >= 0xFFF8) break;
        cluster = next_cluster;
    }
}

void cp(FILE *fp, char *filename, char *new_pos, struct fat_bpb *bpb) {
    struct fat_dir *dirs = ls(fp, bpb);
    struct fat_dir cur = find(dirs, filename, bpb);
    if (cur.name[0] == '\0') {
        fprintf(stderr, "File not found.\n");
        return;
    }

    FILE *new_file = fopen(new_pos, "wb");
    if (!new_file) {
        fprintf(stderr, "Failed to open destination file.\n");
        return;
    }

    int cluster_size = bpb->sector_p_clust * bpb->bytes_p_sect;
    int start_offset = bpb_fdata_addr(bpb) + (cur.starting_cluster - 2) * cluster_size;

    fseek(fp, start_offset, SEEK_SET);
    char *buffer = malloc(cluster_size);
    if (!buffer) {
        fclose(new_file);
        fprintf(stderr, "Failed to allocate memory.\n");
        return;
    }

    for (int i = 0; i < (cur.file_size + cluster_size - 1) / cluster_size; i++) {
        fread(buffer, 1, cluster_size, fp);
        fwrite(buffer, 1, (i < (cur.file_size / cluster_size)) ? cluster_size : (cur.file_size % cluster_size), new_file);
        // Avança para o próximo cluster aqui, se necessário, usando a tabela FAT
    }

    free(buffer);
    fclose(new_file);
}

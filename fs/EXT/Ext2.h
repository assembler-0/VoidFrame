#pragma once
#include "stdint.h"

// Minimal EXT2 data structures

// ext2_super_block structure
// Located at offset 1024 from the start of the volume
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic; // Should be 0xEF53
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    // Extended fields
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    // ... other fields omitted for simplicity
} __attribute__((packed)) Ext2Superblock;

// Block Group Descriptor
typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) Ext2GroupDesc;

// Inode structure
#define EXT2_N_BLOCKS 15
typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed)) Ext2Inode;

// Inode types (in i_mode)
#define EXT2_S_IFDIR  0x4000 // Directory
#define EXT2_S_IFREG  0x8000 // Regular file

#define S_ISDIR(m)  (((m) & 0xF000) == EXT2_S_IFDIR)
#define S_ISREG(m)  (((m) & 0xF000) == EXT2_S_IFREG)

// Directory entry
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed)) Ext2DirEntry;

// Function prototypes for VFS integration
int Ext2Init(uint8_t drive);
int Ext2ReadFile(const char* path, void* buffer, uint32_t max_size);
int Ext2WriteFile(const char* path, const void* buffer, uint32_t size);
int Ext2ListDir(const char* path);
int Ext2CreateFile(const char* path);
int Ext2CreateDir(const char* path);
int Ext2Delete(const char* path);
int Ext2IsFile(const char* path);
int Ext2IsDir(const char* path);
uint64_t Ext2GetFileSize(const char* path);

// Internal helpers
int Ext2ReadInode(uint32_t inode_num, Ext2Inode* inode);
uint32_t Ext2PathToInode(const char* path);

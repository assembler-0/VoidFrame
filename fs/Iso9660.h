#pragma once

#include "stdint.h"

// ISO9660 Primary Volume Descriptor
typedef struct __attribute__((packed)) {
    uint8_t type;
    char id[5];
    uint8_t version;
    uint8_t unused1;
    char system_id[32];
    char volume_id[32];
    uint8_t unused2[8];
    uint32_t volume_space_size_le;
    uint32_t volume_space_size_be;
    uint8_t unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint16_t logical_block_size_le;
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t path_table_loc_le;
    uint32_t optional_path_table_loc_le;
    uint32_t path_table_loc_be;
    uint32_t optional_path_table_loc_be;
    uint8_t root_directory_record[34];
    char volume_set_id[128];
    char publisher_id[128];
    char data_preparer_id[128];
    char application_id[128];
    char copyright_file_id[37];
    char abstract_file_id[37];
    char bibliographic_file_id[37];
    char creation_date[17];
    char modification_date[17];
    char expiration_date[17];
    char effective_date[17];
    uint8_t file_structure_version;
    uint8_t unused4;
    uint8_t application_data[512];
    uint8_t unused5[653];
} Iso9660Pvd;

// ISO9660 Directory Entry
typedef struct __attribute__((packed)) {
    uint8_t length;
    uint8_t extended_attribute_length;
    uint32_t extent_loc_le;
    uint32_t extent_loc_be;
    uint32_t data_length_le;
    uint32_t data_length_be;
    uint8_t recording_date[7];
    uint8_t file_flags;
    uint8_t file_unit_size;
    uint8_t interleave_gap_size;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint8_t file_id_length;
    char file_id[];
} Iso9660DirEntry;

// ISO9660 Path Table Record
typedef struct __attribute__((packed)) {
    uint8_t dir_id_len;
    uint8_t ext_attr_rec_len;
    uint32_t extent_loc;
    uint16_t parent_dir_num;
    char dir_id[];
} Iso9660PathTableRecord;

int Iso9660Read(const char* path, void* buffer, uint32_t max_size);
int Iso9660Copy(const char* iso_path, const char* vfs_path);
int Iso9660CopyFile(const char* iso_path, const char* vfs_path);
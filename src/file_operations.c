#include "filesys.h"
#include "debug.h"
#include "utility.h"
#include <string.h>

#define DIRECTORY_ENTRY_SIZE (sizeof(inode_index_t) + MAX_FILE_NAME_LEN)
#define DIRECTORY_ENTRIES_PER_DATABLOCK (DATA_BLOCK_SIZE / DIRECTORY_ENTRY_SIZE)


// ----------------------- CORE FUNCTION ----------------------- //

char* write_index_and_name(inode_index_t new_inode_index, char* dest)
{
    char *new_entry = calloc(16, 1);
    if (!new_entry) return NULL;

    new_entry[0] = new_inode_index & 0xFF;
    new_entry[1] = (new_inode_index >> 8) & 0xFF;

    strncpy(new_entry + 2, dest, 13);
    new_entry[15] = '\0';
    info(1, "New entry: %s\n", new_entry + 2);
    return new_entry;
}

inode_index_t get_index(byte *contents, int j)
{
    inode_index_t ans = 0;
    ans |= contents[j*16];
    ans |= contents[j*16+1] << 8;
    return ans;
}

void debug_contents(byte *contents, size_t file_size)
{
    info(1, "File size: %zu bytes\n", file_size);
    info(1, "Raw contents (first 32 bytes):\n");
    for(unsigned int i = 0; i < (file_size < 32 ? file_size : 32); i++) {
        info(1, "%c ", contents[i]);
        if((i + 1) % 16 == 0) info(1, "\n");
    }
    info(1, "\n");
    
    int n = file_size / 16;
    info(1, "Number of entries: %d\n", n);
    
    // Debug each entry as we process it
    for(int j = 0; j < n; j++) {
        info(1, "Entry %d:\n", j);
        info(1, "  Index bytes: %02x %02x\n", contents[j*16], contents[j*16+1]);
        info(1, "  Name bytes: ");
        for(int k = 2; k < 16; k++) {
            info(1, "%c ", contents[j*16 + k]);
        }
        info(1, "\n");
    }
}


char **split_string(char *path) {

    // Allocate array of n string pointers

    int n = 1;
    //counts the number of steps in the path
    for(int i = 0; path[i] != 0; i++)
    {
        if(path[i] == '/')
        n++;
    }

    char **tokens = malloc(n * sizeof(char *));
    char *token = malloc(16);

    token = strtok (path,"/");
    int i = 0;
    while (token != NULL)
    {
        tokens[i++] = token;
        token = strtok (NULL, "/");
    }
    return tokens;
}

int get_path_length(char * path)
{
    if(!path) return 0;
    int len_path = 1;
    for(int i = 0; path[i] != 0; i++)
    {
        if(path[i] == '/')
            len_path++;
    }
    return len_path;
}
char *get_path_before_dest(char *path)
{
    char *dupe = strdup(path);
    char *dest = calloc(strlen(path), 1);
    int len_path = 1;
    for(int i = 0; path[i] != 0; i++)
    {
        if(path[i] == '/')
            len_path++;
    }
    char **file_path = split_string(dupe);
    for(int i = 0; i < len_path-1; i++)
    {
        info(1, "%s ", file_path[i]);
    }
    info(1, "\n");
    for(int i = 0; i < len_path-1; i++)
    {
        strcat(dest, file_path[i]);
        strcat(dest, "/");
    }
    dest[strlen(dest)-1] = '\0'; // Remove the trailing slash
    return dest;
}

inode_index_t find_index_of(terminal_context_t *context, inode_t *inode, char* path_before_dest, int len_path)
{
    if(!inode || !path_before_dest) return 0;
    
    char *dupe = strdup(path_before_dest);
    char **file_path = split_string(dupe);
    inode_index_t ret = 0;
    inode_t *curr_dir = context->working_directory;
    
    // Traverse through the path
    for(int i = 0; i < len_path; i++)
    {            
        byte *contents = malloc(curr_dir->internal.file_size);
        if (!contents) {
            free(dupe);
            return -1;
        }
        
        size_t file_size = curr_dir->internal.file_size;
        inode_read_data(context->fs, curr_dir, 0, contents, file_size, &file_size);
        
        int n = file_size / 16;
        char **content_names = malloc(n * sizeof(char *));
        inode_index_t *indices = malloc(n * sizeof(inode_index_t));
        
        // Read directory entries
        for(int j = 0; j < n; j++)
        {
            char* name = calloc(14, 1);
            indices[j] = get_index(contents, j);
            strncpy(name, (char*)&contents[16*j + 2], 13);
            name[13] = '\0';
            content_names[j] = name;
            
            info(1, "Entry %d: %s (index=%d)\n", j, name, indices[j]);
        }
        
        // Find matching entry
        int found = 0;
        for(int j = 0; j < n; j++)
        {
            if(strcmp(file_path[i], content_names[j]) == 0) 
            {
                found = 1;
                ret = indices[j];
                curr_dir = &context->fs->inodes[indices[j]]; // Update current directory
                info(1, "Found '%s' at index %d\n", file_path[i], indices[j]);
                break;
            }
        }
        
        if(i == len_path - 1 && found) {
            info(1, "Final index: %d\n", ret);
            return ret;
        }
        // Clean up
        for(int j = 0; j < n; j++) {
            free(content_names[j]);
        }
        free(content_names);
        free(indices);
        free(contents);
        
        if(!found && i) {
            free(dupe);
            return -1;
        }
    }
    
    free(dupe);
    return ret;
}

fs_file_t fs_open_dir(terminal_context_t *context, char *path)
{
    if(!context || !path) return NULL;
    char *dupe = strdup(path); 
    int len_path = 1;
    int len = strlen(path);
    //counts the number of steps in the path
    for(int i = 0; i<len && path[i] != 0; i++)
    {
        if(path[i] == '/')
        len_path++;
    }
    char **file_path = split_string(dupe);
    inode_t *curr_dir = context->working_directory;
    for(int i = 0; i < len_path; i++)
    {
        if(curr_dir->internal.file_type != DIRECTORY) 
        {
            printf("Error: Directory not found\n");
            return NULL;
        }
            

        byte *contents = malloc(curr_dir->internal.file_size);
        size_t file_size = curr_dir->internal.file_size;
        inode_read_data(context->fs, curr_dir, 0, contents, curr_dir->internal.file_size, &file_size);


        int n = file_size / 16;
        char **content_names = malloc(n * sizeof(char *));
        inode_index_t *indices = malloc(n * sizeof(inode_index_t));
        for(int j = 0; j < n; j++)
        {
            char* name = calloc(14, 1);
            indices[j] = get_index(contents, j);
            strncpy(name, (char*)&contents[16*j + 2], 13);
            name[13] = '\0'; // Ensure null termination
            content_names[j] = name;
            
            info(1, "%s ", content_names[j]);
            info(1, "\n");
        }
        int found = 0;
        for(int j = 0; j < n; j++)
        {
            info(1, "Comparing '%s' (len=%zu) with '%s' (len=%zu)\n", 
            file_path[i], strlen(file_path[i]), 
            content_names[j], strlen(content_names[j]));
         
            // Use strncmp to compare only the actual string contents
            if(strcmp(file_path[i], content_names[j]) == 0) 
            {
                found = 1;
                curr_dir = &context->fs->inodes[indices[j]];
                info(1, "found %s\n", file_path[i]);
                info(1, "found %d\n", indices[j]);
                break;
            }
        }
        if(!found && i)
        {
            // printf("Error: Directory not found\n");
            return NULL;
        }

        // garbage collection
        for(int j = 0; j < n; j++)
        {
            free(content_names[j]);
        }
        free(content_names);
        free(indices);
    }
    info(1, "%s\n", curr_dir->internal.file_name);
    //confirm path exists, leads to a file
    //allocate space for the file, assign its fs and inode. Set offset to 0.
    //return file
    fs_file_t file = malloc(sizeof(fs_file_t));
    if(!file) return NULL;
    file->fs = context->fs;
    file->inode = curr_dir;
    file->offset = 0;
    return file;
}

int fs_contains(filesystem_t *fs, fs_file_t dir, char *name)
{
    if(!fs || !dir || !name) return 0;
    byte *contents = malloc(dir->inode->internal.file_size);
    size_t file_size = dir->inode->internal.file_size;
    inode_read_data(fs, dir->inode, 0, contents, dir->inode->internal.file_size, &file_size);
    // debug_contents(contents, file_size);
    //i need to put 16 bytes into a string
    int n = file_size / 16;
    char **content_names = malloc(n * sizeof(char *));
    inode_index_t *indices = malloc(n * sizeof(inode_index_t));
    for(int j = 0; j < n; j++)
    {
        char* name = calloc(14, 1);
        indices[j] = get_index(contents, j);
        strncpy(name, (char*)&contents[16*j + 2], 13);
        name[13] = '\0'; // Ensure null termination
        content_names[j] = name;
        
        info(1, "%s ", content_names[j]);
        info(1, "\n");
    }
    int found = 0;
    for(int j = 0; j < n; j++)
    {
        info(1, "Comparing '%s' (len=%zu) with '%s' (len=%zu)\n", 
        name, strlen(name), 
        content_names[j], strlen(content_names[j]));
     
        // Use strncmp to compare only the actual string contents
        if(strcmp(name, content_names[j]) == 0) 
        {
            found = 1;
            break;
        }
    }
    
    // garbage collection
    for(int j = 0; j < n; j++)
    {
        free(content_names[j]);
    }
    free(content_names);
    free(indices);

    return found;
}
/*
Creates a new file at path relative to the working directory in context with permissions perms.
The basename of path is the name of the file being created.
If the basename exceed the max file name size, truncate the name to fit.
The size of the file created should be 0.
The permissions of the file should be set to perms
The type of the inode should be DATA_FILE
Creating this file should also update its parent directory's directory entries.
The entry should replace the earliest tombstone in the directory entries.
If there is no tombstones, then the entry should be appended to the end of the directory entries.
If any error (described below) occur, no file should be created and the file system should not be modified, i.e. the state of the file system before the function call should be the same as the state of the file system after the function call.
Return -1 in case of any error. Return 0 on success.
If context or path is NULL, just return 0.
The return codes to be reported (errors) in the order of precedence:
If the dirname of path contains a name which does not exist or whose inode is not a directory inode, report DIR_NOT_FOUND. Return -1.
If the basename of path has a corresponding inode (it may be a directory inode), report FILE_EXIST. Return -1.
If there is not enough D-blocks in the file system to satisfy the request, report INSUFFICIENT_DBLOCKS. Return -1.
If we cannot allocate an inode from the file system, report INODE_UNAVAILABLE. Return -1;*/
int new_file(terminal_context_t *context, char *path, permission_t perms)
{
    if(!context || !path) return 0;
    if(available_inodes(context->fs) == 0)
    {
        printf("Error: No inodes available\n");
        return -1;
    }
    if(available_dblocks(context->fs) == 0)
    {
        printf("Error: Not enough dblocks for operation\n");
        return -1;
    }
    char *path_before_dest = get_path_before_dest(path);
    int path_len = get_path_length(path);
    char *dest = split_string(path)[path_len-1];

    fs_file_t curr_dir = fs_open_dir(context, path_before_dest);
    if(!curr_dir)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    if(fs_contains(context->fs, curr_dir, dest))
    {
        printf("Error: File already exists\n");
        return -1;
    }

    //need to write the file name into the directory
    inode_index_t *new_inode_index = malloc(sizeof(inode_index_t));
    if(claim_available_inode(context->fs, new_inode_index) == SUCCESS)
    {
        info(1, "Claimed inode index: %d (0x%02x)\n", *new_inode_index, *new_inode_index);
    }
    else
    {
        return -1;
    }
    inode_t *new_inode = &context->fs->inodes[*new_inode_index];
    new_inode->internal.file_type = DATA_FILE;
    new_inode->internal.file_size = 0;
    strncpy(new_inode->internal.file_name, dest, 14);
    for(int i = 0; i < 4; i++)
    {
        new_inode->internal.direct_data[i] = 0;
    }
    new_inode->internal.indirect_dblock = 0;
    new_inode->internal.file_perms = perms;
    char *contents = write_index_and_name(*new_inode_index, dest);
    debug_contents((byte*)contents, DIRECTORY_ENTRY_SIZE);
    
    // Get current directory contents to find tombstone or append position
    byte *curr_contents = malloc(curr_dir->inode->internal.file_size);
    size_t curr_size = curr_dir->inode->internal.file_size;
    inode_read_data(context->fs, curr_dir->inode, 0, curr_contents, curr_size, &curr_size);
    size_t write_offset = curr_size;  // Default to appending
    for(size_t i = 0; i < curr_size; i += 16) {
        // Check all 16 bytes for zeros
        int is_tombstone = 1;
        for(int j = 0; j < 16; j++) {
            if(curr_contents[i + j] != 0) {
                is_tombstone = 0;
                break;
            }
        }
        
        if(is_tombstone) {
            info(1, "Found tombstone at offset %zu\n", i);
            write_offset = i;
            break;
        }
    }
    info(1, "Final write offset: %zu\n", write_offset);
    inode_modify_data(context->fs, curr_dir->inode, write_offset, contents, DIRECTORY_ENTRY_SIZE);
    // Clean up
    info(1, "Inspecting dblock index 1:\n");
    for(int i = 0; i < 64; i++) {
        info(1, "Byte %d: 0x%02x ('%c')\n", 
             i, 
             context->fs->dblocks[64 + i],  // dblock 1 starts at offset 64
             context->fs->dblocks[64 + i]);
    }
    free(contents);
    free(curr_contents);
    return 0;
}

int new_directory(terminal_context_t *context, char *path)
{
    if(!context || !path) return 0;
    if(available_inodes(context->fs) <= 1)
    {
        printf("Error: No inodes available\n");
        return -1;
    }
    if(available_dblocks(context->fs) <= 1)
    {
        printf("Error: Not enough dblocks for operation\n");
        return -1;
    }
    char *path_before_dest = get_path_before_dest(path);
    int path_len = get_path_length(path);
    char *dest = split_string(path)[path_len-1];

    fs_file_t curr_dir = fs_open_dir(context, path_before_dest);
    if(!curr_dir)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    if(fs_contains(context->fs, curr_dir, dest))
    {
        printf("Error: Directory already exists\n");
        return -1;
    }

    //need to write the file name into the directory
    inode_index_t *new_inode_index = malloc(sizeof(inode_index_t));
    if(claim_available_inode(context->fs, new_inode_index) == SUCCESS)
    {
        info(1, "Claimed inode index: %d (0x%02x)\n", *new_inode_index, *new_inode_index);
    }
    else
    {
        return -1;
    }
    inode_t *new_inode = &context->fs->inodes[*new_inode_index];
    new_inode->internal.file_type = DIRECTORY;
    new_inode->internal.file_size = 0; 
    size_t dest_len = strlen(dest);
    strncpy(new_inode->internal.file_name, dest, dest_len);
    if (dest_len < MAX_FILE_NAME_LEN) {
        new_inode->internal.file_name[dest_len] = '\0';
    }
    new_inode->internal.indirect_dblock = 0;

    char *special_data1 = calloc(DIRECTORY_ENTRY_SIZE, 1);
    char *special_data2 = calloc(DIRECTORY_ENTRY_SIZE, 1);
    special_data1 = write_index_and_name(*new_inode_index, ".");
    inode_index_t parent_dir_index = find_index_of(context, curr_dir->inode, path_before_dest, path_len-1);
    special_data2 = write_index_and_name(parent_dir_index, "..");

    inode_write_data(context->fs, new_inode, special_data1 , 16);

    inode_write_data(context->fs, new_inode, special_data2, 16);

    new_inode->internal.file_size = 32;

    char *contents = write_index_and_name(*new_inode_index, dest);
    debug_contents((byte*)contents, DIRECTORY_ENTRY_SIZE);
    
    // Get current directory contents to find tombstone or append position
    byte *curr_contents = calloc(curr_dir->inode->internal.file_size, 1);
    size_t curr_size = curr_dir->inode->internal.file_size;
    inode_read_data(context->fs, curr_dir->inode, 0, curr_contents, curr_size, &curr_size);
    size_t write_offset = curr_size;  // Default to appending
    for(size_t i = 0; i < curr_size; i += 16) {
        // Check all 16 bytes for zeros
        int is_tombstone = 1;
        for(int j = 0; j < 16; j++) {
            if(curr_contents[i + j] != 0) {
                is_tombstone = 0;
                break;
            }
        }
        
        if(is_tombstone) {
            info(1, "Found tombstone at offset %zu\n", i);
            write_offset = i;
            break;
        }
    }
    info(1, "Final write offset: %zu\n", write_offset);
    inode_modify_data(context->fs, curr_dir->inode, write_offset, contents, DIRECTORY_ENTRY_SIZE);

    // Clean up
    info(1, "Inspecting dblock index 1:\n");
    info(1, "file_size: %zu\n", new_inode->internal.file_size);
    info(1, "file_type: %d\n", new_inode->internal.file_type);
    info(1, "file_name: %s\n", new_inode->internal.file_name);
    info(1, "direct_data: %u\n", new_inode->internal.direct_data[0]);


    info(1, "file_size: %zu\n", context->fs->inodes[parent_dir_index].internal.file_size);
    info(1, "file_type: %d\n", context->fs->inodes[parent_dir_index].internal.file_type);
    info(1, "file_name: %s\n", context->fs->inodes[parent_dir_index].internal.file_name);
    info(1, "direct_data: %u\n", context->fs->inodes[parent_dir_index].internal.direct_data[0]);
    for(int j = 0; j < 5; j++)
    {
        for(int i = 0; i < 64; i++) {
            info(1, "Byte %d: 0x%02x ('%c')\n", 
                i, 
                context->fs->dblocks[64*j + i],  // dblock 1 starts at offset 64
                context->fs->dblocks[64*j + i]);
        }
    }
    free(contents);
    free(curr_contents);
    return 0;
}

int remove_file(terminal_context_t *context, char *path)
{
    if(!context || !path) return 0;
    char *dupe = strdup(path);
    int len_path = get_path_length(dupe);
    char **file_path = split_string(dupe);
    char *dest = file_path[len_path-1];

    char *path_before_dest = get_path_before_dest(path);
    fs_file_t parent_dir;
    if(strcmp(path_before_dest, "") == 0)    
    {
        parent_dir = malloc(sizeof(struct fs_file));
        if (!parent_dir) return -1;
        parent_dir->fs = context->fs;
        parent_dir->inode = context->working_directory;
        parent_dir->offset = 0;
    }
    else
    {
        parent_dir = fs_open_dir(context, path_before_dest);
    }

    if(!parent_dir)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    if(!fs_contains(context->fs, parent_dir, dest))
    {
        printf("Error: File not found\n");
        return -1;
    }

    fs_file_t file = fs_open_dir(context, path);
    if(!file || file->inode->internal.file_type != DATA_FILE)
    {
        printf("Error: File not found\n");
        return -1;
    }
    inode_t *inode = file->inode;
    inode_index_t removed_index = find_index_of(context, inode, path, len_path);
    // Get current directory contents to find tombstone or append position
    byte *curr_contents = malloc(parent_dir->inode->internal.file_size);
    inode_read_data(context->fs, parent_dir->inode, 0, curr_contents, parent_dir->inode->internal.file_size, &parent_dir->inode->internal.file_size);
    for(size_t i = 0; i < parent_dir->inode->internal.file_size; i++)
    {
        info(1, "Byte %zu: 0x%02x ('%c')\n", 
            i, 
            curr_contents[i],
            curr_contents[i]);
    }
    // for(size_t i = 0; i < parent_dir->inode->internal.file_size; i += 16) 
    // {
    //     inode_index_t index = get_index(curr_contents, i/16);
    //     if(index == removed_index) 
    //     {
    //         // Mark the entry as a tombstone
    //         byte entry[16] = {0};
    //         inode_modify_data(context->fs, parent_dir->inode, i, entry, 16);
    //         info(1, "Marked entry at offset %zu as tombstone\n", i);
    //         break;
    //     }
    // }
    byte *contents = malloc(parent_dir->inode->internal.file_size);
    inode_read_data(context->fs, parent_dir->inode, 0, contents, parent_dir->inode->internal.file_size, &parent_dir->inode->internal.file_size);
    for(size_t i = 0; i < parent_dir->inode->internal.file_size; i += 16) 
    {
        inode_index_t index = get_index(curr_contents, i/16);
        if(index == removed_index) 
        {
            // Mark the entry as a tombstone
            byte entry[16] = {0};
            inode_modify_data(context->fs, parent_dir->inode, i, entry, 16);
            
            int trailing_tombstone = 1;
            for(size_t j = i + 16; j < parent_dir->inode->internal.file_size; j++) 
            {
                if(contents[j] != 0) {
                    trailing_tombstone = 0;
                } 
            }
            if(trailing_tombstone) {
                parent_dir->inode->internal.file_size -= 16;
            }
            info(1, "Marked entry at offset %zu as tombstone\n", i);
            break;
        }
    }

    inode_release_data(context->fs, inode);
    release_inode(context->fs, inode);
    free(file);
    if (parent_dir) {
        free(parent_dir);
    }

    return 0;
}


int is_empty(filesystem_t *fs ,inode_t *inode)
{
    if(!inode) return 0;
    if(inode->internal.file_size == 0) return 1;
    byte *contents = calloc(inode->internal.file_size, 1);
    size_t file_size = inode->internal.file_size;
    inode_read_data(fs, inode, 32, contents, inode->internal.file_size, &file_size);
    for(size_t i = 0; i < inode->internal.file_size; i++)
    {
        if(contents[i] != 0) return 1;
    }
    return 0;
}



#define DBLOCK_MASK_SIZE(blk_count) (((blk_count) + 7) / (sizeof(byte) * 8))


// Print the data block bitmap
void debug_dblock_bitmap(filesystem_t *fs) {
    info(1, "Data Block Bitmap:\n");
    for (size_t i = 0; i < DBLOCK_MASK_SIZE(fs->dblock_count); i++) {
        info(1, "Byte %zu: 0x%02x (binary: ", i, fs->dblocks[i]);
        for (int j = 7; j >= 0; j--) {
            info(1, "%d", (fs->dblocks[i] >> j) & 1);
        }
        info(1, ")\n");
    }
}

// Track data block allocation and release
void debug_dblock_operation(filesystem_t *fs, byte dblock_index, const char* operation) {
    size_t byte_index = dblock_index / 8;
    size_t bit_index = dblock_index % 8;
    info(1, "%s dblock %u (byte %zu, bit %zu)\n", 
         operation, dblock_index, byte_index, bit_index);
    info(1, "Before operation: 0x%02x\n", fs->dblocks[byte_index]);
}

// Debug inode data blocks
void debug_inode_blocks(filesystem_t *fs, inode_t *inode) {
    info(1, "Inode direct data blocks:\n");
    for (int i = 0; i < 4; i++) {
        if (inode->internal.direct_data[i] != 0) {
            info(1, "  Block %d: %u\n", i, inode->internal.direct_data[i]);
        }
    }
    if (inode->internal.indirect_dblock != 0) {
        info(1, "Indirect block: %u\n", inode->internal.indirect_dblock);
    }
}

// we can only delete a directory if it is empty!!
void debug_parent_dir(filesystem_t *fs, inode_t *parent_inode) {
    info(1, "\n=== Parent Directory Debug Info ===\n");
    info(1, "File size: %zu\n", parent_inode->internal.file_size);
    info(1, "File type: %d\n", parent_inode->internal.file_type);
    info(1, "File name: %s\n", parent_inode->internal.file_name);
    
    byte *contents = malloc(parent_inode->internal.file_size);
    size_t size = parent_inode->internal.file_size;
    inode_read_data(fs, parent_inode, 0, contents, size, &size);
    
    info(1, "\nDirectory Entries:\n");
    for(size_t i = 0; i < size; i += 16) {
        inode_index_t index = get_index(contents, i/16);
        char name[14] = {0};
        strncpy(name, (char*)&contents[i + 2], 13);
        
        info(1, "Entry %zu:\n", i/16);
        info(1, "  Index: %u\n", index);
        info(1, "  Name: %s\n", name);
        info(1, "  Raw bytes: ");
        for(int j = 0; j < 16; j++) {
            info(1, "%02x ", contents[i + j]);
        }
        info(1, "\n");
    }
    
    free(contents);
    info(1, "================================\n\n");
}

int remove_directory(terminal_context_t *context, char *path)
{
    if(!context || !path) return 0;

    debug_dblock_bitmap(context->fs);


    char *dupe = strdup(path);
    int len_path = get_path_length(dupe);
    char **file_path = split_string(dupe);
    char *dest = file_path[len_path-1];
    if(strcmp(dest, context->working_directory->internal.file_name) == 0)
    {
        printf("Error: Cannot delete current working directory\n");
        return -1;
    }
    char *path_before_dest = get_path_before_dest(path);
    fs_file_t parent_dir;
    if(strcmp(path_before_dest, "") == 0)    
    {
        parent_dir = malloc(sizeof(struct fs_file));
        if (!parent_dir) return -1;
        parent_dir->fs = context->fs;
        parent_dir->inode = context->working_directory;
        parent_dir->offset = 0;
    }
    else
    {
        parent_dir = fs_open_dir(context, path_before_dest);
    }

    if(!parent_dir)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    if(!fs_contains(context->fs, parent_dir, dest))
    {
        printf("Error: Directory not found\n");
        return -1;
    }

    fs_file_t file = fs_open_dir(context, path);
    if(!file || file->inode->internal.file_type != DIRECTORY)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    
    if(strcmp(dest,".") == 0 || strcmp(dest,"..") == 0)
    {
        printf("Error: Invalid file name\n");
        return -1;
    }
    if(is_empty(context->fs, file->inode) != 0)
    {
        printf("Error: Directory is not empty\n");
        return -1;
    }





    
    inode_t *inode = file->inode;
    inode_index_t removed_index = find_index_of(context, inode, path, len_path);


    debug_dblock_bitmap(context->fs);
    debug_inode_blocks(context->fs, inode);



    debug_dblock_operation(context->fs, inode->internal.direct_data[0], "Releasing");

    inode_release_data(context->fs, inode);
    inode->internal.file_size = 0;

    debug_dblock_bitmap(context->fs);


    release_inode(context->fs, inode);


    debug_dblock_bitmap(context->fs);


    byte *contents = malloc(parent_dir->inode->internal.file_size);
    size_t n = 0;
    inode_read_data(context->fs, parent_dir->inode, 0, contents, parent_dir->inode->internal.file_size, &n);
    for(size_t i = 0; i < parent_dir->inode->internal.file_size; i += 16)
    {
        inode_index_t index = get_index(contents, i/16);
        if(index == removed_index) 
        {
            debug_parent_dir(context->fs, parent_dir->inode);

            byte entry[16] = {0};
            inode_modify_data(context->fs, parent_dir->inode, i, entry, 16);
            
            debug_parent_dir(context->fs, parent_dir->inode);

            size_t last_non_tombstone = 0;
            inode_read_data(context->fs, parent_dir->inode, 0, contents, parent_dir->inode->internal.file_size, &n);
            for(size_t j = 0; j < parent_dir->inode->internal.file_size; j+=16) 
            {
                int tombstone = 1;
                for(int k = 0; k < 16; k++) {
                    if(contents[j + k] != 0) {
                        tombstone = 0;
                        break;
                    }
                }

                if(tombstone == 0) {
                    last_non_tombstone = j;
                }
            }
            if(last_non_tombstone+16 <= parent_dir->inode->internal.file_size) {
                parent_dir->inode->internal.file_size = last_non_tombstone+16;
            } else {
                inode_shrink_data(context->fs, parent_dir->inode, last_non_tombstone+16);
            }
        }
    }

    
    
    free(file);
    if (parent_dir) {
        free(parent_dir);
    }

    return 0;
}

int change_directory(terminal_context_t *context, char *path)
{
    if(!context || !path) return 0;
    char *dupe = strdup(path);
    int len_path = get_path_length(dupe);
    char **file_path = split_string(dupe);
    char *dest = file_path[len_path-1];
    if(strcmp(dest, context->working_directory->internal.file_name) == 0)
    {
        printf("Error: Cannot change to current working directory\n");
        return -1;
    }
    fs_file_t new_wdir = fs_open_dir(context, path);
    if(new_wdir == NULL)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    if(new_wdir->inode->internal.file_type != DIRECTORY)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    // if(strcmp(dest,".") == 0 || strcmp(dest,"..") == 0)
    // {
    //     printf("Error: Invalid file name\n");
    //     return -1;
    // }
    context->working_directory = new_wdir->inode;
    return 0;
}


// Displays the content of a file or a directory located at path relative to the working directory in context.
// If the basename of the path is a file, display the permissions, file size, and filename of the inode of the file.
// If the basename of the path is a directory, display each the directory entries on separate lines treating each child item (both files and directories) of the directory the same manner as the previous bullet point.
// Even for directories, just display the value stored in the inode file size field as the file size of the directory.
// The formatting of displaying a file is:
// The first character of the line should be d if the inode is a directory or f if it is a file. You can use E for any other file types (we have no tests for this, so it is optional)
// The second character of the line should be r if there is read permissions, - otherwise.
// The third character of the line should be w if there is write permissions, - otherwise.
// The fourth character of the line should be x if there is execute permissions, - otherwise.
// The next character should be the tab character \t.
// The next part should be the inode file size displayed as an unsigned long in base 10.
// The next character directly after the file size should be the tab character \t.
// The next part should be the file name as stored in the directory entry (if the basename refer to a directory) or in the inode (if basename refer to a file).
// If directory entry being displayed is a special directory entry, then display -> (One space before and after) immediately after the previous file name. After the array, display the name of the inode.
int list(terminal_context_t *context, char *path)
{
    if(!context || !path) return 0;
    char *path_before_dest = get_path_before_dest(path);
    fs_file_t parent;
    if(strcmp(path_before_dest, "") == 0)    
    {
        parent = malloc(sizeof(struct fs_file));
        if (!parent) return -1;
        parent->fs = context->fs;
        parent->inode = context->working_directory;
        parent->offset = 0;
    }
    else
        parent = fs_open_dir(context, path_before_dest);
    
    if(parent == NULL)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    fs_file_t file = fs_open_dir(context, path);

    if(file == NULL)
    {
        printf("Error: Object not found\n");
        return -1;
    }


    byte *contents = malloc(file->inode->internal.file_size);
    size_t n = 0;
    inode_read_data(context->fs, file->inode, 0, contents, file->inode->internal.file_size, &n);

    if(file->inode->internal.file_type == DATA_FILE)
    {
        printf("f");
        if(file->inode->internal.file_perms & FS_READ) printf("r");
        else printf("-");
        if(file->inode->internal.file_perms & FS_WRITE) printf("w");
        else printf("-");
        if(file->inode->internal.file_perms & FS_EXECUTE) printf("x");
        else printf("-");
        printf("\t%lu\t%s\n", file->inode->internal.file_size, file->inode->internal.file_name);
        return 0;
    }
    else {
        for(size_t i = 0; i < file->inode->internal.file_size; i += 16) {
            inode_index_t index = get_index(contents, i/16);
            char name[14] = {0};
            strncpy(name, (char*)&contents[i + 2], 13);
    
            info(1, "Processing entry at offset %zu:\n", i);
            info(1, "  Index: %u\n", index);
            info(1, "  Name: %s\n", name);
            info(1, "  File type: %d\n", context->fs->inodes[index].internal.file_type);
            info(1, "  Permissions: %d\n", context->fs->inodes[index].internal.file_perms);
    
            if(context->fs->inodes[index].internal.file_type == DIRECTORY) {
                info(1, "  Type: Directory\n");
                printf("d");
                info(1, "  Checking permissions:\n");
                info(1, "    READ: %d\n", context->fs->inodes[index].internal.file_perms & FS_READ);
                if(context->fs->inodes[index].internal.file_perms & FS_READ) printf("r");
                else printf("-");
                info(1, "    WRITE: %d\n", context->fs->inodes[index].internal.file_perms & FS_WRITE);
                if(context->fs->inodes[index].internal.file_perms & FS_WRITE) printf("w");
                else printf("-");
                info(1, "    EXECUTE: %d\n", context->fs->inodes[index].internal.file_perms & FS_EXECUTE);
                if(context->fs->inodes[index].internal.file_perms & FS_EXECUTE) printf("x");
                else printf("-");
            } else {
                info(1, "  Type: File\n");
                printf("f");
                info(1, "  Checking permissions:\n");
                info(1, "    READ: %d\n", context->fs->inodes[index].internal.file_perms & FS_READ);
                if(context->fs->inodes[index].internal.file_perms & FS_READ) printf("r");
                else printf("-");
                info(1, "    WRITE: %d\n", context->fs->inodes[index].internal.file_perms & FS_WRITE);
                if(context->fs->inodes[index].internal.file_perms & FS_WRITE) printf("w");
                else printf("-");
                info(1, "    EXECUTE: %d\n", context->fs->inodes[index].internal.file_perms & FS_EXECUTE);
                if(context->fs->inodes[index].internal.file_perms & FS_EXECUTE) printf("x");
                else printf("-");
            }
    
            info(1, "  File size: %lu\n", context->fs->inodes[index].internal.file_size);
            printf("\t%lu\t%s", context->fs->inodes[index].internal.file_size, name);
    
            if(index == 0) {
                info(1, "  Special case: root directory\n");
                printf(" -> root\n");
            } else if(strcmp(name, ".") == 0) {
                info(1, "  Special case: current directory (.)\n");
                printf(" -> %s\n", name);
            } else if (strcmp(name, "..") == 0) {
                info(1, "  Special case: parent directory (..)\n");
                info(1, "  Parent name: %s\n", file->inode->internal.file_name);
                printf(" -> %s\n", file->inode->internal.file_name);
            } else {
                info(1, "  Regular entry\n");
                printf("\n");
            }
        }
    }
    return 0;
}

char* get_length_path_str(filesystem_t *fs, inode_t *root, inode_t *dest)
{
    size_t path_len = 0;
    if(!root || !dest) return 0;

    inode_t *curr_dir = dest;
    char **reversed_path = malloc(fs->inode_count*sizeof(char*));
    int count = 0;
    reversed_path[count] = malloc(16);
    strncpy(reversed_path[count], dest->internal.file_name, 14);
    count++;
    while(curr_dir != root)
    {
        char *name = calloc(16, 1);
        path_len++;
        byte *contents = calloc(16,1);
        size_t len = 0;
        inode_read_data(fs, curr_dir, 16, contents, 16, &len); 
        inode_index_t index = get_index(contents, 0);
        char *inode_name = fs->inodes[index].internal.file_name;
        strncpy(name, inode_name, 13);
        reversed_path[count] = name;
        count++;
        curr_dir = &fs->inodes[index];
        free(contents);
    }
    
    char *ans = calloc(count * 14, 1);
    for(int i = count-1; i >= 0; i--)
    {
        ans = strcat(ans, reversed_path[i]);
        ans = strcat(ans, "/");
    }
    ans[strlen(ans)-1] = '\0'; 
    return ans;
}

char *get_path_string(terminal_context_t *context)
{
    if(!context) return calloc(0, 1);
    return get_length_path_str(context->fs, &context->fs->inodes[0], context->working_directory);
}

void tree_dfs(inode_t *work_dir, terminal_context_t *context, inode_t *root, int depth, inode_index_t *visited, char* path)
{
    if(!context || !root) return;
    for(int i = 0; i < depth; i++)
    {
        printf("   ");
        info(1, "   ");
    }
    context->working_directory = root;
    printf("%s\n", root->internal.file_name);
    info(1, "%s\n", root->internal.file_name);
    path = get_path_string(context);    
    byte *contents = malloc(root->internal.file_size);
    size_t n = 0;
    inode_read_data(context->fs, root, 0, contents, root->internal.file_size, &n);
    path = calloc(16, 1);
    for(size_t i = 0; i < root->internal.file_size; i += 16) {
        context->working_directory = root;
        inode_index_t index = get_index(contents, i/16);
        if(context->fs->inodes[index].internal.file_type == DIRECTORY && visited[index] == 0) {
            // strcat(path, "/");
            // strcat(path, context->fs->inodes[index].internal.file_name);
            context->working_directory = &context->fs->inodes[index];
            visited[index] = 1;
            tree_dfs(context->working_directory, context, &context->fs->inodes[index], depth + 1, visited, path);
        }
        else if(context->fs->inodes[index].internal.file_type == DATA_FILE && visited[index] == 0) {
            for(int i = 0; i < depth+1; i++)
            {
                printf("   ");
                info(1, "   ");
            }
            printf("%s\n", context->fs->inodes[index].internal.file_name);
            info(1, "%s\n", context->fs->inodes[index].internal.file_name);
            visited[index] = 1;

        }
    }
    context->working_directory = work_dir;
}

int tree(terminal_context_t *context, char *path)
{
    if(!context || !path) return 0;
    inode_t *working_dir = context->working_directory;
    char *dupe = strdup(path);
    int len_path = get_path_length(dupe);
    char **file_path = split_string(dupe);
    char *dest = file_path[len_path-1];
    char *path_before_dest = get_path_before_dest(path);
    fs_file_t parent_dir;
    if(strcmp(path_before_dest, "") == 0)    
    {
        parent_dir = malloc(sizeof(struct fs_file));
        if (!parent_dir) return -1;
        parent_dir->fs = context->fs;
        parent_dir->inode = context->working_directory;
        parent_dir->offset = 0;
    }
    else
    {
        parent_dir = fs_open_dir(context, path_before_dest);
    }
    if(!parent_dir)
    {
        printf("Error: Directory not found\n");
        return -1;
    }
    if(strcmp(dest, context->working_directory->internal.file_name) == 0)
    {
        printf("Error: Cannot change to current working directory\n");
        return -1;
    }
    fs_file_t curr_dir = fs_open_dir(context, path);
    if(curr_dir == NULL)
    {
        printf("Error: Object not found\n");
        return -1;
    }
    if(curr_dir->inode->internal.file_type != DIRECTORY)
    {
        printf("%s\n", curr_dir->inode->internal.file_name);
        return 0;
    }



    // run DFS on filesystem and dir

    // printf("%s\n", curr_dir->inode->internal.file_name);
    inode_index_t *visited = calloc(context->fs->inode_count*sizeof(inode_index_t), 1);
    int prev_index;
    if(strcmp(parent_dir->inode->internal.file_name, "root") == 0)
        prev_index = 0;
    else
        prev_index = find_index_of(context, parent_dir->inode, path_before_dest, get_path_length(path_before_dest));
    int curr_index = find_index_of(context, curr_dir->inode, path, len_path);
    context->working_directory = curr_dir->inode;
    visited[curr_index] = 1;
    visited[prev_index] = 1;
    tree_dfs(context->working_directory, context, curr_dir->inode, 0, visited, path);
    context->working_directory = working_dir;
    free(visited);
    return 0;
}

//Part 2
void new_terminal(filesystem_t *fs, terminal_context_t *term)
{
    if(!fs || !term) return;
    term->working_directory = &fs->inodes[0];
    term->fs = fs;
    //check if inputs are valid

    //assign file system and root inode.
}

fs_file_t fs_open(terminal_context_t *context, char *path)
{
    if(!context || !path) return NULL;
    char *dupe = strdup(path); 
    int len_path = 1;
    //counts the number of steps in the path
    for(int i = 0; path[i] != 0; i++)
    {
        if(path[i] == '/')
        len_path++;
    }
    char **file_path = split_string(dupe);
    inode_t *curr_dir = context->working_directory;
    for(int i = 0; i < len_path; i++)
    {
        if(curr_dir->internal.file_type != DIRECTORY) 
        {
            printf("Error: Directory not found\n");
            return NULL;
        }
            

        byte *contents = malloc(curr_dir->internal.file_size);
        size_t file_size = curr_dir->internal.file_size;
        inode_read_data(context->fs, curr_dir, 0, contents, curr_dir->internal.file_size, &file_size);
        debug_contents(contents, file_size);
        //i need to put 16 bytes into a string
        int n = file_size / 16;
        char **content_names = malloc(n * sizeof(char *));
        inode_index_t *indices = malloc(n * sizeof(inode_index_t));
        for(int j = 0; j < n; j++)
        {
            char* name = calloc(14, 1);
            indices[j] = get_index(contents, j);
            strncpy(name, (char*)&contents[16*j + 2], 13);
            name[13] = '\0'; // Ensure null termination
            content_names[j] = name;
            
            info(1, "%s ", content_names[j]);
            info(1, "\n");
        }
        int found = 0;
        for(int j = 0; j < n; j++)
        {
            info(1, "Comparing '%s' (len=%zu) with '%s' (len=%zu)\n", 
            file_path[i], strlen(file_path[i]), 
            content_names[j], strlen(content_names[j]));
            if(strcmp(file_path[i], content_names[j]) == 0) 
            {
                found = 1;
                curr_dir = &context->fs->inodes[indices[j]];
                info(1, "found %s\n", file_path[i]);
                info(1, "found %d\n", indices[j]);
                break;
            }
        }
        if(!found && i != len_path-1)
        {
            printf("Error: Directory not found\n");
            return NULL;
        }
        else if(!found && i == len_path -1)
        {
            printf("Error: File not found\n");
            return NULL;
        }


        // garbage collection
        for(int j = 0; j < n; j++)
        {
            free(content_names[j]);
        }
        free(content_names);
        free(indices);
    }
    info(1, "%s\n", curr_dir->internal.file_name);
    if(curr_dir->internal.file_type != DATA_FILE)
    {
        printf("Error: Invalid file type\n");
        return NULL;
    }
    //confirm path exists, leads to a file
    //allocate space for the file, assign its fs and inode. Set offset to 0.
    //return file
    fs_file_t file = malloc(sizeof(fs_file_t));
    if(!file) return NULL;
    file->fs = context->fs;
    file->inode = curr_dir;
    file->offset = 0;
    return file;
}

void fs_close(fs_file_t file)
{
    (void)file;
}

size_t fs_read(fs_file_t file, void *buffer, size_t n)
{
    if(!file || !buffer) return 0;
    inode_t *inode = file->inode;
    inode_read_data(file->fs, inode, file->offset, buffer, n, &n);
    file->offset += n;
    return n;
}

size_t fs_write(fs_file_t file, void *buffer, size_t n)
{
    if(!file || !buffer) return 0;
    inode_t *inode = file->inode;
    fs_retcode_t ret = inode_modify_data(file->fs, inode, file->offset, buffer, n);
    if(ret != SUCCESS)
    {
        return 0;
    }

    file->offset += n;
    return n;
}

// Updates the offset stored in file based on the mode seek_mode and the offset.
// Returns -1 on failure and 0 on a successful seek operations.
// If the final offset is less than 0, this is a failed operation. No changes should be made to file, i.e. the state of file before the function call must be equal to its state after the function call.
// If the final offset is greater than the file size, set it to the file size. This is still an succesful seek operation.
// If the seek mode is FS_SEEK_START, the offset is the offset from the beginning of the file.
// If the seek mode is FS_SEEK_CURRENT, the offset is the offset from the current offset stored in file.
// If the seek mode is FS_SEEK_END, the offset is the offset from the end of the file.
int fs_seek(fs_file_t file, seek_mode_t seek_mode, int offset)
{
    if(!file || file == (fs_file_t)-1) return -1;
    if(seek_mode != FS_SEEK_START && seek_mode != FS_SEEK_CURRENT && seek_mode != FS_SEEK_END) 
        return -1;
    if(!file->inode) return -1;
    if(seek_mode == FS_SEEK_START)
    {
        if(offset < 0)
        {
            return -1;
        }
        file->offset = offset;
    }
    else if(seek_mode == FS_SEEK_CURRENT)
    {
        if(offset < 0 && (signed) file->offset + offset < 0)
        {
            return -1;
        }
        file->offset += offset;
    }
    else if(seek_mode == FS_SEEK_END)
    {
        file->offset = file->inode->internal.file_size + offset;
    }
    else if(file->offset > file->inode->internal.file_size)
    {
        file->offset = file->inode->internal.file_size;
    }


    if(file->offset > file->inode->internal.file_size)
    {
        file->offset = file->inode->internal.file_size;
        return 0;
    }
    return 0;
}

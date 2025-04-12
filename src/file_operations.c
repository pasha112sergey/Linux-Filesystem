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

fs_file_t fs_open_dir(terminal_context_t *context, char *path)
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

    info(1, "next available is: %d\n", context->fs->available_inode);
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

    info(1, "next available is: %d\n", context->fs->available_inode);
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

    // error somewhere, here, i might not be writing it correctly
    // char *contents = write_index_and_name(*new_inode_index, dest);
    // debug_contents((byte*)contents, DIRECTORY_ENTRY_SIZE);
    // inode_write_data(context->fs, curr_dir->inode, contents, DIRECTORY_ENTRY_SIZE);
    // info(1, "New inode index: %d (0x%02x)\n", *new_inode_index, *new_inode_index);
    
    // //check if there is enough space in the file system
    char *contents = write_index_and_name(*new_inode_index, dest);
    debug_contents((byte*)contents, DIRECTORY_ENTRY_SIZE);
    
    // Get current directory contents to find tombstone or append position
    byte *curr_contents = malloc(curr_dir->inode->internal.file_size);
    size_t curr_size = curr_dir->inode->internal.file_size;
    inode_read_data(context->fs, curr_dir->inode, 0, curr_contents, curr_size, &curr_size);
    size_t write_offset = curr_size;  // Default to appending
    for(size_t i = 0; i < curr_size; i += DIRECTORY_ENTRY_SIZE) {
        // Debug print current entry
        info(1, "Checking entry at offset %zu: [%02x %02x]\n", 
             i, curr_contents[i], curr_contents[i+1]);
        
        if(curr_contents[i] == 0 && curr_contents[i+1] == 0) {
            // Found a tombstone
            info(1, "Found tombstone at offset %zu\n", i);
            write_offset = i;
            break;
        }
    }
    info(1, "Final write offset: %zu\n", write_offset);
    // // Find tombstone or end of directory
    // size_t write_offset = curr_size;  // Default to appending
    // for(size_t i = 0; i < curr_size; i += DIRECTORY_ENTRY_SIZE) {
    //     if(curr_contents[i] == 0 && curr_contents[i+1] == 0) {
    //         // Found a tombstone
    //         write_offset = i;
    //         break;
    //     }
    // }
    
    // Write the new entry at the correct offset
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
    (void) context;
    (void) path;
    return -2;
}

int remove_file(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

// we can only delete a directory if it is empty!!
int remove_directory(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

int change_directory(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

int list(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
}

char *get_path_string(terminal_context_t *context)
{
    (void) context;

    return NULL;
}

int tree(terminal_context_t *context, char *path)
{
    (void) context;
    (void) path;
    return -2;
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


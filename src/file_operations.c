#include "filesys.h"
#include "debug.h"
#include "utility.h"

#include <string.h>

#define DIRECTORY_ENTRY_SIZE (sizeof(inode_index_t) + MAX_FILE_NAME_LEN)
#define DIRECTORY_ENTRIES_PER_DATABLOCK (DATA_BLOCK_SIZE / DIRECTORY_ENTRY_SIZE)

// ----------------------- CORE FUNCTION ----------------------- //
int new_file(terminal_context_t *context, char *path, permission_t perms)
{
    (void) context;
    (void) path;
    (void) perms;
    return -2;
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

char **split_string(char *path, int n) {

    // Allocate array of n string pointers
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
    char **file_path = split_string(dupe, len_path);
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
            // Allocate 14 bytes for name (13 chars + null terminator)
            char* name = calloc(14, 1); // Using calloc to zero-initialize
            // Store index in indices array
            indices[j] = get_index(contents, j);
            // Copy just the name portion (bytes 2-15)
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


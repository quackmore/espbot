/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

extern "C"
{
#include "espbot_event_codes.h"
}

#include "espbot_global.hpp"
#include "espbot_spiffs.hpp"
#include "espbot_diagnostic.hpp"

/**
 * @brief file system possible status
 * 
 *  FS_unmounted   -
 *  FS_mounted     -
 *
 */
typedef enum
{
    FS_unmounted = 0,
    FS_mounted
} flashfs_status;

/**
 * @brief SPIFFS memory allocation
 * 
 * ram_buffer - ram memory buffer being double the size of the logical page size
 * file_descriptors - 4 file descriptors => 4 file opened simultaneously
 * cache
 * 
 */
static struct
{
    u8_t ram_buffer[LOG_PAGE_SIZE * 2];
    u8_t file_descriptors[32 * 4];
#if SPIFFS_CACHE
    u8_t cache[(LOG_PAGE_SIZE + 32) * 4];
#else
    u8_t cache[1];
#endif
    spiffs handler;
    spiffs_config config;
    int status;
} esp_spiffs;

/**
 * @brief mounting the file system
 * 
 * SPIFFS_USE_MAGIC is enabled so following the documentation 
 * the function will:
 * 1) try mounting the FS
 * 2) if mounting fails with error SPIFFS_ERR_NOT_A_FS then
 *    format the FS 
 *    try mounting it again
 * 3) if mounting fails with error different from SPIFFS_ERR_NOT_A_FS then
 *    unmount the FS
 *    format the FS 
 *    try mounting it again
 * 
 */
void esp_spiffs_mount(void)
{
    esp_spiffs.status = FS_unmounted;

    esp_spiffs.config.phys_size = FS_END - FS_START;
    esp_spiffs.config.phys_addr = FS_START;
    esp_spiffs.config.phys_erase_block = (1024 * 4);
    esp_spiffs.config.log_block_size = (1024 * 4);
    esp_spiffs.config.log_page_size = LOG_PAGE_SIZE;
    esp_spiffs.config.hal_read_f = esp_spiffs_read;
    esp_spiffs.config.hal_write_f = esp_spiffs_write;
    esp_spiffs.config.hal_erase_f = esp_spiffs_erase;
    s32_t res = SPIFFS_mount(&esp_spiffs.handler,
                             &esp_spiffs.config,
                             esp_spiffs.ram_buffer,
                             esp_spiffs.file_descriptors,
                             sizeof(esp_spiffs.file_descriptors),
                             esp_spiffs.cache,
                             sizeof(esp_spiffs.cache),
                             0);
    if (res != SPIFFS_OK)
    {
        if (res == SPIFFS_ERR_MAGIC_NOT_POSSIBLE)
        {
            dia_fatal_evnt(SPIFFS_INIT_CANNOT_MOUNT, res);
            FATAL("Error %d mounting File System", res);
            FATAL("Try another page size or block size");
            return;
        }
        if (res == SPIFFS_ERR_NOT_A_FS)
        {
            TRACE("Error %d mounting File System, will try to format the file system", res);
        }
        if (res != SPIFFS_ERR_NOT_A_FS)
        {
            TRACE("Unmounting File System");
            SPIFFS_unmount(&esp_spiffs.handler);
        }
        TRACE("Formatting File System");
        res = SPIFFS_format(&esp_spiffs.handler);
        if (res == SPIFFS_OK)
        {
            dia_info_evnt(SPIFFS_INIT_FS_FORMATTED);
            INFO("File System formatted");
        }
        else
        {
            dia_fatal_evnt(SPIFFS_INIT_CANNOT_FORMAT);
            FATAL("Error %d formatting File System", res);
            return;
        }
        res = SPIFFS_mount(&esp_spiffs.handler,
                           &esp_spiffs.config,
                           esp_spiffs.ram_buffer,
                           esp_spiffs.file_descriptors,
                           sizeof(esp_spiffs.file_descriptors),
                           esp_spiffs.cache,
                           sizeof(esp_spiffs.cache),
                           0);
        if (res != SPIFFS_OK)
        {
            dia_fatal_evnt(SPIFFS_INIT_CANNOT_MOUNT, res);
            FATAL("Error %d mounting File System", res);
            return;
        }
    }
    dia_info_evnt(SPIFFS_INIT_FS_MOUNTED);
    INFO("File System mounted");
    esp_spiffs.status = FS_mounted;
    u32_t total = 0;
    u32_t used = 0;
    res = SPIFFS_info(&esp_spiffs.handler, &total, &used);
    dia_info_evnt(SPIFFS_INIT_FS_SIZE, total);
    dia_info_evnt(SPIFFS_INIT_FS_USED, used);
    INFO("File System size [bytes]: %d, used [bytes]:%d", total, used);
    espmem.stack_mon();
}

u32_t esp_spiffs_total_size()
{
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(SPIFFS_GET_TOTAL_SIZE_FS_NOT_MOUNTED);
        ERROR("FS is not mounted, cannot get total size");
        return 0;
    }
    s32_t res;
    u32_t total = 0;
    u32_t used = 0;
    res = SPIFFS_info(&esp_spiffs.handler, &total, &used);
    espmem.stack_mon();
    return total;
}

u32_t esp_spiffs_used_size()
{
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(SPIFFS_GET_USED_SIZE_FS_NOT_MOUNTED);
        ERROR("FS is not mounted, cannot get total size");
        return 0;
    }
    s32_t res;
    u32_t total = 0;
    u32_t used = 0;
    res = SPIFFS_info(&esp_spiffs.handler, &total, &used);
    espmem.stack_mon();
    return used;
}

s32_t esp_spiffs_check()
{
    ALL("esp_spiffs_check");
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(SPIFFS_CHECK_FS_NOT_MOUNTED);
        ERROR("FS is not mounted, cannot check it");
        return 0;
    }
    s32_t res = SPIFFS_check(&esp_spiffs.handler);
    if (res == SPIFFS_OK)
    {
        dia_info_evnt(SPIFFS_CHECK_SUCCESSFULLY);
        INFO("Successfully checked File System");
    }
    else
    {
        dia_error_evnt(SPIFFS_CHECK_ERRORS, res);
        ERROR("Error %d checking File System", res);
    }
    espmem.stack_mon();
    return res;
}

struct spiffs_dirent *esp_spiffs_list(int file_idx)
{
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(SPIFFS_LIST_FS_NOT_MOUNTED);
        ERROR("FS is not mounted, cannot list");
        return NULL;
    }
    static spiffs_DIR dd;
    static struct spiffs_dirent directory;
    struct spiffs_dirent *pfile;

    if (file_idx == 0)
        SPIFFS_opendir(&esp_spiffs.handler, "/", &dd);
    pfile = SPIFFS_readdir(&dd, &directory);
    if (pfile == NULL)
        SPIFFS_closedir(&dd);
    espmem.stack_mon();
    return pfile;
}

/**
 * @brief Espfile 
 * 
 */

/**
 * @brief Construct a new Espfile:: Espfile object
 * Open 'filename', or create it when it does not exists
 * on success: the file is ready for READ and WRITE operations
 * on failure: _err is set according to error code 
 * @param filename 
 */
Espfile::Espfile(char *filename)
{
    _handler = -1; // no handler
    _err = SPIFFS_OK;
    os_memset(_name, 0, 32);
    os_strncpy(_name, filename, 31);
    if (os_strlen(filename) > 31)
    {
        dia_warn_evnt(ESPFILE_NAME_TRUNCATED);
        WARN("Filename truncated to 31 characters");
    }
    if (esp_spiffs.status != FS_mounted)
    {
        _err = SPIFFS_ERR_NOT_MOUNTED;
        dia_error_evnt(ESPFILE_FS_NOT_MOUNTED);
        ERROR("Espfile::Espfile FS not mounted");
        return;
    }
    _handler = SPIFFS_open(&esp_spiffs.handler, _name, SPIFFS_CREAT | SPIFFS_RDWR | SPIFFS_APPEND, 0);
    if (_handler < 0)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_OPEN_ERROR, _err);
        ERROR("Espfile::Espfile error %d opening file %s", _err, _name);
    }
}

// close the file (if open)
// and eventually flush chache to flash memory
/**
 * @brief Destroy the Espfile:: Espfile object
 * 
 */
Espfile::~Espfile()
{
    if (_handler < 0)
        // this file was not open...
        return;
    s32_t res = SPIFFS_close(&esp_spiffs.handler, _handler);
    if (res != SPIFFS_OK)
    {
        dia_error_evnt(ESPFILE_CLOSE_ERROR, SPIFFS_errno(&esp_spiffs.handler));
        ERROR("Espfile::~Espfile error %d closing file %s", SPIFFS_errno(&esp_spiffs.handler), _name);
    }
}

s32_t Espfile::n_read(char *buffer, int len)
{
    s32_t res = SPIFFS_OK;
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(ESPFILE_N_READ_FS_NOT_MOUNTED);
        ERROR("Espfile::n_read FS not mounted");
        return SPIFFS_ERR_NOT_MOUNTED;
    }
    if ((_handler < 0) || (_err != SPIFFS_OK))
    {
        // there was a previous error managing this file...
        // any further operation is not consistent
        return SPIFFSESP_ERR_NOTCONSISTENT;
    }
    res = SPIFFS_read(&esp_spiffs.handler, _handler, (u8_t *)buffer, len);
    if (res < SPIFFS_OK)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_N_READ_READ_ERROR, _err);
        ERROR("Espfile::n_read error %d reading file %s", _err, _name);
    }
    espmem.stack_mon();
    return res;
}

s32_t Espfile::n_read(char *buffer, int offset, int len)
{
    s32_t res = SPIFFS_OK;
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(ESPFILE_N_READ_OFFSET_FS_NOT_MOUNTED);
        ERROR("Espfile::n_read_offset FS not mounted");
        return SPIFFS_ERR_NOT_MOUNTED;
    }
    if ((_handler < 0) || (_err != SPIFFS_OK))
    {
        // there was a previous error managing this file...
        // any further operation is not consistent
        return SPIFFSESP_ERR_NOTCONSISTENT;
    }
    res = SPIFFS_lseek(&esp_spiffs.handler, _handler, offset, SPIFFS_SEEK_SET);
    if (res < SPIFFS_OK)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_N_READ_SEEK_ERROR, _err);
        ERROR("Espfile::n_read error %d seeking into file %s", _err, _name);
        return res;
    }
    res = SPIFFS_read(&esp_spiffs.handler, _handler, (u8_t *)buffer, len);
    if (res < SPIFFS_OK)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_N_READ_OFFSET_READ_ERROR, _err);
        ERROR("Espfile::n_read error %d reading file %s", _err, _name);
        return res;
    }
    espmem.stack_mon();
    return res;
}

s32_t Espfile::n_append(char *buffer, int len)
{
    s32_t res = SPIFFS_OK;
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(ESPFILE_N_APPEND_FS_NOT_MOUNTED);
        ERROR("Espfile::n_append FS not mounted");
        return SPIFFS_ERR_NOT_MOUNTED;
    }
    if ((_handler < 0) || (_err != SPIFFS_OK))
    {
        // there was a previous error managing this file...
        // any further operation is not consistent
        return SPIFFSESP_ERR_NOTCONSISTENT;
    }
    res = SPIFFS_write(&esp_spiffs.handler, _handler, (u8_t *)buffer, len);
    if (res < SPIFFS_OK)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_N_APPEND_WRITE_ERROR, _err);
        ERROR("Espfile::n_append error %d writing file %s", _err, _name);
    }
    espmem.stack_mon();
    return res;
}

s32_t Espfile::clear()
{
    s32_t res = SPIFFS_OK;
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(ESPFILE_CLEAR_FS_NOT_MOUNTED);
        ERROR("Espfile::clear FS not mounted");
        return SPIFFS_ERR_NOT_MOUNTED;
    }
    if ((_handler < 0) || (_err != SPIFFS_OK))
    {
        // there was a previous error managing this file...
        // any further operation is not consistent
        return SPIFFSESP_ERR_NOTCONSISTENT;
    }
    res = SPIFFS_close(&esp_spiffs.handler, _handler);
    if (res != SPIFFS_OK)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_CLEAR_CLOSE_ERROR, _err);
        ERROR("Espfile::clear error %d closing file %s", _err, _name);
        return _err;
    }
    _handler = SPIFFS_open(&esp_spiffs.handler, _name, SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR | SPIFFS_APPEND, 0);
    if (_handler < 0)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_CLEAR_OPEN_ERROR, _err);
        ERROR("Espfile::clear error %d opening file %s", _err, _name);
        return _err;
    }
    espmem.stack_mon();
    return res;
}

s32_t Espfile::remove()
{
    s32_t res = SPIFFS_OK;
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(ESPFILE_REMOVE_FS_NOT_MOUNTED);
        ERROR("Espfile::remove FS not mounted");
        return SPIFFS_ERR_NOT_MOUNTED;
    }
    if ((_handler < 0) || (_err != SPIFFS_OK))
    {
        // there was a previous error managing this file...
        // any further operation is not consistent
        return SPIFFSESP_ERR_NOTCONSISTENT;
    }
    res = SPIFFS_fremove(&esp_spiffs.handler, _handler);
    if (res != SPIFFS_OK)
    {
        _err = SPIFFS_errno(&esp_spiffs.handler);
        dia_error_evnt(ESPFILE_REMOVE_ERROR, _err);
        ERROR("Espfile::remove error %d removing file %s", _err, _name);
        return res;
    }
    _handler = -1; // so the destructor won't try to close the file
    return res;
}

bool Espfile::exists(char *name)
{
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(ESPFILE_EXISTS_FS_NOT_MOUNTED);
        ERROR("Espfile::exists FS not mounted");
        return false;
    }
    spiffs_DIR directory;
    struct spiffs_dirent tmp_file;
    struct spiffs_dirent *file_ptr;

    // make a copy of filename and trunk it to 31 characters
    char filename[32];
    os_memset(filename, 0, 32);
    os_strncpy(filename, name, 31);
    // search for the file
    SPIFFS_opendir(&esp_spiffs.handler, "/", &directory);
    while ((file_ptr = SPIFFS_readdir(&directory, &tmp_file)))
    {
        if (0 == os_strcmp(filename, (char *)file_ptr->name))
            return true;
    }
    SPIFFS_closedir(&directory);
    espmem.stack_mon();
    return false;
}

int Espfile::size(char *name)
{
    if (esp_spiffs.status != FS_mounted)
    {
        dia_error_evnt(ESPFILE_SIZE_FS_NOT_MOUNTED);
        ERROR("Espfile::size FS not mounted");
        return -1;
    }
    spiffs_DIR directory;
    struct spiffs_dirent tmp_file;
    struct spiffs_dirent *file_ptr;
    espmem.stack_mon();

    SPIFFS_opendir(&esp_spiffs.handler, "/", &directory);
    while ((file_ptr = SPIFFS_readdir(&directory, &tmp_file)))
    {
        if (0 == os_strncmp(name, (char *)file_ptr->name, os_strlen(name)))
            return file_ptr->size;
    }
    SPIFFS_closedir(&directory);
    return -1;
}
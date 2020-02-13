/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "spiffs_flash_functions.hpp"
#include "espbot_global.hpp"
#include "spiffs_esp8266.hpp"

Flashfs::Flashfs()
{
    status = FFS_NOT_INIT;
}

void Flashfs::init(void)
{
    // SPIFFS_USE_MAGIC is enabled so following documentation:
    // 1) Call SPIFFS_mount
    // 2) If SPIFFS_mount fails with SPIFFS_ERR_NOT_A_FS, keep going.
    // 3) Otherwise, call SPIFFS_unmount and call SPIFFS_format
    // 4) Call SPIFFS_mount again.
    s32_t res;
    status = FFS_UNAVAILABLE;

    m_config.phys_size = FS_END - FS_START; // use all spi flash
    m_config.phys_addr = FS_START;          // start spiffs at start of spi flash
    m_config.phys_erase_block = (1024 * 4); // according to datasheet
    m_config.log_block_size = (1024 * 4);   // let us not complicate things
    m_config.log_page_size = LOG_PAGE_SIZE; // as we said
    m_config.hal_read_f = esp_spiffs_read;
    m_config.hal_write_f = esp_spiffs_write;
    m_config.hal_erase_f = esp_spiffs_erase;
    res = SPIFFS_mount(&m_fs,
                       &m_config,
                       m_work,
                       m_fd_space,
                       sizeof(m_fd_space),
                       m_cache,
                       sizeof(m_cache),
                       0);
    if (res != SPIFFS_OK)
    {
        if (res == SPIFFS_ERR_MAGIC_NOT_POSSIBLE)
        {
            esp_diag.fatal(SPIFFS_INIT_CANNOT_MOUNT, res);
            FATAL("Error %d mounting File System", res);
            FATAL("Try another page size or block size");
            status = FFS_UNAVAILABLE;
            return;
        }
        if (res == SPIFFS_ERR_NOT_A_FS)
        {
            TRACE("Error %d mounting File System, will try to format the file system", res);
        }
        if (res != SPIFFS_ERR_NOT_A_FS)
        {
            TRACE("Unmounting File System");
            SPIFFS_unmount(&m_fs);
        }
        TRACE("Formatting File System");
        res = SPIFFS_format(&m_fs);
        if (res == SPIFFS_OK)
        {
            esp_diag.info(SPIFFS_INIT_FS_FORMATTED);
            INFO("File System formatted");
        }
        else
        {
            esp_diag.fatal(SPIFFS_INIT_CANNOT_FORMAT);
            FATAL("Error %d formatting File System", res);
            status = FFS_UNAVAILABLE;
            return;
        }
        res = SPIFFS_mount(&m_fs,
                           &m_config,
                           m_work,
                           m_fd_space,
                           sizeof(m_fd_space),
                           m_cache,
                           sizeof(m_cache),
                           0);
        if (res != SPIFFS_OK)
        {
            esp_diag.fatal(SPIFFS_INIT_CANNOT_MOUNT, res);
            FATAL("Error %d mounting File System", res);
            status = FFS_UNAVAILABLE;
            return;
        }
    }
    esp_diag.info(SPIFFS_INIT_FS_MOUNTED);
    INFO("File System mounted");
    status = FFS_AVAILABLE;
    u32_t total = 0;
    u32_t used = 0;
    res = SPIFFS_info(&m_fs, &total, &used);
    esp_diag.info(SPIFFS_INIT_FS_SIZE, total);
    esp_diag.info(SPIFFS_INIT_FS_USED, used);
    INFO("File System size [bytes]: %d, used [bytes]:%d", total, used);
    espmem.stack_mon();
}

void Flashfs::format(void)
{
    ALL("Flashfs::format");
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_FORMAT_FS_NOT_INIT);
        ERROR("Cannot format a not initialized File System");
        return;
    }
    s32_t res;
    TRACE("Unmounting File System");
    SPIFFS_unmount(&m_fs);
    TRACE("Formatting File System");
    res = SPIFFS_format(&m_fs);
    if (res == SPIFFS_OK)
    {
        esp_diag.info(SPIFFS_FORMAT_FS_FORMATTED);
        INFO("File System formatted");
        status = FFS_UNMOUNTED;
    }
    else
    {
        esp_diag.fatal(SPIFFS_FORMAT_CANNOT_FORMAT, res);
        FATAL("Error %d formatting File System", res);
        status = FFS_UNAVAILABLE;
    }
    espmem.stack_mon();
}

void Flashfs::unmount(void)
{
    ALL("Flashfs::unmount");
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_UNMOUNT_FS_NOT_INIT);
        ERROR("Unmounting a not initialized File System");
        return;
    }
    SPIFFS_unmount(&m_fs);
    esp_diag.info(SPIFFS_UNMOUNT_FS_UNMOUNTED);
    INFO("File System unmounted");
    status = FFS_UNMOUNTED;
}

flashfs_status Flashfs::get_status()
{
    return status;
}

bool Flashfs::is_available()
{
    if ((status == FFS_AVAILABLE) || (status == FFS_ERRORS))
        return true;
    else
        return false;
}

s32_t Flashfs::last_error()
{
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_LAST_ERROR_FS_NOT_INIT);
        ERROR("Looking for last error of a not initialized File System");
        return 0;
    }
    return SPIFFS_errno(&m_fs);
}

u32_t Flashfs::get_total_size()
{
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_GET_TOTAL_SIZE_FS_NOT_INIT);
        ERROR("Looking for total size of a not initialized File System");
        return 0;
    }
    s32_t res;
    u32_t total = 0;
    u32_t used = 0;
    res = SPIFFS_info(&m_fs, &total, &used);
    espmem.stack_mon();
    return total;
}

u32_t Flashfs::get_used_size()
{
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_GET_USED_SIZE_FS_NOT_INIT);
        ERROR("Looking for used size of a not initialized File System");
        return 0;
    }
    s32_t res;
    u32_t total = 0;
    u32_t used = 0;
    res = SPIFFS_info(&m_fs, &total, &used);
    espmem.stack_mon();
    return used;
}

s32_t Flashfs::check()
{
    ALL("Flashfs::check");
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_CHECK_FS_NOT_INIT);
        ERROR("Checking a not initialized File System");
        return 0;
    }
    s32_t res = SPIFFS_check(&m_fs);
    if (res == SPIFFS_OK)
    {
        esp_diag.info(SPIFFS_CHECK_SUCCESSFULLY);
        INFO("Successfully checked File System");
        status = FFS_AVAILABLE;
    }
    else
    {
        esp_diag.info(SPIFFS_CHECK_ERRORS, res);
        ERROR("Error %d checking File System", res);
        status = FFS_ERRORS;
    }
    espmem.stack_mon();
    return res;
}

struct spiffs_dirent *Flashfs::list(int t_file)
{
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_CHECK_FS_NOT_INIT);
        ERROR("Listing a not initialized File System");
        return NULL;
    }
    static spiffs_DIR dd;
    static struct spiffs_dirent ffile;
    struct spiffs_dirent *pfile;

    if (t_file == 0)
        SPIFFS_opendir(&m_fs, "/", &dd);
    pfile = SPIFFS_readdir(&dd, &ffile);
    if (pfile == NULL)
        SPIFFS_closedir(&dd);
    espmem.stack_mon();
    return pfile;
}

spiffs *Flashfs::get_handler()
{
    if (status == FFS_NOT_INIT)
    {
        esp_diag.error(SPIFFS_GET_HANDLER_NOT_INIT);
        ERROR("Looking for handler of a not initialized File System");
        return NULL;
    }
    return &m_fs;
}

// create a new file with no name, no operations will be permitted
// the file status is set to FFS_F_UNAVAILABLE
Ffile::Ffile(Flashfs *t_fs)
{
    status = FFS_F_UNAVAILABLE;
    m_name[0] = '\0';
    if (t_fs->is_available())
    {
        m_fs = t_fs;
    }
    else
    {
        m_fs = NULL;
        esp_diag.error(SPIFFS_FFILE_FS_NOT_AVAILABLE);
        ERROR("Ffile::Ffile FS not available");
    }
}

// create a new file variable with the specified name
// create a new file, or open if it exists, ready for READ and WRITE (APPEND) operations
// in case of errors the file status is set to FFS_F_UNAVAILABLE
Ffile::Ffile(Flashfs *t_fs, char *t_filename)
{
    status = FFS_F_UNAVAILABLE;
    os_strncpy(m_name, t_filename, 30);
    if (os_strlen(t_filename) > 30)
    {
        esp_diag.warn(SPIFFS_FFILE_NAME_TRUNCATED);
        WARN("Filename truncated to 30 characters");
        m_name[30] = '\0';
    }
    if (t_fs->is_available())
    {
        m_fs = t_fs;
        m_fd = SPIFFS_open(m_fs->get_handler(), m_name, SPIFFS_CREAT | SPIFFS_RDWR | SPIFFS_APPEND, 0);
        if (m_fd < 0)
        {
            esp_diag.error(SPIFFS_FFILE_FILE_OPEN_ERROR, SPIFFS_errno(m_fs->get_handler()));
            ERROR("Ffile::Ffile error %d opening file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
        }
        else
            status = FFS_F_OPEN;
    }
    else
    {
        m_fs = NULL;
        esp_diag.error(SPIFFS_FFILE_FS_NOT_AVAILABLE);
        ERROR("Ffile::Ffile FS not available");
    }
}

// close the file (if open)
// and eventually flush chache to flash memory
Ffile::~Ffile()
{
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_close(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_FFILE_DES_FILE_CLOSE_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::~Ffile error %d closing file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
        }
    }
}

// return the file name
char *Ffile::get_name()
{
    if (os_strlen(m_name) == 0)
    {
        esp_diag.error(SPIFFS_GET_NAME_NO_NAME);
        ERROR("File has no name");
        status = FFS_F_UNAVAILABLE;
    }
    return m_name;
}

// set the filename
// if the file was open with a different filename it will be closed and changed saved to flash
// then
// create a new file, or open if it exists, ready for READ and WRITE (APPEND) operations
// in case of errors the file status is set to FFS_F_UNAVAILABLE
void Ffile::open(char *t_filename)
{
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_close(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_OPEN_FILE_CLOSE_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::open error %d while closing file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
            espmem.stack_mon();
        }
        os_strncpy(m_name, t_filename, 30);
        if (os_strlen(t_filename) > 30)
        {
            esp_diag.warn(SPIFFS_OPEN_NAME_TRUNCATED);
            WARN("Ffile::open filename truncated to 30 characters");
            m_name[30] = '\0';
        }
        m_fd = SPIFFS_open(m_fs->get_handler(), m_name, SPIFFS_CREAT | SPIFFS_RDWR | SPIFFS_APPEND, 0);
        if (m_fd < 0)
        {
            esp_diag.error(SPIFFS_OPEN_FILE_OPEN_ERROR, SPIFFS_errno(m_fs->get_handler()));
            ERROR("Ffile::open error %d opening file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            status = FFS_F_UNAVAILABLE;
        }
        else
            status = FFS_F_OPEN;
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esp_diag.error(SPIFFS_OPEN_FS_NOT_AVAILABLE);
        ERROR("Ffile::open FS not available");
    }
}

// return the file status
flashfs_file_status Ffile::get_status()
{
    return status;
}

// return the file status
bool Ffile::is_available()
{
    if ((status == FFS_F_OPEN) || (FFS_F_MODIFIED_UNSAVED))
        return true;
    else
        return false;
}

// read t_len bytes from the file to the t_buffer
int Ffile::n_read(char *t_buffer, int t_len)
{
    s32_t res = 0;
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            res = SPIFFS_read(m_fs->get_handler(), m_fd, (u8_t *)t_buffer, t_len);
            if (res < SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_N_READ_FILE_READ_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::n_read error %d reading file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
        }
        else
        {
            esp_diag.error(SPIFFS_N_READ_CANNOT_READ, status);
            ERROR("Ffile::n_read cannot read file %s, file status is %d", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esp_diag.error(SPIFFS_N_READ_FS_NOT_AVAILABLE);
        ERROR("Ffile::n_read FS not available");
        res = -1;
    }
    espmem.stack_mon();
    return (int)res;
}

// read t_len bytes from the file + offset to the t_buffer
int Ffile::n_read(char *t_buffer, int offset, int t_len)
{
    s32_t res = 0;
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            res = SPIFFS_lseek(m_fs->get_handler(), m_fd, offset, SPIFFS_SEEK_SET);
            if (res < SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_N_READ_FILE_SEEK_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::n_read error %d seeking into file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
                return (int)res;
            }
            res = SPIFFS_read(m_fs->get_handler(), m_fd, (u8_t *)t_buffer, t_len);
            if (res < SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_N_READ_FILE_READ_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::n_read error %d reading file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
                return (int)res;
            }
        }
        else
        {
            esp_diag.error(SPIFFS_N_READ_CANNOT_READ, status);
            ERROR("Ffile::n_read cannot read file %s, file status is %d", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esp_diag.error(SPIFFS_N_READ_FS_NOT_AVAILABLE);
        ERROR("Ffile::n_read FS not available");
        res = -1;
    }
    espmem.stack_mon();
    return (int)res;
}

// write (append) t_len bytes from the t_buffer to the file
int Ffile::n_append(char *t_buffer, int t_len)
{
    s32_t res = 0;
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            res = SPIFFS_write(m_fs->get_handler(), m_fd, (u8_t *)t_buffer, t_len);
            if (res < SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_N_APPEND_FILE_WRITE_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::n_append error %d writing file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
            else
                status = FFS_F_MODIFIED_UNSAVED;
        }
        else
        {
            esp_diag.error(SPIFFS_N_APPEND_CANNOT_WRITE, status);
            ERROR("Ffile::n_append cannot write file %s, file status is %d", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esp_diag.error(SPIFFS_N_APPEND_FS_NOT_AVAILABLE);
        ERROR("Ffile::n_append FS not available");
        res = -1;
    }
    espmem.stack_mon();
    return (int)res;
}

// clear the file content
void Ffile::clear()
{
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_close(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_CLEAR_FILE_CLOSE_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::clear error %d closing file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
            m_fd = SPIFFS_open(m_fs->get_handler(), m_name, SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR | SPIFFS_APPEND, 0);
            if (m_fd < 0)
            {
                esp_diag.error(SPIFFS_CLEAR_FILE_OPEN_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::clear error %d opening file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
                status = FFS_F_UNAVAILABLE;
            }
            else
                status = FFS_F_MODIFIED_UNSAVED;
            espmem.stack_mon();
        }
        else
        {
            esp_diag.error(SPIFFS_CLEAR_CANNOT_CLEAR, status);
            ERROR("Ffile::clear cannot clear file %s, file status is %d", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esp_diag.error(SPIFFS_CLEAR_FS_NOT_AVAILABLE);
        ERROR("Ffile::clear FS not available");
    }
}

// remove the file
void Ffile::remove()
{
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_fremove(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_REMOVE_FILE_REMOVE_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::remove error %d removing file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
            else
                status = FFS_F_REMOVED;
            espmem.stack_mon();
        }
        else
        {
            esp_diag.error(SPIFFS_REMOVE_CANNOT_REMOVE, status);
            ERROR("Ffile::remove cannot remove file %s, file status is %d", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esp_diag.error(SPIFFS_REMOVE_FS_NOT_AVAILABLE);
        ERROR("Ffile::remove FS not available");
    }
}

// flush chached changes to the flash memory
void Ffile::flush_cache()
{
    if (m_fs && (m_fs->is_available()))
    {
        if (status == FFS_F_MODIFIED_UNSAVED)
        {
            s32_t res = SPIFFS_fflush(m_fs->get_handler(), m_fd);
            if (res < SPIFFS_OK)
            {
                esp_diag.error(SPIFFS_FLUSH_FILE_FLUSH_ERROR, SPIFFS_errno(m_fs->get_handler()));
                ERROR("Ffile::flush_cache error %d flushing cache for file %s", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
            espmem.stack_mon();
        }
        else
        {
            esp_diag.error(SPIFFS_FLUSH_CANNOT_FLUSH, status);
            ERROR("Ffile::flush_cache annot flush cache for file %s, file status is %d", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esp_diag.error(SPIFFS_FLUSH_FS_NOT_AVAILABLE);
        ERROR("Ffile::flush_cache FS not available");
    }
}

bool Ffile::exists(Flashfs *t_fs, char *t_name)
{
    spiffs_DIR directory;
    struct spiffs_dirent tmp_file;
    struct spiffs_dirent *file_ptr;

    if (t_fs->is_available())
    {
        SPIFFS_opendir(t_fs->get_handler(), "/", &directory);
        while ((file_ptr = SPIFFS_readdir(&directory, &tmp_file)))
        {
            if (0 == os_strncmp(t_name, (char *)file_ptr->name, os_strlen(t_name)))
            {
                return true;
            }
        }
        SPIFFS_closedir(&directory);
    }
    else
    {
        esp_diag.error(SPIFFS_EXISTS_FS_NOT_AVAILABLE);
        ERROR("Ffile::exists FS not available");
    }
    espmem.stack_mon();
    return false;
}

int Ffile::size(Flashfs *t_fs, char *t_name)
{
    spiffs_DIR directory;
    struct spiffs_dirent tmp_file;
    struct spiffs_dirent *file_ptr;
    espmem.stack_mon();

    if (t_fs->is_available())
    {
        SPIFFS_opendir(t_fs->get_handler(), "/", &directory);
        while ((file_ptr = SPIFFS_readdir(&directory, &tmp_file)))
        {
            if (0 == os_strncmp(t_name, (char *)file_ptr->name, os_strlen(t_name)))
            {
                return file_ptr->size;
            }
        }
        SPIFFS_closedir(&directory);
    }
    else
    {
        esp_diag.error(SPIFFS_SIZE_FS_NOT_AVAILABLE);
        ERROR("Ffile::size FS not available");
    }
    return -1;
}
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "spiffs_flash_functions.hpp"
#include "spiffs_esp8266.hpp"

Flashfs::Flashfs()
{
    esplog.all("Flashfs::Flashfs\n");
    status = FFS_NOT_INIT;
}

void Flashfs::init(void)
{
    esplog.all("Flashfs::init\n");
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
            esplog.fatal("Error mounting the file system, error code %d\n", res);
            esplog.fatal("Try another page size or block size.\n");
            status = FFS_UNAVAILABLE;
            return;
        }
        if (res == SPIFFS_ERR_NOT_A_FS)
        {
            esplog.trace("Error mounting the file system, error code %d\n", res);
            esplog.trace("Will try to format the file system.\n");
        }
        if (res != SPIFFS_ERR_NOT_A_FS)
        {
            esplog.trace("Unmounting the file system.\n");
            SPIFFS_unmount(&m_fs);
        }
        esplog.trace("Formatting the file system.\n");
        res = SPIFFS_format(&m_fs);
        if (res == SPIFFS_OK)
        {
            esplog.trace("File system formatted.\n");
        }
        else
        {
            esplog.fatal("Cannot format file system, error code %d\n", res);
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
            esplog.fatal("Cannot mount file system, error code %d\n", res);
            status = FFS_UNAVAILABLE;
            return;
        }
    }
    esplog.info("File system mounted.\n");
    status = FFS_AVAILABLE;
    u32_t total = 0;
    u32_t used = 0;
    res = SPIFFS_info(&m_fs, &total, &used);
    esplog.info("File system size [bytes]: %d, used [bytes]:%d.\n", total, used);
    espmem.stack_mon();
}

void Flashfs::format(void)
{
    esplog.all("Flashfs::format\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Formatting a not initialized file system\n");
        return;
    }
    s32_t res;
    esplog.trace("Unmounting the file system.\n");
    SPIFFS_unmount(&m_fs);
    esplog.trace("Formatting the file system.\n");
    res = SPIFFS_format(&m_fs);
    if (res == SPIFFS_OK)
    {
        esplog.trace("File system formatted.\n");
        status = FFS_UNMOUNTED;
    }
    else
    {
        esplog.fatal("Cannot format file system, error code %d\n", res);
        status = FFS_UNAVAILABLE;
    }
    espmem.stack_mon();
}

void Flashfs::unmount(void)
{
    esplog.all("Flashfs::unmount\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Unmounting a not initialized file system\n");
        return;
    }
    SPIFFS_unmount(&m_fs);
    esplog.trace("File system unmounted.\n");
    status = FFS_UNMOUNTED;
}

flashfs_status Flashfs::get_status()
{
    esplog.all("Flashfs::get_status\n");
    return status;
}

bool Flashfs::is_available()
{
    esplog.all("Flashfs::is_available\n");
    if ((status == FFS_AVAILABLE) || (status == FFS_ERRORS))
        return true;
    else
        return false;
}

s32_t Flashfs::last_error()
{
    esplog.all("Flashfs::last_error\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Looking for last error of a not initialized file system\n");
        return 0;
    }
    return SPIFFS_errno(&m_fs);
}

u32_t Flashfs::get_total_size()
{
    esplog.all("Flashfs::get_total_size\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Looking for total size of a not initialized file system\n");
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
    esplog.all("Flashfs::get_used_size\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Looking for used size of a not initialized file system\n");
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
    esplog.all("Flashfs::check\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Checking a not initialized file system\n");
        return 0;
    }
    s32_t res = SPIFFS_check(&m_fs);
    if (res == SPIFFS_OK)
    {
        esplog.trace("Successfully checked the file system.\n");
        status = FFS_AVAILABLE;
    }
    else
    {
        esplog.error("File system check found errors, error code %d\n", res);
        status = FFS_ERRORS;
    }
    espmem.stack_mon();
    return res;
}

struct spiffs_dirent *Flashfs::list(int t_file)
{
    esplog.all("Flashfs::list\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Listing a not initialized file system\n");
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
    esplog.all("Flashfs::get_handler\n");
    if (status == FFS_NOT_INIT)
    {
        esplog.error("Looking for handler of a not initialized file system\n");
        return NULL;
    }
    return &m_fs;
}

// create a new file with no name, no operations will be permitted
// the file status is set to FFS_F_UNAVAILABLE
Ffile::Ffile(Flashfs *t_fs)
{
    esplog.all("Ffile::Ffile()\n");
    status = FFS_F_UNAVAILABLE;
    m_name[0] = '\0';
    if (t_fs->is_available())
    {
        m_fs = t_fs;
    }
    else
    {
        m_fs = NULL;
        esplog.error("Cannot create files cause the file system is not available\n");
    }
}

// create a new file variable with the specified name
// create a new file, or open if it exists, ready for READ and WRITE (APPEND) operations
// in case of errors the file status is set to FFS_F_UNAVAILABLE
Ffile::Ffile(Flashfs *t_fs, char *t_filename)
{
    esplog.all("Ffile::Ffile(,)\n");
    status = FFS_F_UNAVAILABLE;
    os_strncpy(m_name, t_filename, 30);
    if (os_strlen(t_filename) > 30)
    {
        esplog.warn("Filename will be truncated to 30 characters\n");
        m_name[30] = '\0';
    }
    if (t_fs->is_available())
    {
        m_fs = t_fs;
        m_fd = SPIFFS_open(m_fs->get_handler(), m_name, SPIFFS_CREAT | SPIFFS_RDWR | SPIFFS_APPEND, 0);
        if (m_fd < 0)
        {
            esplog.error("SPIFFS error %d while opening file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
        }
        else
            status = FFS_F_OPEN;
    }
    else
    {
        m_fs = NULL;
        esplog.error("Cannot create files cause the file system is not available.\n");
    }
}

// close the file (if open)
// and eventually flush chache to flash memory
Ffile::~Ffile()
{
    esplog.all("Ffile::~Ffile\n");
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_close(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
                esplog.error("SPIFFS error %d while closing file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
        }
    }
}

// return the file name
char *Ffile::get_name()
{
    esplog.all("Ffile::get_name\n");
    if (os_strlen(m_name) == 0)
    {
        esplog.error("The file has no name\n");
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
    esplog.all("Ffile::open\n");
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_close(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
                esplog.error("SPIFF error %d while closing file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
            espmem.stack_mon();
        }
        os_strncpy(m_name, t_filename, 30);
        if (os_strlen(t_filename) > 30)
        {
            esplog.warn("Filename will be truncated to 30 characters\n");
            m_name[30] = '\0';
        }
        m_fd = SPIFFS_open(m_fs->get_handler(), m_name, SPIFFS_CREAT | SPIFFS_RDWR | SPIFFS_APPEND, 0);
        if (m_fd < 0)
        {
            esplog.error("SPIFFS error %d while opening file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
            status = FFS_F_UNAVAILABLE;
        }
        else
            status = FFS_F_OPEN;
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esplog.error("opening file on a not available file system\n");
    }
}

// return the file status
flashfs_file_status Ffile::get_status()
{
    esplog.all("Ffile::get_status\n");
    return status;
}

// return the file status
bool Ffile::is_available()
{
    esplog.all("Ffile::is_available\n");
    if ((status == FFS_F_OPEN) || (FFS_F_MODIFIED_UNSAVED))
        return true;
    else
        return false;
}

// read t_len bytes from the file to the t_buffer
int Ffile::n_read(char *t_buffer, int t_len)
{
    esplog.all("Ffile::n_read\n");
    s32_t res = 0;
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            res = SPIFFS_read(m_fs->get_handler(), m_fd, (u8_t *)t_buffer, t_len);
            if (res < SPIFFS_OK)
            {
                esplog.error("SPIFFS error %d while reading from file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
        }
        else
        {
            esplog.error("Cannot read from file %s, file status is %d\n", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esplog.error("reading file on a not available file system\n");
        res = -1;
    }
    espmem.stack_mon();
    return (int)res;
}

// read t_len bytes from the file + offset to the t_buffer
int Ffile::n_read(char *t_buffer, int offset, int t_len)
{
    esplog.all("Ffile::n_read(offset)\n");
    s32_t res = 0;
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            res = SPIFFS_lseek(m_fs->get_handler(), m_fd, offset, SPIFFS_SEEK_SET);
            if (res < SPIFFS_OK)
            {
                esplog.error("SPIFFS error %d while seeking into file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
                return (int)res;
            }
            res = SPIFFS_read(m_fs->get_handler(), m_fd, (u8_t *)t_buffer, t_len);
            if (res < SPIFFS_OK)
            {
                esplog.error("SPIFFS error %d while reading from file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
                return (int)res;
            }
        }
        else
        {
            esplog.error("Cannot read from file %s, file status is %d\n", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esplog.error("reading file on a not available file system\n");
        res = -1;
    }
    espmem.stack_mon();
    return (int)res;
}

// write (append) t_len bytes from the t_buffer to the file
int Ffile::n_append(char *t_buffer, int t_len)
{
    esplog.all("Ffile::n_append\n");
    s32_t res = 0;
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            res = SPIFFS_write(m_fs->get_handler(), m_fd, (u8_t *)t_buffer, t_len);
            if (res < SPIFFS_OK)
            {
                esplog.error("SPIFFS error %d while writing to file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
            }
            else
                status = FFS_F_MODIFIED_UNSAVED;
        }
        else
        {
            esplog.error("Cannot write to file %s, file status is %d\n", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esplog.error("writing file on a not available file system\n");
        res = -1;
    }
    espmem.stack_mon();
    return (int)res;
}

// clear the file content
void Ffile::clear()
{
    esplog.all("Ffile::clear\n");
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_close(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
                esplog.error("SPIFFS error %d while closing file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
            m_fd = SPIFFS_open(m_fs->get_handler(), m_name, SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR | SPIFFS_APPEND, 0);
            if (m_fd < 0)
            {
                esplog.error("SPIFFS error %d while opening file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
                status = FFS_F_UNAVAILABLE;
            }
            else
                status = FFS_F_MODIFIED_UNSAVED;
            espmem.stack_mon();
        }
        else
        {
            esplog.error("Cannot clear file %s, file status is %d\n", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esplog.error("clearing file on a not available file system\n");
    }
}

// remove the file
void Ffile::remove()
{
    esplog.all("Ffile::remove\n");
    if (m_fs && (m_fs->is_available()))
    {
        if ((status == FFS_F_OPEN) || (status == FFS_F_MODIFIED_UNSAVED))
        {
            s32_t res = SPIFFS_fremove(m_fs->get_handler(), m_fd);
            if (res != SPIFFS_OK)
                esplog.error("SPIFFS error %d while removing file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
            else
                status = FFS_F_REMOVED;
            espmem.stack_mon();
        }
        else
        {
            esplog.error("Cannot remove file %s, file status is %d\n", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esplog.error("Removing file on a not available file system\n");
    }
}

// flush chached changes to the flash memory
void Ffile::flush_cache()
{
    esplog.all("Ffile::flush_cache\n");
    if (m_fs && (m_fs->is_available()))
    {
        if (status == FFS_F_MODIFIED_UNSAVED)
        {
            s32_t res = SPIFFS_fflush(m_fs->get_handler(), m_fd);
            if (res < SPIFFS_OK)
                esplog.error("SPIFFS error %d while flushing cache for file %s\n", SPIFFS_errno(m_fs->get_handler()), m_name);
            espmem.stack_mon();
        }
        else
        {
            esplog.error("Cannot flush cache for file %s, file status is %d\n", m_name, status);
        }
    }
    else
    {
        status = FFS_F_UNAVAILABLE;
        esplog.error("Flushing cache on a not available file system\n");
    }
}

bool Ffile::exists(Flashfs *t_fs, char *t_name)
{
    esplog.all("Ffile::exists\n");
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
        esplog.error("Checking if file exists on not available file system\n");
    }
    espmem.stack_mon();
    return false;
}

int Ffile::size(Flashfs *t_fs, char *t_name)
{
    esplog.all("Ffile::size\n");
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
        esplog.error("Checking file size on not available file system\n");
    }
    return -1;
}
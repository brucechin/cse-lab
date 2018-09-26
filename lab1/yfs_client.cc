// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client()
{
    ec = new extent_client();

}

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
    ec = new extent_client();
    if (ec->put(1, "") != extent_protocol::OK)
        printf("error init root dir\n"); // XYB: init root dir
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is a dir\n", inum);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool
yfs_client::isdir(inum inum)
{
    // Oops! is this still correct when you implement symlink?
    return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;
    /*
     * your code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */

    if(ino <= 0 || size < 0){
        return IOERR;
    }

    std::string buf;   
    if (ec->get(ino, buf) != extent_protocol::OK) {
        return IOERR;
    }
    size_t bufsize = buf.size();
    if(size > bufsize)
        buf.resize(size, '\0'); 
    else 
        buf.resize(size);
    if (ec->put(ino, buf) != extent_protocol::OK) {

        return IOERR;
    }

    return r;
}

int 
yfs_client::writedir(inum dir, std::list<dirent> &entries) // Write the directory entry table.
{
    int r = OK;

    if (!isdir(dir))
        return IOERR;

    std::ostringstream ost;
    for (std::list<dirent>::iterator it = entries.begin(); it != entries.end(); it++) {
        ost << it->name;
        ost.put('\0');
        ost << it->inum;
    }

    if (ec->put(dir, ost.str()) != extent_protocol::OK) {
        printf("writedir: fail to write directory\n");
        return IOERR;
    }

    return r;
}


int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    bool found = false;
    inum old_inum;
    lookup(parent, name, found, old_inum);
    if (found) {
        return EXIST;
    }

    if (ec->create(extent_protocol::T_FILE, ino_out) != extent_protocol::OK) {
        printf("create: fail to create file\n");
        return IOERR;
    }

    std::list<dirent> entries;
    if (readdir(parent, entries) != OK) {
        return IOERR;
    }
    dirent entry;
    entry.name = name;
    entry.inum = ino_out;
    entries.push_back(entry);
    if (writedir(parent, entries) != OK) {
        return IOERR;
    }
    return r;
}

bool yfs_client::add_entry_and_save(inum parent, const char *name, inum inum) {
    std::list<dirent> entries;

    if (readdir(parent, entries) != OK) {
        printf("add_entry_and_save: fail to read directory entires\n");
        return false;
    }

    dirent entry;
    entry.name = name;
    entry.inum = inum;
    entries.push_back(entry);

    if (writedir(parent, entries) != OK) {
        printf("add_entry_and_save: fail to write directory entires\n");
        return false;
    }

    return true;
}

int yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum& ino_out) {
    // on exist, return EXIST
    if (has_duplicate(parent, name)) {
        return EXIST;
    }

    // create file first
    if (ec->create(extent_protocol::T_DIR, ino_out) != extent_protocol::OK) {
        printf("create: fail to create directory\n");
        return IOERR;
    }

    // write back
    if (add_entry_and_save(parent, name, ino_out) == false) {
        return IOERR;
    }

    return OK;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */

    if (!isdir(parent)) return IOERR;

    if (!name) return IOERR;

    std::list<dirent> entries;
    found = false;
    if(readdir(parent, entries) != OK){
        printf("lookup: fail to read entries\n");
        return IOERR;
    }

    for(std::list<dirent>::iterator it = entries.begin(); it != entries.end(); it++){
        if(it->name == name){
            found = true;
            ino_out = it->inum;
            break;
        }
    }   

    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */

    std::string buf;
    if(ec->get(dir, buf) != extent_protocol::OK){
        printf("readdir: fail to read directory\n");
        return IOERR;
    }

    list.clear();
    std::istringstream ist(buf);
    dirent entry;
    while(std::getline(ist, entry.name, '\0')){
        ist >> entry.inum;
        list.push_back(entry);
    }

    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your code goes here.
     * note: read using ec->get().
     */

    if(ino <= 0 || off < 0 || size < 0 ){
        printf("read: parameter out of range\n");
        return IOERR;
    }
    extent_protocol::attr a;
    if (ec->getattr(ino, a) != extent_protocol::OK) {
        printf("read: error getting attr\n");
        return IOERR;
    }

    std::string buf;    
    if (ec->get(ino, buf) != extent_protocol::OK) {
        printf("read: fail to read file\n");
        return IOERR;
    }
    data = buf.substr(off, size);

    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */

    if(ino <= 0 || size < 0 || off < 0) return IOERR;

    std::string buf;
    if(ec->get(ino, buf) != extent_protocol::OK) {
        printf("write: fail to read file\n");
        return IOERR;
    }
    int bufsize = buf.size();
    if(bufsize < off) {
        buf.resize(off,'\0');   
        buf.append(data, size);
    } else {
        if(bufsize < off + (int)size) {
            buf.resize(off);
            buf.append(data,size);
        } else
            buf.replace(off, size, std::string(data,size));     
    }
    bytes_written = size;
    ec->put(ino, buf);
    return r;
}

int yfs_client::unlink(inum parent, const char *name) {
    printf("unlink: try to unlink %s from parent %llu\n", name, parent);

    // invalid inode number
    if (parent <= 0) {
        printf("unlink: invalid inode number %llu\n", parent);
        return IOERR;
    }

    // read entries
    std::list<dirent> entries;

    if (readdir(parent, entries) != OK) {
        printf("unlink: fail to read directory\n");
        return IOERR;
    }

    // locate target inode number
    std::list<dirent>::iterator it;

    for (it = entries.begin(); it != entries.end(); ++it) {
        if (it->name == name) {
            break;
        }
    }

    if (it == entries.end()) {
        printf("unlink: no such file or directory %s\n", name);
        return IOERR;
    }

    if (!isfile(it->inum)) {
        printf("unlink: %s is not a file\n", name);
        return IOERR;
    }

    // remove file and entry in directory
    if (ec->remove(it->inum) != extent_protocol::OK) {
        printf("unlink: fail to remove file %s\n", name);
        return IOERR;
    }

    entries.erase(it);

    if (writedir(parent, entries) != OK) {
        printf("unlink: fail to write directory entires\n");
        return IOERR;
    }

    return OK;
}

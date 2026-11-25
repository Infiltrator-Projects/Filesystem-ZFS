/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <infiltratr/filesystem_module_v2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>
#include <time.h>
typedef struct{int fd;uint32_t sector;uint64_t size;unsigned writes,live;} Media;
static int rd(void *c,uint64_t at,void *p,uint32_t n)
{
    Media *m=c;if(at%m->sector||n%m->sector||n>m->sector||
        at>m->size||n>m->size-at)return -1;
    return pread(m->fd,p,n,(off_t)at)==(ssize_t)n?0:-1;
}
static int wr(void *c,uint64_t at,const void *p,uint32_t n)
{
    Media *m=c;if(at%m->sector||n%m->sector||n>m->sector||
        at>m->size||n>m->size-at)return -1;
    ++m->writes;return pwrite(m->fd,p,n,(off_t)at)==(ssize_t)n?0:-1;
}
static int fl(void *c){return fsync(((Media*)c)->fd);}
static int cancelled(void *c){(void)c;return 1;}
static void *al(void *c,size_t n){Media*m=c;void*p=malloc(n);if(p)++m->live;return p;}
static void re(void*c,void*p){Media*m=c;if(p)--m->live;free(p);}
static int rng(void*c,void*p,uint32_t n){(void)c;return getrandom(p,n,0)==(ssize_t)n?0:-1;}
static uint64_t clock_ns(void*c){(void)c;struct timespec t;clock_gettime(CLOCK_REALTIME,&t);return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ZFS format check line %d: %s\n",__LINE__,#x);goto done;}}while(0)
int main(int argc,char**argv)
{
    if(argc!=4)return 2;
    void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib)return 2;
    void *sym=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);IfsFsModuleApiEntryV2 entry=NULL;
    if(!sym||sizeof(sym)!=sizeof(entry))return 2;
    memcpy(&entry,&sym,sizeof(entry));
    const IfsFsModuleApiV2*api=entry();
    if(!ifs_fs_api_v2_valid(api,"zfs"))return 2;
    const IfsFsCoreOpsV2*core=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2*vol=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
    if(!core||!vol||!vol->format)return 2;
    Media m={.fd=open(argv[2],O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600),
             .sector=(uint32_t)strtoul(argv[3],NULL,10),.size=128ull*1024u*1024u};
    if(m.fd<0)return 2;
    int result=1;
    CHECK(ftruncate(m.fd,(off_t)m.size)==0);
    IfsFsHostRuntimeV2 h={.abi_version=IFS_FS_HOST_ABI_V2,.structure_bytes=sizeof(h),
        .capabilities=IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE|IFS_FS_HOST_CLOCK,
        .volume_bytes=m.size,.logical_sector_bytes=m.sector,.physical_sector_bytes=m.sector,
        .maximum_transfer_bytes=m.sector,.context=&m,.read_at=rd,.write_at=wr,.flush=fl,
        .allocate=al,.release=re,.random_bytes=rng,.realtime_ns=clock_ns,.monotonic_ns=clock_ns};
    IfsFsHostRuntimeV2 ro=h;
    ro.capabilities=IFS_FS_HOST_READ_ONLY|IFS_FS_HOST_CLOCK;ro.write_at=NULL;ro.flush=NULL;
    CHECK(vol->format(&ro,1,"nativezfs",0)==IFS_FS_ACCESS);
    CHECK(vol->format(&h,2,"nativezfs",0)==IFS_FS_UNSUPPORTED);
    CHECK(vol->format(&h,1,"invalid/name",0)==IFS_FS_INVALID);
    CHECK(!m.writes);
    int rc=vol->format(&h,1,"nativezfs",0);
    if(rc!=IFS_FS_OK)fprintf(stderr,"native formatter returned %d\n",rc);
    CHECK(rc==IFS_FS_OK && !m.live);
    uint32_t v=0;CHECK(core->probe(&ro,&v)==IFS_FS_OK && v==1u && !m.live);
    const unsigned prior_writes=m.writes;
    h.capabilities|=IFS_FS_HOST_CANCEL;h.cancelled=cancelled;
    CHECK(vol->format(&h,1,"replacement",0)==IFS_FS_BUSY);
    CHECK(m.writes==prior_writes && !m.live);
    CHECK(core->probe(&ro,&v)==IFS_FS_OK && v==1u);
    puts("Native ZFS format and MOS/dataset verification PASS");result=0;
done:
    close(m.fd);dlclose(lib);if(result)unlink(argv[2]);return result;
}

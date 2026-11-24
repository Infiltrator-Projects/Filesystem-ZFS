/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "zfs_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
typedef struct {int fd;uint32_t sector;unsigned live;int fail_mos;} Media;
static int rd(void *c,uint64_t off,void *p,uint32_t n)
{
    Media *m=c;
    if(off%m->sector || n%m->sector || n>m->sector)return -1;
    if(m->fail_mos && off>=4ull*1024u*1024u)return -1;
    return pread(m->fd,p,n,(off_t)off)==(ssize_t)n?0:-1;
}
static void *alloc(void *c,size_t n)
{Media *m=c;void *p=malloc(n);if(p)++m->live;return p;}
static void rel(void *c,void *p)
{Media *m=c;if(p)--m->live;free(p);}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    Media m={.fd=open(argv[1],O_RDONLY)};
    struct stat st;
    if(m.fd<0 || fstat(m.fd,&st))return 2;
    for(m.sector=512u;m.sector<=4096u;m.sector*=8u) {
        IfsFsHostRuntimeV2 h={
            .abi_version=IFS_FS_HOST_ABI_V2,.structure_bytes=IFS_FS_HOST_V2_BASE_BYTES,
            .capabilities=IFS_FS_HOST_READ_ONLY,.volume_bytes=(uint64_t)st.st_size,
            .logical_sector_bytes=m.sector,.physical_sector_bytes=m.sector,
            .maximum_transfer_bytes=m.sector,.context=&m,.read_at=rd,.allocate=alloc,.release=rel
        };
        uint8_t *mos=NULL;uint32_t bytes=0;LdZfsByteOrder order=0;
        int rc=zfs_read_mos_host_v2(&h,&mos,&bytes,&order);
        if(rc!=IFS_FS_OK || !mos || !bytes || m.live!=1u) {
            fprintf(stderr,"MOS root failed: rc=%d sector=%u bytes=%u live=%u\n",rc,m.sector,bytes,m.live);
            return 1;
        }
        rel(&m,mos);
        m.fail_mos=1;
        rc=zfs_read_mos_host_v2(&h,&mos,&bytes,&order);
        if(rc==IFS_FS_OK || mos || bytes || m.live)return 1;
        m.fail_mos=0;
        printf("Verified MOS root and I/O rejection at %u-byte sectors\n",m.sector);
    }
    close(m.fd);return 0;
}

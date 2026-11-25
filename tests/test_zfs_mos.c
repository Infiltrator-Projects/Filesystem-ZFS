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
            LdZfsSummary summary;
            const int sr=zfs_read_summary_host_v2(&h,&summary);
            if(sr==IFS_FS_OK)fprintf(stderr,"Topology: type=%s direct=%d config=%d metaslab_vdevs=%u ashift=%llu shift=%llu asize=%llu root_vdev=%llu top_id=%llu checksum=%u compression=%u root_type=%u\n",
                summary.top_vdev_type,summary.single_leaf_supported,summary.config_known,
                summary.metaslab_vdevs,(unsigned long long)summary.ashift,
                (unsigned long long)summary.metaslab_shift,(unsigned long long)summary.top_vdev_asize,
                (unsigned long long)summary.root_vdev,(unsigned long long)summary.top_vdev_id,
                summary.root_checksum,summary.root_compression,summary.root_type);
            fprintf(stderr,"MOS root failed: rc=%d sector=%u bytes=%u live=%u\n",rc,m.sector,bytes,m.live);
            return 1;
        }
        rel(&m,mos);
        uint8_t dnode[512];
        rc=zfs_read_mos_dnode_host_v2(&h,1u,dnode,&order);
        if(rc!=IFS_FS_OK || dnode[0]!=1u || m.live) {
            fprintf(stderr,"MOS pool directory dnode failed: rc=%d type=%u live=%u\n",rc,dnode[0],m.live);
            return 1;
        }
        puts("Verified pool directory dnode through checksummed metadnode tree");
        uint64_t dataset=0;
        rc=zfs_read_root_dataset_host_v2(&h,&dataset,&mos,&bytes,&order);
        if(rc!=IFS_FS_OK || !dataset || !mos || m.live!=1u) {
            fprintf(stderr,"Root dataset failed: rc=%d object=%llu live=%u\n",
                rc,(unsigned long long)dataset,m.live);
            return 1;
        }
        rel(&m,mos);mos=NULL;
        printf("Verified root dataset object %llu and its filesystem objset\n",
               (unsigned long long)dataset);
        m.fail_mos=1;
        rc=zfs_read_mos_host_v2(&h,&mos,&bytes,&order);
        if(rc==IFS_FS_OK || mos || bytes || m.live)return 1;
        m.fail_mos=0;
        printf("Verified MOS root and I/O rejection at %u-byte sectors\n",m.sector);
    }
    close(m.fd);return 0;
}

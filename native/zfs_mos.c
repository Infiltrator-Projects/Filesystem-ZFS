/* SPDX-License-Identifier: GPL-3.0-or-later
 * Common V2 MOS bootstrap reader. The bounded compression decoders are
 * retained from this repository's historical read-only analysis engine.
 * No path, descriptor, kernel mount, or external process enters this layer.
 * This is an internal prerequisite to creating a complete ZFS pool;
 * successful label recognition alone is not evidence of an importable MOS.
 */
#include "zfs_native.h"
#include "zfs_hash.h"
#include <infiltratr/endian.h>
#include <string.h>

static uint64_t word64(const uint8_t *p,LdZfsByteOrder order)
{return order==LD_ZFS_BYTE_ORDER_LITTLE?infiltratr_load_le64(p):infiltratr_load_be64(p);}
static uint32_t word32(const uint8_t *p,LdZfsByteOrder order)
{return order==LD_ZFS_BYTE_ORDER_LITTLE?infiltratr_load_le32(p):infiltratr_load_be32(p);}
static uint16_t word16(const uint8_t *p,LdZfsByteOrder order)
{return order==LD_ZFS_BYTE_ORDER_LITTLE?infiltratr_load_le16(p):infiltratr_load_be16(p);}
static int decompress_zle(const uint8_t *source, size_t source_length,
                          uint8_t *destination, size_t destination_length)
{
    const size_t literal_limit = 64U;
    size_t source_offset = 0U;
    size_t destination_offset = 0U;

    while (source_offset < source_length &&
           destination_offset < destination_length) {
        const size_t encoded = (size_t)source[source_offset++] + 1U;
        if (encoded <= literal_limit) {
            if (encoded > source_length - source_offset ||
                encoded > destination_length - destination_offset)
                return -1;
            memcpy(destination + destination_offset,
                   source + source_offset, encoded);
            source_offset += encoded;
            destination_offset += encoded;
        } else {
            const size_t zero_count = encoded - literal_limit;
            if (zero_count > destination_length - destination_offset)
                return -1;
            memset(destination + destination_offset, 0, zero_count);
            destination_offset += zero_count;
        }
    }
    return destination_offset == destination_length ? 0 : -1;
}

static int decompress_lzjb(const uint8_t *source, size_t source_length,
                           uint8_t *destination, size_t destination_length)
{
    size_t source_offset = 0U;
    size_t destination_offset = 0U;
    uint8_t copy_map = 0U;
    unsigned int copy_mask = 1U << 7U;

    while (destination_offset < destination_length) {
        copy_mask <<= 1U;
        if (copy_mask == (1U << 8U)) {
            copy_mask = 1U;
            if (source_offset >= source_length)
                return -1;
            copy_map = source[source_offset++];
        }

        if ((copy_map & copy_mask) != 0U) {
            if (source_length - source_offset < 2U)
                return -1;
            const unsigned int match_length =
                (unsigned int)(source[source_offset] >> 2U) + 3U;
            const unsigned int match_offset =
                (((unsigned int)source[source_offset] << 8U) |
                 source[source_offset + 1U]) & 0x3ffU;
            source_offset += 2U;
            if (match_offset == 0U || match_offset > destination_offset)
                return -1;
            size_t copy_from = destination_offset - match_offset;
            for (unsigned int index = 0U;
                 index < match_length &&
                 destination_offset < destination_length;
                 ++index)
                destination[destination_offset++] =
                    destination[copy_from++];
        } else {
            if (source_offset >= source_length)
                return -1;
            destination[destination_offset++] = source[source_offset++];
        }
    }
    return 0;
}

static int lz4_extended_length(const uint8_t *source, size_t source_length,
                               size_t *source_offset, size_t *length)
{
    if (*length != 15U)
        return 0;
    for (;;) {
        if (*source_offset >= source_length)
            return -1;
        const uint8_t value = source[(*source_offset)++];
        if (*length > SIZE_MAX - value)
            return -1;
        *length += value;
        if (value != 255U)
            return 0;
    }
}

static int decompress_lz4_raw(const uint8_t *source, size_t source_length,
                              uint8_t *destination,
                              size_t destination_length)
{
    size_t source_offset = 0U;
    size_t destination_offset = 0U;

    while (source_offset < source_length) {
        const uint8_t token = source[source_offset++];
        size_t literal_length = (size_t)(token >> 4U);
        if (lz4_extended_length(source, source_length, &source_offset,
                                &literal_length) != 0)
            return -1;
        if (literal_length > source_length - source_offset ||
            literal_length > destination_length - destination_offset)
            return -1;
        memcpy(destination + destination_offset,
               source + source_offset, literal_length);
        source_offset += literal_length;
        destination_offset += literal_length;

        if (source_offset == source_length)
            return destination_offset == destination_length ? 0 : -1;
        if (source_length - source_offset < 2U)
            return -1;

        const size_t match_offset =
            (size_t)source[source_offset] |
            ((size_t)source[source_offset + 1U] << 8U);
        source_offset += 2U;
        if (match_offset == 0U || match_offset > destination_offset)
            return -1;

        size_t match_length = (size_t)(token & 0x0fU);
        if (lz4_extended_length(source, source_length, &source_offset,
                                &match_length) != 0 ||
            match_length > SIZE_MAX - 4U)
            return -1;
        match_length += 4U;
        if (match_length > destination_length - destination_offset)
            return -1;

        size_t copy_from = destination_offset - match_offset;
        for (size_t index = 0U; index < match_length; ++index)
            destination[destination_offset++] = destination[copy_from++];
    }

    return destination_offset == destination_length ? 0 : -1;
}

static int decompress_lz4(const uint8_t *source, size_t source_length,
                          uint8_t *destination, size_t destination_length)
{
    if (source_length < 4U)
        return -1;
    const uint32_t compressed_length =
        ((uint32_t)source[0] << 24U) |
        ((uint32_t)source[1] << 16U) |
        ((uint32_t)source[2] << 8U) |
        (uint32_t)source[3];
    if ((uint64_t)compressed_length + 4U > source_length)
        return -1;
    return decompress_lz4_raw(source + 4U, compressed_length,
                              destination, destination_length);
}


static int check_block(const uint8_t *raw,uint32_t size,uint32_t checksum,
                       LdZfsByteOrder order,const uint64_t expected[4])
{
    uint64_t sums[4]={0};
    if(checksum==6u) {
        for(uint32_t i=0;i<size;i+=16u) {
            sums[0]+=word64(raw+i,order);sums[1]+=word64(raw+i+8,order);
            sums[2]+=sums[0];sums[3]+=sums[1];
        }
    } else if(checksum==7u) {
        for(uint32_t i=0;i<size;i+=4u) {
            sums[0]+=word32(raw+i,order);sums[1]+=sums[0];
            sums[2]+=sums[1];sums[3]+=sums[2];
        }
    } else if(checksum==8u) {
        uint8_t hash[32];
        zfs_sha256(raw,size,NULL,0u,hash);
        for(unsigned i=0;i<4u;++i)sums[i]=infiltratr_load_be64(hash+i*8u);
    } else return IFS_FS_UNSUPPORTED;
    for(unsigned i=0;i<4u;++i)if(sums[i]!=expected[i])return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}

/* Read the newest selected transaction's MOS root using all three DVAs.
 * DVA addresses are relative to the allocatable vdev, not the partition.
 * A valid checksum is required before decompressing or interpreting data.
 * The returned buffer is allocated through the host and owned by the caller.
 * Unsupported compression, encryption and gang blocks fail closed. */
int zfs_read_mos_host_v2(const IfsFsHostRuntimeV2 *h,uint8_t **out,
                         uint32_t *length,LdZfsByteOrder *data_order)
{
    if(!out || !length || !data_order)return IFS_FS_INVALID;
    *out=NULL;*length=0u;*data_order=0;
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    LdZfsSummary s;
    int rc=zfs_read_summary_host_v2(h,&s);
    if(rc!=IFS_FS_OK)return rc;
    if(!s.single_leaf_supported)return IFS_FS_UNSUPPORTED;
    uint8_t ub[1024];
    rc=ifs_fs_host_read_bytes_v2(h,s.uberblock_magic_offset,ub,sizeof(ub));
    if(rc!=IFS_FS_OK)return rc;
    if(word64(ub,s.byte_order)!=UINT64_C(0xbab10c) ||
       word64(ub+16,s.byte_order)!=s.uberblock_txg)return IFS_FS_CORRUPT;
    const uint8_t *bp=ub+40u;
    const uint64_t prop=word64(bp+48u,s.byte_order);
    if(((prop>>39u)&1u) || ((prop>>61u)&3u))return IFS_FS_UNSUPPORTED;
    if(((prop>>48u)&255u)!=11u || ((prop>>56u)&31u))return IFS_FS_CORRUPT;
    const uint32_t lsize=(uint32_t)(((prop&65535u)+1u)<<9u);
    const uint32_t psize=(uint32_t)((((prop>>16u)&65535u)+1u)<<9u);
    const uint32_t compression=(uint32_t)((prop>>32u)&127u);
    const uint32_t checksum=(uint32_t)((prop>>40u)&255u);
    const LdZfsByteOrder order=(prop>>63u)?LD_ZFS_BYTE_ORDER_LITTLE:LD_ZFS_BYTE_ORDER_BIG;
    /* MOS objsets are small. Cap resource usage independently of a
     * malicious on-media block pointer's maximum representable size. */
    if(lsize<1024u || lsize>65536u || psize>lsize)return IFS_FS_CORRUPT;
    if(compression!=2u && compression!=3u && compression!=14u && compression!=15u)
        return IFS_FS_UNSUPPORTED;
    if(checksum!=6u && checksum!=7u && checksum!=8u)return IFS_FS_UNSUPPORTED;
    if(compression==2u && psize!=lsize)return IFS_FS_CORRUPT;
    const uint64_t birth=word64(bp+80u,s.byte_order);
    if(!birth || birth>s.uberblock_txg)return IFS_FS_CORRUPT;
    uint64_t expected[4];
    for(unsigned i=0;i<4u;++i)expected[i]=word64(bp+96u+i*8u,s.byte_order);
    uint8_t *physical=h->allocate(h->context,psize);
    uint8_t *logical=h->allocate(h->context,lsize);
    if(!physical || !logical) {
        if(physical)h->release(h->context,physical);
        if(logical)h->release(h->context,logical);
        return IFS_FS_NO_MEMORY;
    }
    const uint64_t start=UINT64_C(4)<<20;
    int last=IFS_FS_CORRUPT;
    for(unsigned i=0;i<3u;++i) {
        if(h->cancelled && h->cancelled(h->context)){last=IFS_FS_BUSY;break;}
        const uint64_t d0=word64(bp+i*16u,s.byte_order);
        const uint64_t d1=word64(bp+i*16u+8u,s.byte_order);
        const uint64_t asize=(d0&UINT64_C(0xffffff))<<9u;
        const uint64_t vdev=(d0>>32u)&UINT64_C(0xffffff);
        if(!asize || vdev!=s.top_vdev_id)continue;
        if(d1>>63u){last=IFS_FS_UNSUPPORTED;continue;}
        if(d1>(UINT64_MAX-start)>>9u)continue;
        const uint64_t offset=d1<<9u,at=start+offset;
        if(asize<psize || offset>s.top_vdev_asize ||
           asize>s.top_vdev_asize-offset || at>h->volume_bytes ||
           psize>h->volume_bytes-at)continue;
        rc=ifs_fs_host_read_bytes_v2(h,at,physical,psize);
        if(rc!=IFS_FS_OK){last=rc;continue;}
        rc=check_block(physical,psize,checksum,order,expected);
        if(rc!=IFS_FS_OK){last=rc;continue;}
        int d=0;
        if(compression==2u)memcpy(logical,physical,lsize);
        else if(compression==3u)d=decompress_lzjb(physical,psize,logical,lsize);
        else if(compression==14u)d=decompress_zle(physical,psize,logical,lsize);
        else d=decompress_lz4(physical,psize,logical,lsize);
        if(d){last=IFS_FS_CORRUPT;continue;}
        /* The MOS object set must have a bounded, ordinary metadnode
         * and DMU_OST_META (type 1) at the objset type field. */
        if(logical[0]!=10u || logical[2]<1u || logical[2]>12u ||
           !logical[3] || logical[3]>3u || logical[12] ||
           !word16(logical+8u,order) ||
           64u+128u*logical[3]+word16(logical+10u,order)>512u ||
           word64(logical+704u,order)!=1u) {
            last=IFS_FS_CORRUPT;continue;
        }
        *out=logical;*length=lsize;*data_order=order;last=IFS_FS_OK;
        break;
    }
    h->release(h->context,physical);
    if(last!=IFS_FS_OK)h->release(h->context,logical);
    return last;
}

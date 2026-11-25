/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native single-leaf ZFS pool creation, legacy SPA/ZPL version 1 profile.
 * Disk encodings are based on the published OpenZFS on-media structures
 * (dnode, dsl_dir, dsl_dataset, space_map, nvpair, vdev and uberblock).
 * No OpenZFS library or external command is used by this implementation.
 * Construct the entire initial transaction in memory before invalidating
 * any existing labels. All writes use the exclusive Common V2 host lease.
 * This intentionally excludes RAID, encryption, feature flags and snapshots.
 */
#include "zfs_native.h"
#include "zfs_hash.h"
#include <infiltratr/endian.h>
#include <stdatomic.h>
#include <string.h>

enum { BLK=4096, LABEL=262144, CONFIG=114688, START=4194304,
       PLAN=1048576, MS_SHIFT=24, TXG=4, MOS_TABLE=16384 };
typedef struct {
    uint8_t *data;
    uint32_t used;
} Plan;
static atomic_flag create_gate=ATOMIC_FLAG_INIT;
static void le16(uint8_t *p,uint16_t x)
{p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static void le32(uint8_t *p,uint32_t x)
{infiltratr_store_le32(p,x);}
static void le64(uint8_t *p,uint64_t x)
{infiltratr_store_le64(p,x);}
static uint64_t get64(const uint8_t *p)
{return infiltratr_load_le64(p);}

/* Packed XDR nvlist encoder. Lengths include both encoded and native
 * decoded sizes. Strings are padded to four bytes on wire, eight in
 * the decoded nvpair. Nested lists carry their own version/flags. */
typedef struct {uint8_t *p;size_t cap,pos;int bad;} Nv;
static void nv_bytes(Nv *n,const void *p,size_t bytes)
{
    if(n->bad || bytes>n->cap-n->pos){n->bad=1;return;}
    memcpy(n->p+n->pos,p,bytes);n->pos+=bytes;
}
static void nv32(Nv *n,uint32_t x)
{uint8_t b[4];infiltratr_store_be32(b,x);nv_bytes(n,b,4);}
static void nv64(Nv *n,uint64_t x)
{uint8_t b[8];infiltratr_store_be64(b,x);nv_bytes(n,b,8);}
static void nv_str(Nv *n,const char *s)
{
    const size_t len=strlen(s);const uint8_t zero[3]={0};
    if(len>UINT32_MAX){n->bad=1;return;}
    nv32(n,(uint32_t)len);nv_bytes(n,s,len);
    nv_bytes(n,zero,(4u-len%4u)%4u);
}
static size_t pair_start(Nv *n,const char *key,uint32_t type,uint32_t count)
{
    size_t at=n->pos;nv32(n,0);nv32(n,0);nv_str(n,key);
    nv32(n,type);nv32(n,count);return at;
}
static void pair_end(Nv *n,size_t at,const char *key,size_t native_bytes)
{
    if(n->bad)return;
    const size_t decoded=((16u+strlen(key)+1u+7u)&~7u)+
                         ((native_bytes+7u)&~7u);
    if(n->pos-at>UINT32_MAX || decoded>UINT32_MAX){n->bad=1;return;}
    infiltratr_store_be32(n->p+at,(uint32_t)(n->pos-at));
    infiltratr_store_be32(n->p+at+4u,(uint32_t)decoded);
}
static void nv_num(Nv *n,const char *k,uint64_t x)
{size_t at=pair_start(n,k,8,1);nv64(n,x);pair_end(n,at,k,8);}
static void nv_text(Nv *n,const char *k,const char *v)
{size_t at=pair_start(n,k,9,1);nv_str(n,v);pair_end(n,at,k,strlen(v)+1);}
static void nv_begin(Nv *n){nv32(n,0);nv32(n,1);}
static void nv_end(Nv *n){nv32(n,0);nv32(n,0);}
static void leaf_config(Nv *n,uint64_t leaf,uint64_t asize)
{
    nv_begin(n);nv_text(n,"type","disk");nv_num(n,"id",0);
    nv_num(n,"guid",leaf);nv_text(n,"path","/dev/infiltrator-zfs");
    nv_num(n,"whole_disk",0);nv_num(n,"metaslab_array",13);
    nv_num(n,"metaslab_shift",MS_SHIFT);nv_num(n,"ashift",12);
    nv_num(n,"asize",asize);nv_num(n,"is_log",0);
    nv_num(n,"create_txg",TXG);nv_end(n);
}
static size_t pool_config(uint8_t *out,size_t cap,const char *name,
                          uint64_t pool,uint64_t leaf,uint64_t asize,int mos)
{
    Nv n={.p=out,.cap=cap};
    const uint8_t hdr[4]={1,1,0,0};nv_bytes(&n,hdr,4);
    nv_begin(&n);nv_num(&n,"version",1);nv_text(&n,"name",name);
    nv_num(&n,"state",1);nv_num(&n,"txg",TXG);nv_num(&n,"pool_guid",pool);
    nv_num(&n,"vdev_children",1);nv_text(&n,"hostname","InfiltratorOS");
    if(!mos){nv_num(&n,"top_guid",leaf);nv_num(&n,"guid",leaf);}
    const size_t at=pair_start(&n,"vdev_tree",19,1);
    if(mos) {
        nv_begin(&n);nv_text(&n,"type","root");nv_num(&n,"id",0);
        nv_num(&n,"guid",pool);
        size_t child=pair_start(&n,"children",20,1);
        leaf_config(&n,leaf,asize);pair_end(&n,child,"children",32);
        nv_end(&n);
    } else leaf_config(&n,leaf,asize);
    pair_end(&n,at,"vdev_tree",24);nv_end(&n);
    return n.bad?0:n.pos;
}

/* Block pointer addresses are relative to the vdev allocation origin.
 * Every initial metadata block has two physically distinct copies; the
 * MOS root has three. Use Fletcher-4, little endian, no compression. */
static int put_block(Plan *p,const uint8_t *data,uint32_t bytes,
                     unsigned type,unsigned copies,uint8_t bp[128])
{
    if(!bytes || bytes%BLK || copies<1 || copies>3 ||
       bytes>(PLAN-p->used)/copies)return IFS_FS_NO_SPACE;
    memset(bp,0,128);
    for(unsigned i=0;i<copies;++i) {
        le64(bp+16u*i,bytes/512u);le64(bp+16u*i+8,p->used/512u);
        memcpy(p->data+p->used,data,bytes);p->used+=bytes;
    }
    const uint64_t sectors=bytes/512u-1u;
    le64(bp+48,sectors|(sectors<<16)|(UINT64_C(2)<<32)|
        (UINT64_C(7)<<40)|((uint64_t)type<<48)|(UINT64_C(1)<<63));
    le64(bp+80,TXG);le64(bp+88,1);
    uint64_t a=0,b=0,c=0,d=0;
    for(uint32_t i=0;i<bytes;i+=4) {
        a+=infiltratr_load_le32(data+i);b+=a;c+=b;d+=c;
    }
    le64(bp+96,a);le64(bp+104,b);le64(bp+112,c);le64(bp+120,d);
    return IFS_FS_OK;
}
static uint64_t bp_used(const uint8_t bp[128])
{
    uint64_t used=0;
    for(unsigned i=0;i<3;++i)used+=(get64(bp+16u*i)&UINT64_C(0xffffff))*512u;
    return used;
}
/* Ordinary one-slot dnode, direct tree, one block pointer and up to
 * 320 bonus bytes. Empty bonus-only objects deliberately have a hole. */
static void dnode(uint8_t *dn,unsigned type,unsigned bonus_type,
                  unsigned bonus_len,uint32_t blocksize,const uint8_t *bp)
{
    memset(dn,0,512);dn[0]=(uint8_t)type;dn[1]=14;dn[2]=1;
    dn[3]=1;dn[4]=(uint8_t)bonus_type;dn[5]=7;dn[6]=2;dn[7]=1;
    le16(dn+8,(uint16_t)(blocksize/512u));le16(dn+10,(uint16_t)bonus_len);
    if(bp){memcpy(dn+64,bp,128);le64(dn+24,bp_used(bp));}
}
static void zap_begin(uint8_t *p,uint64_t salt)
{memset(p,0,BLK);le64(p,UINT64_C(0x8000000000000003));le64(p+8,salt);}
static int zap_add(uint8_t *p,const char *name,uint64_t value)
{
    size_t n=strlen(name);if(!n || n>=50)return IFS_FS_INVALID;
    unsigned free_slot=0;
    for(unsigned i=64;i<BLK;i+=64) {
        if(!p[i+14]){if(!free_slot)free_slot=i;continue;}
        if(!memcmp(p+i+14,name,n+1))return IFS_FS_INVALID;
    }
    if(!free_slot)return IFS_FS_NO_SPACE;
    le64(p+free_slot,value);memcpy(p+free_slot+14,name,n+1);
    return IFS_FS_OK;
}
static int zap_object(Plan *p,uint8_t *dn,unsigned type,uint8_t *raw)
{
    uint8_t bp[128];int rc=put_block(p,raw,BLK,type,2,bp);
    if(rc==IFS_FS_OK)dnode(dn,type,0,0,BLK,bp);
    return rc;
}
static void dsl_dir(uint8_t *dn,uint64_t now,uint64_t head,uint64_t parent,
                    uint64_t children,uint64_t props,uint64_t used)
{
    dnode(dn,12,12,256,BLK,NULL);uint8_t *v=dn+192;
    le64(v,now);le64(v+8,head);le64(v+16,parent);
    le64(v+32,children);le64(v+40,used);
    le64(v+48,used);le64(v+56,used);le64(v+80,props);
}
/* Build dataset and MOS in dependency order. Reserve the space-map
 * blocks' own allocations, the final dnode table and all root copies
 * before recording the single contiguous first-metaslab allocation. */
static int build_transaction(Plan *p,const char *name,uint64_t pool,
    uint64_t leaf,uint64_t asize,uint64_t now,uint64_t fsid,uint64_t dataset_guid,
    uint8_t rootbp[128])
{
    uint8_t raw[BLK],bp[128],dataset_bp[128],table[BLK]={0};
    uint8_t mos[MOS_TABLE]={0};int rc;
#define TRY(x) do {rc=(x);if(rc!=IFS_FS_OK)return rc;}while(0)
#define DN(i) (mos+512u*(i))
#define ZAP(i,type) TRY(zap_object(p,DN(i),type,raw))
    /* ZPL master node, delete queue, and root directory znode. */
    zap_begin(raw,fsid);
    TRY(zap_add(raw,"VERSION",1));TRY(zap_add(raw,"ROOT",3));
    TRY(zap_add(raw,"DELETE_QUEUE",2));
    TRY(zap_object(p,table+512,21,raw));
    zap_begin(raw,fsid+1);TRY(zap_object(p,table+1024,22,raw));
    zap_begin(raw,fsid+2);TRY(put_block(p,raw,BLK,20,2,bp));
    dnode(table+1536,20,17,264,BLK,bp);
    uint8_t *z=table+1536+192;
    for(unsigned i=0;i<4;++i)le64(z+16u*i,now);
    le64(z+64,1);le64(z+72,0040755);le64(z+80,2);
    le64(z+88,3);le64(z+96,2);
    /* Legacy inline ACL: owner, owning group, everyone. */
    le32(z+184,3);
    const uint16_t flags[]={0x1000,0x2040,0x4000};
    for(unsigned i=0;i<3;++i) {
        uint8_t *ace=z+192+12u*i;
        le32(ace,UINT32_MAX);le32(ace+4,i?0x1200a9:0x1f01ff);
        le16(ace+8,flags[i]);
    }
    TRY(put_block(p,table,BLK,10,2,bp));
    memset(raw,0,BLK);dnode(raw,10,0,0,BLK,bp);le64(raw+704,2);
    TRY(put_block(p,raw,BLK,11,2,dataset_bp));
    const uint64_t dataset_used=p->used;
    /* MOS pool directory and packed trusted topology. */
    zap_begin(raw,pool);
    TRY(zap_add(raw,"config",2));TRY(zap_add(raw,"root_dataset",3));
    TRY(zap_add(raw,"sync_bplist",12));TRY(zap_add(raw,"pool_props",15));
    ZAP(1,1);
    memset(raw,0,BLK);
    size_t n=pool_config(raw,BLK,name,pool,leaf,asize,1);
    if(!n)return IFS_FS_NO_SPACE;
    TRY(put_block(p,raw,BLK,3,2,bp));dnode(DN(2),3,4,8,BLK,bp);
    le64(DN(2)+192,n);
    zap_begin(raw,pool+1);ZAP(5,15);
    zap_begin(raw,pool+2);TRY(zap_add(raw,"$MOS",4));ZAP(6,13);
    zap_begin(raw,pool+3);ZAP(7,15);
    zap_begin(raw,pool+4);ZAP(8,13);
    zap_begin(raw,pool+5);ZAP(10,14);
    dnode(DN(11),5,6,16,131072,NULL);
    dnode(DN(12),5,6,16,16384,NULL);
    zap_begin(raw,pool+6);ZAP(15,31);
    /* Head dataset with a fully materialised ZPL object set. */
    dnode(DN(9),16,16,320,BLK,NULL);
    uint8_t *ds=DN(9)+192;
    le64(ds,3);le64(ds+32,10);le64(ds+48,now);le64(ds+56,1);
    le64(ds+64,11);le64(ds+72,dataset_used);
    le64(ds+80,dataset_used/2);le64(ds+88,dataset_used/2);
    le64(ds+104,fsid&UINT64_C(0x00ffffffffffffff));le64(ds+112,dataset_guid);
    memcpy(ds+128,dataset_bp,128);
    memset(raw,0,BLK);le64(raw,14);
    TRY(put_block(p,raw,BLK,2,2,bp));dnode(DN(13),2,0,0,BLK,bp);
    const uint64_t final_used=p->used+2u*BLK+2u*MOS_TABLE+3u*BLK;
    if(final_used>PLAN || final_used>(UINT64_C(1)<<MS_SHIFT) ||
       final_used/BLK>32768u)return IFS_FS_NO_SPACE;
    memset(raw,0,BLK);
    /* A single legacy SM_ALLOC record: run length minus one, at offset
     * zero. The allocated bytes include the space map and final roots. */
    le64(raw,final_used/BLK-1u);
    TRY(put_block(p,raw,BLK,8,2,bp));dnode(DN(14),8,7,24,BLK,bp);
    /* Legacy space maps have a 24-byte bonus in the conventional
     * three-pointer dnode, yielding a 64-byte bonus capacity. A
     * one-pointer dnode has 320 bytes of bonus capacity and OpenZFS
     * interprets that as a feature-enabled histogram space map. */
    DN(14)[3]=3u;
    le64(DN(14)+448,14);le64(DN(14)+456,8);le64(DN(14)+464,final_used);
    dsl_dir(DN(3),now,9,0,6,5,final_used);
    dsl_dir(DN(4),now,0,3,8,7,final_used-dataset_used);
    TRY(put_block(p,mos,MOS_TABLE,10,2,bp));
    memset(raw,0,BLK);dnode(raw,10,0,0,MOS_TABLE,bp);le64(raw+704,1);
    TRY(put_block(p,raw,BLK,11,3,rootbp));
    return p->used==final_used?IFS_FS_OK:IFS_FS_CORRUPT;
#undef ZAP
#undef DN
#undef TRY
}
static void seal(uint8_t *p,uint32_t bytes,uint64_t physical)
{
    uint8_t verifier[32]={0},hash[32];le64(verifier,physical);
    le64(p+bytes-40,UINT64_C(0x0210da7ab10c7a11));
    zfs_sha256(p,bytes-32,verifier,sizeof(verifier),hash);
    for(unsigned i=0;i<4;++i)
        le64(p+bytes-32+8u*i,infiltratr_load_be64(hash+8u*i));
}
static int write_at(const IfsFsHostRuntimeV2 *h,uint64_t at,const void *p,uint32_t n)
{
    if(h->cancelled && h->cancelled(h->context))return IFS_FS_BUSY;
    return ifs_fs_host_write_bytes_v2(h,at,p,n);
}
static int valid_name(const char *s)
{
    size_t n=strlen(s);if(!n || n>63 ||
       !((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')))return 0;
    if(!strcmp(s,"log")||!strcmp(s,"mirror")||!strncmp(s,"raidz",5)||
       !strncmp(s,"draid",5)||!strcmp(s,"spare")||
       (*s=='c'&&s[1]>='0'&&s[1]<='9'))return 0;
    for(size_t i=0;i<n;++i) {
        unsigned char c=(unsigned char)s[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':'))return 0;
    }
    return 1;
}
static int create_pool(const IfsFsHostRuntimeV2 *h,const char *name)
{
    const uint64_t total=h->volume_bytes,asize=total-START-2u*LABEL;
    const uint64_t offsets[]={0,LABEL,total-2u*LABEL,total-LABEL};
    uint64_t ids[4];
    int rc=ifs_fs_host_random_bytes_v2(h,ids,sizeof(ids));
    if(rc!=IFS_FS_OK)return rc;
    for(unsigned i=0;i<4;++i) {
        if(!ids[i])return IFS_FS_IO_ERROR;
        for(unsigned j=0;j<i;++j)if(ids[i]==ids[j])return IFS_FS_IO_ERROR;
    }
    uint64_t now=h->realtime_ns?h->realtime_ns(h->context)/1000000000u:0;
    uint8_t *plan=h->allocate(h->context,PLAN);
    uint8_t *label=h->allocate(h->context,LABEL);
    if(!plan || !label) {
        if(plan)h->release(h->context,plan);
        if(label)h->release(h->context,label);
        return IFS_FS_NO_MEMORY;
    }
    Plan p={.data=plan};uint8_t root[128];
    rc=build_transaction(&p,name,ids[0],ids[1],asize,now,ids[2],ids[3],root);
    if(rc!=IFS_FS_OK)goto done;
    memset(label,0,LABEL);
    /* Validate both XDR configurations before the first destructive write. */
    if(!pool_config(label+16384,CONFIG-40,name,ids[0],ids[1],asize,0)) {
        rc=IFS_FS_NO_SPACE;goto done;
    }
    memset(label,0,LABEL);
    /* A cancellation before the first destructive I/O must not enter
     * the cleanup path, which intentionally revokes all pool labels.
     * Once a write is attempted, even a reported failure may be partial. */
    if(h->cancelled && h->cancelled(h->context)){rc=IFS_FS_BUSY;goto done;}
    rc=ifs_fs_host_write_bytes_v2(h,offsets[0],label,LABEL);
    if(rc!=IFS_FS_OK)goto failed;
    for(unsigned i=1;i<4;++i) {
        rc=write_at(h,offsets[i],label,LABEL);if(rc!=IFS_FS_OK)goto failed;
    }
    if(h->flush(h->context)){rc=IFS_FS_IO_ERROR;goto failed;}
    rc=write_at(h,START,plan,p.used);if(rc!=IFS_FS_OK)goto failed;
    if(h->flush(h->context)){rc=IFS_FS_IO_ERROR;goto failed;}
    /* Publish the checksummed config and a committed uberblock in each
     * independent label. The slot index matches TXG modulo the ring size.
     * On a reported failure revoke all four identities best-effort. */
    for(unsigned k=0;k<4;++k) {
        const unsigned i=(k+2u)%4u;
        memset(label,0,LABEL);
        (void)pool_config(label+16384,CONFIG-40,name,ids[0],ids[1],asize,0);
        seal(label+16384,CONFIG,offsets[i]+16384u);
        const uint32_t slot=131072u+(TXG%32u)*BLK;
        uint8_t *u=label+slot;
        le64(u,UINT64_C(0xbab10c));le64(u+8,1);le64(u+16,TXG);
        le64(u+24,ids[0]+ids[1]);le64(u+32,now);memcpy(u+40,root,128);
        le64(u+168,1);seal(u,BLK,offsets[i]+slot);
        rc=write_at(h,offsets[i],label,LABEL);if(rc!=IFS_FS_OK)goto failed;
        if(h->flush(h->context)){rc=IFS_FS_IO_ERROR;goto failed;}
    }
    /* Verify the newly created dataset through the independent native
     * read path; qualification separately imports it with OpenZFS zdb. */
    uint8_t *objset=NULL;uint32_t bytes=0;uint64_t dataset=0;
    LdZfsByteOrder order=0;
    rc=zfs_read_root_dataset_host_v2(h,&dataset,&objset,&bytes,&order);
    if(objset)h->release(h->context,objset);
    if(rc!=IFS_FS_OK || dataset!=9u || order!=LD_ZFS_BYTE_ORDER_LITTLE) {
        if(rc==IFS_FS_OK)rc=IFS_FS_CORRUPT;
        goto failed;
    }
    goto done;
failed:
    memset(label,0,LABEL);
    for(unsigned i=0;i<4;++i)(void)ifs_fs_host_write_bytes_v2(h,offsets[i],label,LABEL);
    (void)h->flush(h->context);
done:
    h->release(h->context,label);h->release(h->context,plan);return rc;
}
int zfs_format_host_v2(const IfsFsHostRuntimeV2 *h,uint32_t variant,
                       const char *name,uint32_t flags)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY) ||
       !(h->capabilities&IFS_FS_HOST_EXCLUSIVE) ||
       !(h->capabilities&IFS_FS_HOST_DURABLE))return IFS_FS_ACCESS;
    if(variant!=1u || flags || h->logical_sector_bytes>BLK ||
       h->volume_bytes%LABEL || h->volume_bytes<128ull*1024u*1024u ||
       h->volume_bytes>4ull*1024u*1024u*1024u)return IFS_FS_UNSUPPORTED;
    if(!name || !*name)name="infiltrator";
    if(!valid_name(name))return IFS_FS_INVALID;
    if(atomic_flag_test_and_set_explicit(&create_gate,memory_order_acquire))return IFS_FS_BUSY;
    int rc=create_pool(h,name);
    atomic_flag_clear_explicit(&create_gate,memory_order_release);
    return rc;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "zfs_native.h"
#include "zfs_hash.h"
#include <infiltratr/endian.h>
#include <string.h>

static void copy_name(char *out, size_t capacity, const char *value)
{
    size_t n = 0;
    while (n + 1 < capacity && value[n]) {
        out[n] = value[n];
        ++n;
    }
    out[n] = 0;
}

/*
 * OpenZFS leaf-vdev label geometry.  These constants intentionally describe
 * only the stable on-disk label/uberblock envelope that Defragmenter needs.
 * OpenZFS source is a corroborating reference; production parsing remains
 * first-party and does not link libzfs/libzpool.
 */
#define ZFS_VDEV_PAD_SIZE (8U << 10)
#define ZFS_VDEV_PHYS_SIZE (112U << 10)
#define ZFS_UBERBLOCK_RING_SIZE (128U << 10)
#define ZFS_UBERBLOCK_SIZE (1U << 10)
#define ZFS_VDEV_LABEL_SIZE (ZFS_VDEV_PAD_SIZE * 2U + ZFS_VDEV_PHYS_SIZE + ZFS_UBERBLOCK_RING_SIZE)
#define ZFS_VDEV_LABELS 4U
#define ZFS_VDEV_PHYS_OFFSET (ZFS_VDEV_PAD_SIZE * 2U)
#define ZFS_UBERBLOCK_RING_OFFSET (ZFS_VDEV_PAD_SIZE * 2U + ZFS_VDEV_PHYS_SIZE)
#define ZFS_NVLIST_BYTES ZFS_VDEV_PHYS_SIZE
#define ZFS_UBERBLOCK_MAGIC UINT64_C(0x00bab10c)
#define ZFS_SPA_VERSION_FEATURES UINT64_C(5000)

static uint64_t label_offset(uint64_t psize, uint32_t label);

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t position;
} ZfsXdr;

typedef struct {
    bool has_id;
    bool has_guid;
    bool has_ashift;
    bool has_metaslab_array;
    bool has_metaslab_shift;
    bool has_asize;
    uint64_t id;
    uint64_t guid;
    uint64_t ashift;
    uint64_t metaslab_array;
    uint64_t metaslab_shift;
    uint64_t asize;
    char type[16];
} ZfsVdevFields;

static uint32_t xdr_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static uint64_t xdr_be64(const uint8_t *data)
{
    return ((uint64_t)xdr_be32(data) << 32U) |
           (uint64_t)xdr_be32(data + 4U);
}

static bool xdr_take(ZfsXdr *xdr, size_t bytes, const uint8_t **out)
{
    if (bytes > xdr->length - xdr->position)
        return false;
    *out = xdr->data + xdr->position;
    xdr->position += bytes;
    return true;
}

static bool xdr_u32(ZfsXdr *xdr, uint32_t *value)
{
    const uint8_t *data = NULL;
    if (!xdr_take(xdr, 4U, &data))
        return false;
    *value = xdr_be32(data);
    return true;
}

static bool xdr_u64(ZfsXdr *xdr, uint64_t *value)
{
    const uint8_t *data = NULL;
    if (!xdr_take(xdr, 8U, &data))
        return false;
    *value = xdr_be64(data);
    return true;
}

static bool xdr_string(ZfsXdr *xdr, char *destination, size_t capacity)
{
    uint32_t length = 0U;
    if (!xdr_u32(xdr, &length) || length > xdr->length - xdr->position)
        return false;
    const size_t padded = ((size_t)length + 3U) & ~(size_t)3U;
    const uint8_t *data = NULL;
    if (padded > xdr->length - xdr->position ||
        !xdr_take(xdr, padded, &data))
        return false;
    if (destination != NULL && capacity != 0U) {
        size_t copy = length;
        if (copy != 0U && data[copy - 1U] == 0U)
            copy--;
        if (copy >= capacity)
            copy = capacity - 1U;
        memcpy(destination, data, copy);
        destination[copy] = '\0';
    }
    return true;
}

static bool zfs_parse_nvlist(ZfsXdr *xdr, unsigned depth,
                             bool feature_scope,
                             LdZfsSummary *summary);

static bool supported_mos_feature(const char *name)
{
    /*
     * These features can alter MOS-visible structures, but the bounded reader
     * either already understands the changed form or independently refuses the
     * affected construct (embedded/gang blocks) when it is encountered.
     *
     * Device-removal and RAIDZ-family MOS features are deliberately absent:
     * their DVA/topology translation rules are outside the single-leaf reader.
     */
    return strcmp(name, "com.delphix:hole_birth") == 0 ||
           strcmp(name, "com.delphix:embedded_data") == 0 ||
           strcmp(name, "com.klarasystems:vdev_zaps_v2") == 0 ||
           strcmp(name, "com.klarasystems:dynamic_gang_header") == 0;
}

static void record_vdev_fields(const ZfsVdevFields *fields,
                               LdZfsSummary *summary)
{
    if (!fields->has_metaslab_array)
        return;
    if (summary->metaslab_vdevs != UINT32_MAX)
        summary->metaslab_vdevs++;
    if (summary->metaslab_vdevs != 1U)
        return;

    summary->top_vdev_id = fields->has_id ? fields->id : UINT64_MAX;
    summary->ashift = fields->has_ashift ? fields->ashift : 0U;
    summary->metaslab_array = fields->metaslab_array;
    summary->metaslab_shift =
        fields->has_metaslab_shift ? fields->metaslab_shift : 0U;
    summary->top_vdev_asize = fields->has_asize ? fields->asize : 0U;
    if (fields->type[0] != '\0')
        copy_name(summary->top_vdev_type, sizeof(summary->top_vdev_type), fields->type);
}

static bool zfs_parse_nvpair(ZfsXdr *xdr, unsigned depth,
                             bool feature_scope,
                             LdZfsSummary *summary, ZfsVdevFields *fields)
{
    const size_t pair_start = xdr->position;
    uint32_t encoded = 0U;
    uint32_t decoded = 0U;
    if (!xdr_u32(xdr, &encoded) || !xdr_u32(xdr, &decoded))
        return false;
    if (encoded == 0U && decoded == 0U)
        return true;
    if (encoded < 20U || encoded > xdr->length - pair_start)
        return false;
    const size_t pair_end = pair_start + encoded;

    char name[64];
    uint32_t type = 0U;
    uint32_t elements = 0U;
    if (!xdr_string(xdr, name, sizeof(name)) ||
        !xdr_u32(xdr, &type) || !xdr_u32(xdr, &elements) ||
        xdr->position > pair_end)
        return false;

    if (feature_scope && type == 1U && elements == 0U) {
        summary->mos_features_present = true;
        if (summary->mos_feature_count != UINT32_MAX)
            summary->mos_feature_count++;
        if (!supported_mos_feature(name)) {
            summary->mos_features_supported = false;
            if (summary->unsupported_mos_feature[0] == '\0')
                copy_name(summary->unsupported_mos_feature, sizeof(summary->unsupported_mos_feature), name);
        }
    } else if (type == 8U && elements == 1U) {
        uint64_t value = 0U;
        if (!xdr_u64(xdr, &value))
            return false;
        if (depth == 0U) {
            if (strcmp(name, "pool_guid") == 0)
                summary->pool_guid = value;
            else if (strcmp(name, "guid") == 0)
                summary->leaf_guid = value;
            else if (strcmp(name, "top_guid") == 0)
                summary->top_guid = value;
        }
        if (strcmp(name, "id") == 0) {
            fields->has_id = true;
            fields->id = value;
        } else if (strcmp(name, "guid") == 0) {
            fields->has_guid = true;
            fields->guid = value;
        } else if (strcmp(name, "ashift") == 0) {
            fields->has_ashift = true;
            fields->ashift = value;
        } else if (strcmp(name, "metaslab_array") == 0) {
            fields->has_metaslab_array = true;
            fields->metaslab_array = value;
        } else if (strcmp(name, "metaslab_shift") == 0) {
            fields->has_metaslab_shift = true;
            fields->metaslab_shift = value;
        } else if (strcmp(name, "asize") == 0) {
            fields->has_asize = true;
            fields->asize = value;
        }
    } else if (type == 9U && elements == 1U) {
        char value[32];
        if (!xdr_string(xdr, value, sizeof(value)))
            return false;
        if (strcmp(name, "type") == 0)
            copy_name(fields->type, sizeof(fields->type), value);
    } else if (type == 19U && elements == 1U) {
        const bool child_feature_scope =
            feature_scope || strcmp(name, "features_for_read") == 0;
        if (depth >= 8U ||
            !zfs_parse_nvlist(xdr, depth + 1U,
                              child_feature_scope, summary))
            return false;
    } else if (type == 20U) {
        if (depth >= 8U || elements > 256U)
            return false;
        for (uint32_t index = 0U; index < elements; ++index)
            if (!zfs_parse_nvlist(xdr, depth + 1U,
                                  feature_scope, summary))
                return false;
    }

    if (xdr->position > pair_end)
        return false;
    xdr->position = pair_end;
    return true;
}

static bool zfs_parse_nvlist(ZfsXdr *xdr, unsigned depth,
                             bool feature_scope,
                             LdZfsSummary *summary)
{
    uint32_t version = 0U;
    uint32_t flags = 0U;
    if (!xdr_u32(xdr, &version) || !xdr_u32(xdr, &flags) ||
        version != 0U)
        return false;
    (void)flags;

    ZfsVdevFields fields;
    memset(&fields, 0, sizeof(fields));
    for (;;) {
        const size_t before = xdr->position;
        uint32_t encoded = 0U;
        uint32_t decoded = 0U;
        if (before > xdr->length - 8U)
            return false;
        encoded = xdr_be32(xdr->data + before);
        decoded = xdr_be32(xdr->data + before + 4U);
        if (encoded == 0U && decoded == 0U) {
            xdr->position += 8U;
            break;
        }
        if (!zfs_parse_nvpair(xdr, depth, feature_scope,
                              summary, &fields))
            return false;
    }
    record_vdev_fields(&fields, summary);
    return true;
}

/* Every vdev label and uberblock carries a location-bound SHA-256 trailer.
 * The block's endianness determines the word order of both the offset
 * verifier and the four stored digest words. Hash through the trailer magic,
 * then hash the verifier in place of the checksum. Media remains immutable. */
#define ZFS_ECK_MAGIC UINT64_C(0x0210da7ab10c7a11)
#define ZFS_ECK_BYTES 40u
static int verify_label_checksum(const uint8_t *data, size_t bytes,
                                 uint64_t offset, LdZfsByteOrder *order)
{
    const uint8_t *tail = data + bytes - ZFS_ECK_BYTES;
    const bool little = infiltratr_load_le64(tail) == ZFS_ECK_MAGIC;
    if (!little && infiltratr_load_be64(tail) != ZFS_ECK_MAGIC)
        return IFS_FS_NOT_FOUND;
    *order = little ? LD_ZFS_BYTE_ORDER_LITTLE : LD_ZFS_BYTE_ORDER_BIG;
    uint8_t verifier[32] = {0}, digest[32];
    if (little) infiltratr_store_le64(verifier, offset);
    else infiltratr_store_be64(verifier, offset);
    zfs_sha256(data, bytes - sizeof(verifier), verifier, sizeof(verifier), digest);
    for (size_t i = 0; i < sizeof(verifier); i += 8) {
        const uint8_t *word = tail + 8 + i;
        uint64_t expected = little ? infiltratr_load_le64(word) : infiltratr_load_be64(word);
        if (expected != infiltratr_load_be64(digest + i)) return IFS_FS_CORRUPT;
    }
    return IFS_FS_OK;
}

static int parse_label_config(const IfsFsHostRuntimeV2 *host, uint64_t psize,
                              LdZfsSummary *summary, uint8_t *config)
{
    const uint64_t offset = label_offset(psize, summary->label_index) + ZFS_VDEV_PHYS_OFFSET;
    int status = ifs_fs_host_read_bytes_v2(host, offset, config, ZFS_NVLIST_BYTES);
    if (status != IFS_FS_OK) return status;
    LdZfsByteOrder order;
    status = verify_label_checksum(config, ZFS_NVLIST_BYTES, offset, &order);
    if (status != IFS_FS_OK) return status;
    if (config[0] != 1U) return IFS_FS_UNSUPPORTED;
    if (config[1] > 1U || config[2] || config[3]) return IFS_FS_CORRUPT;
    ZfsXdr xdr = {.data = config, .length = ZFS_NVLIST_BYTES - ZFS_ECK_BYTES, .position = 4U};
    summary->mos_features_supported = true;
    if (!zfs_parse_nvlist(&xdr, 0U, false, summary) ||
        !summary->pool_guid || !summary->leaf_guid)
        return IFS_FS_CORRUPT;
    summary->config_known = summary->top_guid != 0U && summary->metaslab_vdevs != 0U;
    return IFS_FS_OK;
}

static uint64_t load_u64(const uint8_t *data, LdZfsByteOrder byte_order)
{
    return byte_order == LD_ZFS_BYTE_ORDER_LITTLE
        ? infiltratr_load_le64(data)
        : infiltratr_load_be64(data);
}

static bool decode_uberblock(const uint8_t bytes[ZFS_UBERBLOCK_SIZE],
                             LdZfsByteOrder *byte_order,
                             uint64_t *version, uint64_t *txg,
                             uint64_t *guid_sum, uint64_t *timestamp)
{
    const uint64_t little_magic = infiltratr_load_le64(bytes);
    const uint64_t big_magic = infiltratr_load_be64(bytes);
    if (little_magic == ZFS_UBERBLOCK_MAGIC)
        *byte_order = LD_ZFS_BYTE_ORDER_LITTLE;
    else if (big_magic == ZFS_UBERBLOCK_MAGIC)
        *byte_order = LD_ZFS_BYTE_ORDER_BIG;
    else
        return false;

    *version = load_u64(bytes + 8U, *byte_order);
    *txg = load_u64(bytes + 16U, *byte_order);
    *guid_sum = load_u64(bytes + 24U, *byte_order);
    *timestamp = load_u64(bytes + 32U, *byte_order);

    /*
     * A bare magic word is not enough to identify a member.  Real committed
     * uberblocks carry a non-zero transaction group and a recognised SPA
     * version. The supported-version policy follows location-bound verification.
     */
    return *version != 0U && *txg != 0U;
}

static uint64_t label_offset(uint64_t psize, uint32_t label)
{
    const uint64_t ordinal = (uint64_t)label * ZFS_VDEV_LABEL_SIZE;
    return label < (ZFS_VDEV_LABELS / 2U)
        ? ordinal
        : psize - (uint64_t)ZFS_VDEV_LABELS * ZFS_VDEV_LABEL_SIZE + ordinal;
}

static void decode_root_block_pointer(const uint8_t *uberblock,
                                      LdZfsByteOrder byte_order,
                                      LdZfsSummary *summary)
{
    const uint8_t *root = uberblock + 40U;
    const uint64_t dva_word0 = load_u64(root + 0U, byte_order);
    const uint64_t dva_word1 = load_u64(root + 8U, byte_order);
    const uint64_t prop = load_u64(root + 48U, byte_order);

    summary->root_asize = (dva_word0 & UINT64_C(0xffffff)) << 9U;
    summary->root_vdev = (dva_word0 >> 32U) & UINT64_C(0xffffff);
    summary->root_offset =
        (dva_word1 & UINT64_C(0x7fffffffffffffff)) << 9U;
    summary->root_embedded = ((prop >> 39U) & 1U) != 0U;
    if (!summary->root_embedded) {
        summary->root_lsize =
            (((prop >> 0U) & UINT64_C(0xffff)) + 1U) << 9U;
        summary->root_psize =
            (((prop >> 16U) & UINT64_C(0xffff)) + 1U) << 9U;
    }
    summary->root_compression = (uint32_t)((prop >> 32U) & UINT64_C(0x7f));
    summary->root_checksum = (uint32_t)((prop >> 40U) & UINT64_C(0xff));
    summary->root_type = (uint32_t)((prop >> 48U) & UINT64_C(0xff));
    summary->root_level = (uint32_t)((prop >> 56U) & UINT64_C(0x1f));
    summary->root_logical_birth = load_u64(root + 80U, byte_order);
}

static bool better_uberblock(uint64_t txg, uint64_t timestamp,
                             const LdZfsSummary *summary)
{
    if (summary->candidate_uberblocks == 0U)
        return true;
    if (txg != summary->uberblock_txg)
        return txg > summary->uberblock_txg;
    return timestamp > summary->uberblock_timestamp;
}

static int scan_labels(const IfsFsHostRuntimeV2 *host, uint64_t psize,
                       LdZfsSummary *summary, uint8_t *scratch)
{
    /* OpenZFS clamps a ring slot to 1..8 KiB, using the top vdev's ashift.
     * Reading one 8 KiB window handles all four sizes on 512-byte or 4Kn hosts. */
    const uint32_t window_bytes = 8192;
    uint32_t candidates = 0;
    int no_candidate = IFS_FS_NOT_FOUND;
    for (uint32_t label = 0; label < ZFS_VDEV_LABELS; ++label) {
        LdZfsSummary config = {.size_bytes = summary->size_bytes, .label_index = label};
        int config_status = parse_label_config(host, psize, &config, scratch);
        if (config_status == IFS_FS_IO_ERROR || config_status == IFS_FS_NO_MEMORY)
            return config_status;
        if (config_status == IFS_FS_CORRUPT || config_status == IFS_FS_UNSUPPORTED)
            no_candidate = config_status;
        const uint64_t ring = label_offset(psize, label) + ZFS_UBERBLOCK_RING_OFFSET;
        uint32_t shift = config.ashift < 10u ? 10u : config.ashift > 13u ? 13u : (uint32_t)config.ashift;
        uint32_t stride = 1u << shift;
        for (uint32_t relative = 0; relative < ZFS_UBERBLOCK_RING_SIZE; relative += window_bytes) {
            int read_status = ifs_fs_host_read_bytes_v2(host, ring + relative, scratch, window_bytes);
            if (read_status != IFS_FS_OK) return read_status;
            for (uint32_t pos = 0; pos < window_bytes; pos += stride) {
                const uint8_t *bytes = scratch + pos;
                LdZfsByteOrder order, trailer_order;
                uint64_t version, txg, guid_sum, timestamp;
                if (!decode_uberblock(bytes, &order, &version, &txg, &guid_sum, &timestamp))
                    continue;
                if (config_status != IFS_FS_OK) {
                    if (no_candidate == IFS_FS_NOT_FOUND) no_candidate = IFS_FS_CORRUPT;
                    continue;
                }
                uint64_t offset = ring + relative + pos;
                int status = verify_label_checksum(bytes, stride, offset, &trailer_order);
                if (status == IFS_FS_NO_MEMORY || status == IFS_FS_IO_ERROR) return status;
                if (status != IFS_FS_OK || order != trailer_order) {
                    no_candidate = IFS_FS_CORRUPT;
                    continue;
                }
                if (version > 28U && version != ZFS_SPA_VERSION_FEATURES)
                    return IFS_FS_UNSUPPORTED;
                LdZfsSummary candidate = config;
                decode_root_block_pointer(bytes, order, &candidate);
                if (candidate.root_embedded) return IFS_FS_UNSUPPORTED;
                /* A committed MOS root is a non-hole level-zero objset BP.
                 * Probing does not assert that the dataset itself can be read. */
                if (!candidate.root_asize || candidate.root_type != 11u || candidate.root_level ||
                    !candidate.root_logical_birth || candidate.root_logical_birth > txg) {
                    no_candidate = IFS_FS_CORRUPT;
                    continue;
                }
                if (candidates && (config.pool_guid != summary->pool_guid ||
                                   config.leaf_guid != summary->leaf_guid))
                    return IFS_FS_CORRUPT;
                ++candidates;
                if (!better_uberblock(txg, timestamp, summary)) continue;
                *summary = candidate;
                summary->candidate_uberblocks = candidates;
                summary->uberblock_magic_offset = offset;
                summary->uberblock_txg = txg;
                summary->uberblock_version = version;
                summary->uberblock_guid_sum = guid_sum;
                summary->uberblock_timestamp = timestamp;
                summary->uberblock_slot = (relative + pos) / stride;
                summary->byte_order = order;
            }
        }
    }
    if (!candidates) return no_candidate;
    summary->candidate_uberblocks = candidates;
    summary->single_leaf_supported = summary->config_known && summary->metaslab_vdevs == 1U &&
        strcmp(summary->top_vdev_type, "disk") == 0 && summary->root_vdev == summary->top_vdev_id &&
        summary->ashift >= 9U && summary->ashift <= 16U &&
        summary->metaslab_shift >= summary->ashift && summary->metaslab_shift < 63U &&
        summary->top_vdev_asize != 0U && summary->mos_features_supported;
    return IFS_FS_OK;
}

int zfs_read_summary_host_v2(const IfsFsHostRuntimeV2 *host,
                             LdZfsSummary *summary)
{
    uint64_t psize;
    if (!summary || !ifs_fs_host_v2_valid(host))
        return IFS_FS_INVALID;
    memset(summary, 0, sizeof(*summary));
    summary->size_bytes = host->volume_bytes;
    psize = host->volume_bytes -
            (host->volume_bytes % ZFS_VDEV_LABEL_SIZE);
    if (psize < (uint64_t)ZFS_VDEV_LABELS * ZFS_VDEV_LABEL_SIZE)
        return IFS_FS_NOT_FOUND;
    /* The 112 KiB packed nvlist and the later 8 KiB uberblock window have
     * disjoint lifetimes. Keep both off the firmware stack and release the
     * same host allocation on every exit, including I/O and checksum errors. */
    uint8_t *scratch = host->allocate(host->context, ZFS_NVLIST_BYTES);
    if (!scratch) return IFS_FS_NO_MEMORY;
    int status = scan_labels(host, psize, summary, scratch);
    host->release(host->context, scratch);
    return status;
}


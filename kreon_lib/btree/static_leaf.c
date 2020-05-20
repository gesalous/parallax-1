#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include "static_leaf.h"
#include "conf.h"

extern uint32_t leaf_size_per_level[MAX_LEVELS];
extern level_offsets_todata leaf_node_offsets[MAX_LEVELS];

static void retrieve_static_leaf_structures(bt_static_leaf_node const *leaf, struct bt_static_leaf_structs *src)
{
	char *leaf_base_address = (char *)leaf;
	uint8_t level_id = leaf->header.level_id;
	src->bitmap = (bt_leaf_bitmap *)(leaf_base_address + leaf_node_offsets[level_id].bitmap_offset);
	src->slot_array = (bt_leaf_slot_array *)(leaf_base_address + leaf_node_offsets[level_id].slot_array_offset);
	src->kv_entries = (bt_leaf_entry *)(leaf_base_address + leaf_node_offsets[level_id].kv_entries_offset);
}

static uint32_t get_bitmap_size(uint8_t level_id)
{
	return sizeof(bt_leaf_bitmap) * leaf_node_offsets[level_id].bitmap_entries;
}

static uint32_t get_slot_array_size(uint8_t level_id)
{
	return sizeof(bt_leaf_slot_array) * leaf_node_offsets[level_id].slot_array_entries;
}

static uint32_t get_kv_entries_size(uint8_t level_id)
{
	return sizeof(bt_leaf_entry) * leaf_node_offsets[level_id].kv_entries;
}

void init_static_leaf_metadata(bt_static_leaf_node *leaf)
{
	struct bt_static_leaf_structs src;
	uint8_t level_id = leaf->header.level_id;
	retrieve_static_leaf_structures(leaf, &src);
	/* If the bt_leaf_bitmap struct becomes a different type than an unsigned char,
	   the UCHAR_MAX should be replaced with the maximum value the type can hold.  */
	memset(src.bitmap, 0, get_bitmap_size(level_id));
	memset(src.slot_array, 0, get_slot_array_size(level_id));
	memset(src.kv_entries, 0, get_kv_entries_size(level_id));
}

struct bsearch_result binary_search_static_leaf(bt_static_leaf_node const *leaf, struct splice *key_buf)
{
	struct bt_static_leaf_structs src;
	char *leaf_key_prefix, *leaf_key_buf;
	struct bsearch_result result = { 0, INSERT };
	int32_t start = 0, middle = 0, end = leaf->header.numberOfEntriesInNode - 1;
	const int32_t numberOfEntriesInNode = leaf->header.numberOfEntriesInNode;
	const int32_t kv_entries = leaf_node_offsets[leaf->header.level_id].kv_entries;
	uint32_t pos;
	int ret, ret_case;
	retrieve_static_leaf_structures(leaf, &src);

	while (numberOfEntriesInNode > 0) {
		middle = (start + end) / 2;

		if (numberOfEntriesInNode > kv_entries || middle < 0 || middle >= numberOfEntriesInNode) {
			result.status = ERROR;
			return result;
		}

		pos = src.slot_array[middle].index;
		leaf_key_prefix = src.kv_entries[pos].prefix;
		ret = prefix_compare(leaf_key_prefix, key_buf->data, PREFIX_SIZE);
		ret_case = ret < 0 ? LESS_THAN_ZERO : ret > 0 ? GREATER_THAN_ZERO : EQUAL_TO_ZERO;

		if (ret_case == EQUAL_TO_ZERO) {
			leaf_key_buf = (void *)(MAPPED + src.kv_entries[pos].pointer);
			ret = _tucana_key_cmp(leaf_key_buf, key_buf, KV_FORMAT, KV_FORMAT);

			if (ret == 0) {
				result.middle = middle;
				result.status = UPDATE;
				return result;
			}

			ret_case = ret < 0 ? LESS_THAN_ZERO : GREATER_THAN_ZERO;
		}

		switch (ret_case) {
		case LESS_THAN_ZERO:
			start = middle + 1;
			if (start > end) {
				middle++;
				result.middle = middle;
				result.status = INSERT;
				return result;
			}
			continue;
		case GREATER_THAN_ZERO:
			end = middle - 1;
			if (start > end) {
				result.middle = middle;
				result.status = INSERT;
				return result;
			}
			continue;
		}
	}

	return result;
}

void shift_slot_array(bt_static_leaf_node *leaf, uint32_t middle)
{
	struct bt_static_leaf_structs src;
	const size_t num_items = leaf->header.numberOfEntriesInNode - middle;
	retrieve_static_leaf_structures(leaf, &src);

	if (num_items == 0)
		return;

	memmove(&src.slot_array[middle + 1], &src.slot_array[middle], num_items * sizeof(bt_leaf_slot_array));
}
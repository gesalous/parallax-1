#include <assert.h>
#include <stdio.h>
#include <alloca.h>
#include <string.h>
#include <stdlib.h>
#include "../build/external-deps/log/src/log.h"
//#include "../kreon_lib/btree/btree.h"
#include "../kreon_rdma_client/kreon_rdma_client.h"
#include "../kreon_server/create_regions_utils.h"

#define NUM_KEYS 1000000
#define BASE 1000000
#define NUM_REGIONS 16
#define KEY_PREFIX "userakias"
#define KV_SIZE 1024

#define SCAN_SIZE 50
#define PREFETCH_ENTRIES 16
#define PREFETCH_MEM_SIZE (32 * 1024)
#define ZOOKEEPER "192.168.1.134:2181"
#define HOST "tie4.cluster.ics.forth.gr-8080"

extern ZooLogLevel logLevel;
typedef struct key {
	uint32_t key_size;
	char key_buf[];
} key;

typedef struct value {
	uint32_t value_size;
	char value_buf[];
} value;

int main(int argc, char *argv[])
{
	uint32_t region_id = 0;
	uint64_t range = NUM_KEYS / NUM_REGIONS;
	uint64_t min_key, max_key;
	uint32_t error_code;
	if (argc > 1 && strcmp(argv[1], "--create_regions") == 0) {
		log_info("Creating %d regions", NUM_REGIONS);

		char *args_buf[14];
		args_buf[1] = strdup("-c");
		/*static fields*/
		args_buf[8] = "--size";
		args_buf[9] = "1000000";

		args_buf[10] = "--host";
		args_buf[11] = strdup(HOST);
		args_buf[12] = "--zookeeper";
		args_buf[13] = strdup(ZOOKEEPER);

		args_buf[4] = strdup("--minkey");
		args_buf[5] = malloc(16);
		args_buf[6] = strdup("--maxkey");
		args_buf[7] = malloc(16);
		/*dynamic fields*/
		for (region_id = 0; region_id < NUM_REGIONS - 1; region_id++) {
			min_key = BASE + (region_id * range);
			max_key = min_key + range;
			args_buf[2] = strdup("--region");
			args_buf[3] = (char *)malloc(16);
			sprintf(args_buf[3], "%u", region_id);
			if (region_id == 0)
				sprintf(args_buf[5], "%s", "0000000");
			else
				sprintf(args_buf[5], "userakias%lu", min_key);
			sprintf(args_buf[7], "userakias%lu", max_key);
			create_region(13, args_buf);
			log_info("Created region id %s minkey %s maxkey %s", args_buf[2], args_buf[4], args_buf[6]);
		}
		/*last region*/
		min_key = BASE + (region_id * range);
		sprintf(args_buf[3], "%u", region_id);
		sprintf(args_buf[5], "userakias%lu", min_key);
		sprintf(args_buf[7], "+oo");
		create_region(13, args_buf);
		log_info("Created region id %s minkey %s maxkey %s", args_buf[2], args_buf[4], args_buf[6]);
	}
	logLevel = ZOO_LOG_LEVEL_INFO;

	if (krc_init(ZOOKEEPER, 2181) != KRC_SUCCESS) {
		log_fatal("Failed to init library");
		exit(EXIT_FAILURE);
	}

	uint64_t i = 0;
	uint64_t j = 0;
	krc_scanner *sc;
	key *k = (key *)malloc(KV_SIZE);
	log_info("Starting population for %lu keys...", NUM_KEYS);
	for (i = BASE; i < (BASE + NUM_KEYS); i++) {
		strncpy(k->key_buf, KEY_PREFIX, strlen(KEY_PREFIX));
		if (i % 100000 == 0)
			log_info("inserted up to %llu th key", i);

		sprintf(k->key_buf + strlen(KEY_PREFIX), "%llu", (long long unsigned)i);
		k->key_size = strlen(k->key_buf) + 1;
		value *v = (value *)((uint64_t)k + sizeof(key) + k->key_size);
		v->value_size = KV_SIZE - ((2 * sizeof(key)) + k->key_size);
		memset(v->value_buf, 0xDD, v->value_size);
		krc_put(k->key_size, k->key_buf, v->value_size, v->value_buf);
	}
	log_info("Population ended, testing gets");
	for (i = BASE; i < (BASE + NUM_KEYS); i++) {
		strncpy(k->key_buf, KEY_PREFIX, strlen(KEY_PREFIX));
		if (i % 100000 == 0)
			log_info("looked up to %llu th key", i);

		sprintf(k->key_buf + strlen(KEY_PREFIX), "%llu", (long long unsigned)i);
		k->key_size = strlen(k->key_buf) + 1;
		value *v = (value *)((uint64_t)k + sizeof(key) + k->key_size);
		v->value_size = KV_SIZE - ((2 * sizeof(key)) + k->key_size);
		memset(v->value_buf, 0xDD, v->value_size);
		krc_value *val = krc_get(k->key_size, k->key_buf, 2 * 1024, &error_code);
		if (error_code != KRC_SUCCESS) {
			log_fatal("key %s not found test failed!");
			exit(EXIT_FAILURE);
		}
		free(val);
	}

	log_info("Gets successful ended, testing small scans....");

	for (i = BASE; i < (BASE + (NUM_KEYS - SCAN_SIZE)); i++) {
		if (i % 10000 == 0)
			log_info("<Scan no %llu>", i);

		sprintf(k->key_buf + strlen(KEY_PREFIX), "%llu", (long long unsigned)i);
		strncpy(k->key_buf, KEY_PREFIX, strlen(KEY_PREFIX));
		sprintf(k->key_buf + strlen(KEY_PREFIX), "%llu", (long long unsigned)i);
		k->key_size = strlen(k->key_buf) + 1;

		sc = krc_scan_init(PREFETCH_ENTRIES, PREFETCH_MEM_SIZE);
		krc_scan_set_start(sc, k->key_size, k->key_buf,KRC_GREATER_OR_EQUAL);
		krc_scan_get_next(sc);
		if (!krc_scan_is_valid(sc)) {
			log_fatal("Test failed key %s invalid scanner (it shoulddn't!)", k->key_buf);
			exit(EXIT_FAILURE);
		}
		if (k->key_size != sc->curr_key->key_size ||
		    memcmp(k->key_buf, sc->curr_key->key_buf, k->key_size) != 0) {
			log_fatal("Test failed key %s not found scanner instead returned %d:%s", k->key_buf,
				  sc->curr_key->key_size, sc->curr_key->key_buf);
			assert(0);
			exit(EXIT_FAILURE);
		}

		for (j = 1; j <= SCAN_SIZE; j++) {
			/*construct the key we expect*/
			strncpy(k->key_buf, KEY_PREFIX, strlen(KEY_PREFIX));
			sprintf(k->key_buf + strlen(KEY_PREFIX), "%llu", (long long unsigned)i + j);
			k->key_size = strlen(k->key_buf) + 1;
			//log_info("Expecting key %s",k->key_buf);
			krc_scan_get_next(sc);
			if (!krc_scan_is_valid(sc)) {
				log_fatal("Test failed key %s not found scanner reason scan invalid!(it shouldn't)",
					  k->key_buf);
				assert(0);
				exit(EXIT_FAILURE);
			}
			if (k->key_size != sc->curr_key->key_size ||
			    memcmp(k->key_buf, sc->curr_key->key_buf, k->key_size) != 0) {
				log_fatal("Test failed key %s not found scanner instead returned %d:%s", k->key_buf,
					  sc->curr_key->key_size, sc->curr_key->key_buf);
				exit(EXIT_FAILURE);
			}
		}

		if (i % 10000 == 0)
			log_info("</Scan no %llu>", i);
		krc_scan_close(sc);
	}
	log_info("small scan test Successfull");

	log_info("Running a full scan");

	sc = krc_scan_init(PREFETCH_ENTRIES, PREFETCH_MEM_SIZE);
	krc_scan_set_start(sc, 7, "0000000",KRC_GREATER_OR_EQUAL);
	for (i = BASE; i < (BASE + NUM_KEYS); i++) {
		strncpy(k->key_buf, KEY_PREFIX, strlen(KEY_PREFIX));
		sprintf(k->key_buf + strlen(KEY_PREFIX), "%llu", (long long unsigned)i);
		k->key_size = strlen(k->key_buf) + 1;

		krc_scan_get_next(sc);
		if (!krc_scan_is_valid(sc)) {
			log_fatal("Test failed key %s invalid scanner (it shoulddn't!)", k->key_buf);
			exit(EXIT_FAILURE);
		}
		if (k->key_size != sc->curr_key->key_size ||
		    memcmp(k->key_buf, sc->curr_key->key_buf, k->key_size) != 0) {
			log_fatal("Test failed key %s not found scanner instead returned %d:%s", k->key_buf,
				  sc->curr_key->key_size, sc->curr_key->key_buf);
			exit(EXIT_FAILURE);
		}
	}
	krc_scan_close(sc);
	log_info("full scan test Successfull");
	return 1;
}

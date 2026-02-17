#include "osmem.h"
#include "block_meta.h"
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#define MEM_BOUND 8
#define META_SIZE round_up(sizeof(struct block_meta))
#define LARGE_ALLOC_LIMIT (128 * 1024)

struct block_meta *global_head;
struct block_meta *first_block;
int first_alloc = 1;

int switch_alloc(struct block_meta *block, size_t size);
size_t round_up(size_t size);
struct block_meta *seek_free_block(struct block_meta **previous, size_t size);
void coalesce_free_blocks(void);
void break_block(struct block_meta *block, size_t size);
void init_new_block(struct block_meta **block, struct block_meta *last_block, size_t size, size_t threshold);
void *handle_alloc(size_t size, size_t threshold);

void *os_malloc(size_t size)
{
	if (size == 0)
		return NULL;
	return handle_alloc(size, LARGE_ALLOC_LIMIT);
}

void os_free(void *ptr)
{
	if (!ptr)
		return;

	struct block_meta *block = (struct block_meta *)((char *)ptr - META_SIZE);
	int prev_status = block->status;

	block->status = STATUS_FREE;
	coalesce_free_blocks();

	if (prev_status == STATUS_MAPPED) {
		if (block == global_head)
			first_block = global_head->next;

		int result = munmap(block, block->size + META_SIZE);

		assert(result != -1 && "munmap fail");

		if (block == global_head)
			global_head = NULL;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if (nmemb == 0 || size == 0)
		return NULL;

	size_t full_size = nmemb * size;
	long page_size = sysconf(_SC_PAGE_SIZE);

	assert(page_size != -1 && "sysconf fail");

	void *memory_block = handle_alloc(full_size, page_size);

	assert(memory_block != NULL && "malloc fail");

	memset(memory_block, 0, full_size);
	return memory_block;
}

void *os_realloc(void *ptr, size_t size)
{
	if (!ptr)
		return os_malloc(size);

	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	struct block_meta *block = (struct block_meta *)((char *)ptr - META_SIZE);

	if (block->status == STATUS_FREE)
		return NULL;

	size_t current_size = block->size;
	size_t adjusted_size = round_up(size);

	if (current_size >= adjusted_size) {
		if (!switch_alloc(block, size)) {
			if (current_size - adjusted_size >= round_up(1 + META_SIZE)) {
				break_block(block, adjusted_size);
				block->size = adjusted_size;
			}
			return ptr;
		}
		if (current_size == adjusted_size)
			return ptr;
	} else {
		if (block->next == NULL && block->status == STATUS_ALLOC && adjusted_size < LARGE_ALLOC_LIMIT) {
			size_t additional_size = adjusted_size - current_size;

			sbrk(additional_size);
			block->size = adjusted_size;
			return ptr;
		}
		coalesce_free_blocks();
		struct block_meta *next_block = block->next;

		if (next_block && next_block->status == STATUS_FREE && block->size + next_block->size + META_SIZE >= adjusted_size) {
			block->size += next_block->size + META_SIZE;
			block->next = next_block->next;
			if (block->size >= adjusted_size) {
				break_block(block, adjusted_size);
				return ptr;
			}
		}
	}

	void *new_block = os_malloc(size);

	assert(new_block != NULL && "malloc fail");

	size_t copy_size = current_size < adjusted_size ? current_size : adjusted_size;

	memcpy(new_block, ptr, copy_size);
	os_free(ptr);
	return new_block;
}

size_t round_up(size_t size)
{
	return ((size + MEM_BOUND - 1) / MEM_BOUND) * MEM_BOUND;
}

struct block_meta *seek_free_block(struct block_meta **previous, size_t size)
{
	coalesce_free_blocks();
	struct block_meta *current = first_block;
	struct block_meta *best_fit = NULL;
	size_t min_fit = LONG_MAX;

	while (current != NULL) {
		if (current->status == STATUS_FREE && current->size >= round_up(size)) {
			if (current->size < min_fit) {
				min_fit = current->size;
				best_fit = current;
			}
		}
		*previous = current;
		current = current->next;
	}
	return best_fit;
}

void coalesce_free_blocks(void)
{
	struct block_meta *block = first_block;

	while (block != NULL && block->next != NULL) {
		if (block->status == STATUS_FREE && block->next->status == STATUS_FREE) {
			struct block_meta *next = block->next;

			block->size += next->size + META_SIZE;
			block->next = next->next;

			assert(block != block->next && "block points to itself");
		} else {
			block = block->next;
		}
	}
}

void break_block(struct block_meta *block, size_t size)
{
	size_t adjusted_size = round_up(size + META_SIZE);
	struct block_meta *remaining = (struct block_meta *)((char *)block + adjusted_size);

	remaining->size = block->size - size - META_SIZE;
	remaining->status = STATUS_FREE;
	remaining->next = block->next;
	block->next = remaining;
}

void init_new_block(struct block_meta **block, struct block_meta *last_block, size_t size, size_t threshold)
{
	size_t total_size = round_up(size + META_SIZE);

	if (total_size < threshold) {
		if (first_alloc) {
			(*block) = (struct block_meta *)sbrk(LARGE_ALLOC_LIMIT);
			first_alloc = 0;
		} else {
			(*block) = (struct block_meta *)sbrk(total_size);
		}
		assert(*block != (void *)-1 && "sbrk fail");
		(*block)->status = STATUS_ALLOC;
	} else {
		(*block) = (struct block_meta *)mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		assert(*block != MAP_FAILED && "mmap fail");
		(*block)->status = STATUS_MAPPED;
	}

	if (last_block)
		last_block->next = *block;

	(*block)->size = round_up(size);
	(*block)->next = NULL;
}

int switch_alloc(struct block_meta *block, size_t size)
{
	size_t required_size = round_up(size + META_SIZE);

	return (block->status == STATUS_MAPPED && required_size < LARGE_ALLOC_LIMIT) ||
		   (block->status == STATUS_ALLOC && required_size >= LARGE_ALLOC_LIMIT);
}

void *handle_alloc(size_t size, size_t threshold)
{
	if (!global_head) {
		init_new_block(&global_head, NULL, size, threshold);
		if (first_block)
			global_head->next = first_block;
		first_block = global_head;
		return (void *)((char *)global_head + META_SIZE);
	}

	struct block_meta *block;
	size_t aligned_size = round_up(size);
	struct block_meta *previous = global_head;

	block = seek_free_block(&previous, size);
	if (block) {
		size_t remaining_size = block->size - aligned_size;

		if (remaining_size >= round_up(1 + META_SIZE)) {
			break_block(block, aligned_size);
			block->size = aligned_size;
		}
		block->status = STATUS_ALLOC;
	} else {
		if (previous->status == STATUS_FREE) {
			size_t expand_size = aligned_size - previous->size;

			sbrk(expand_size);
			block = previous;
			block->size = aligned_size;
			block->status = STATUS_ALLOC;
		} else {
			init_new_block(&block, previous, size, threshold);
		}
	}
	return (void *)((char *)block + META_SIZE);
}

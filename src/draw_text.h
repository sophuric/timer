#ifndef DRAW_TEXT_H
#define DRAW_TEXT_H
#include <stdbool.h>
#include <stdio.h>
#include <sys/ioctl.h>

#define DRAW_TEXTS(...) ((const char *const[]) {__VA_ARGS__, NULL})

bool render_figlet_text(const char *text, const char *font, char *out, size_t out_size);
void measure_text(const char *text, size_t *width, size_t *height);
void clear_screen(FILE *fp);
void draw_text(FILE *fp, const char *text, int x, int y);

struct draw_text_blocks_settings {
	// e.g if global_offset = ( 0, 0), global_mul = (0.5, 0.5), the blocks will be aligned in the center of the terminal
	// e.g if global_offset = ( 1, 1), global_mul = (0.0, 0.0), the blocks will be aligned (1,1) cells from the top left of the terminal
	// e.g if global_offset = (-1,-1), global_mul = (1.0, 1.0), the blocks will be aligned (1,1) cells from the bottom right of the terminal
	int global_offset_x, global_offset_y;
	float global_mul_x, global_mul_y;

	bool stack_horizontal; // false = blocks are stacked vertically, first text at the top. true = blocks are stacked horizontally, first text on the left.
	int each_offset;       // how many cells separate each text block
	float each_align;      // alignment of each text block, 0.0 = align left/top, 0.5 = align center/middle, 1.0 = align right/bottom, for stack vertical/horizontal respectively
};

bool draw_text_blocks(FILE *fp, const char *const *texts, size_t terminal_width, size_t terminal_height, struct draw_text_blocks_settings draw_settings);

void set_terminal_title(FILE *fp, const char *title);
#endif

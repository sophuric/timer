#include "draw_text.h"
#include "io.h"

#include <stdio.h>
#include <ctype.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define STR_(str) #str
#define STR(str) STR_(str)

bool render_figlet_text(const char *text, const char *font, char *out, size_t out_size) {
	static char *const path = STR(FIGLET_BIN_PATH);
	const char *argv[16] = {};
	int i = 0;
	argv[i++] = path;
	argv[i++] = "-lw10000"; // left align, set max width really high
	if (font) {
		argv[i++] = "-f";
		argv[i++] = font;
	}
	argv[i++] = "--";
	argv[i++] = text;

	// TODO: disable_display before error occurs so we exit safely and the error appears
	if (!fork_execvp_read_stdout(path, (char *const *) argv, out, out_size - 1, &out_size, false)) return false;
	out[out_size] = '\0';
	return true;
}

void measure_text(const char *text, size_t *width_, size_t *height_) {
	size_t width = 0, current_width = 0, height = 0;
	if (text) {
		for (; *text; ++text) {
			if (*text == '\n') {
				if (current_width == 0) ++height;
				else if (current_width > width)
					width = current_width;
				current_width = 0;
			}
			if (!isprint(*text)) continue;
			if (current_width == 0) ++height;
			++current_width;
		}
		if (current_width > width) width = current_width;
	}

	if (width_) *width_ = width;
	if (height_) *height_ = height;
}

void clear_screen(FILE *fp) {
	fprintf(fp, "\x1b[2J");
}

void draw_text(FILE *fp, const char *text, int x, int y) {
	if (!text) return;
	for (const char *line_start = text;; ++text) {
		int new_y = y;
		if (*text == '\r' || *text == '\0')
			;
		else if (*text == '\n')
			++new_y;
		else
			continue;

		size_t write_size = text - line_start;
		if (text > line_start) {
			// move cursor
			fprintf(fp, "\x1b[%d;%dH", y + 1, x + 1);
			// write line to the fp
			fwrite(line_start, 1, write_size, fp);
		}

		y = new_y;

		if (*text == '\0') break;
		line_start = text + 1; // move start to next character
	}
	fprintf(fp, "\r\n");
}

struct pos {
	union {
		size_t width, x;
	};
	union {
		size_t height, y;
	};
};

bool draw_text_blocks(FILE *fp, const char *const *texts, size_t terminal_width_, size_t terminal_height_, struct draw_text_blocks_settings set) {
	struct pos terminal_size = {terminal_width_, terminal_height_};
	size_t text_count = 0;
	for (const char *const *t = texts; *t; ++t) ++text_count;

	// measure total size of texts
	struct pos total_size = {0, 0}, text_sizes[text_count];
	for (size_t i = 0; i < text_count; ++i) {
		struct pos *size = &text_sizes[i];
		measure_text(texts[i], &size->width, &size->height);

		int additional_offset = i > 0 ? set.each_offset : 0;

		if (set.stack_horizontal)
			total_size.width += size->width + additional_offset;
		else if (size->width > total_size.width)
			total_size.width = size->width;

		if (!set.stack_horizontal)
			total_size.height += size->height + additional_offset;
		else if (size->height > total_size.height)
			total_size.height = size->height;
	}

	if (terminal_size.x < total_size.x) return false;
	if (terminal_size.y < total_size.y) return false;

	struct pos text_pos;
	text_pos.x = (size_t) ((terminal_size.x - total_size.x) * set.global_mul_x) + set.global_offset_x;
	text_pos.y = (size_t) ((terminal_size.y - total_size.y) * set.global_mul_y) + set.global_offset_y;

	for (size_t i = 0; i < text_count; ++i) {
		struct pos text_size = text_sizes[i];

		if (set.stack_horizontal)
			text_pos.y = (size_t) ((terminal_size.y - text_size.y) * set.global_mul_y) + set.global_offset_y;
		else
			text_pos.x = (size_t) ((terminal_size.x - text_size.x) * set.global_mul_x) + set.global_offset_x;

		if (text_size.x == 0 && text_size.y == 0) continue;

		// draw
		draw_text(fp, texts[i], text_pos.x, text_pos.y);

		if (set.stack_horizontal)
			text_pos.x += text_size.x + set.each_offset;
		else
			text_pos.y += text_size.y + set.each_offset;
	}

	return true;
}

void set_terminal_title(FILE *fp, const char *title) {
	fprintf(fp, "\x1b]2;%s\x07", title);
	fflush(fp);
}

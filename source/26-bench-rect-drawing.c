#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>  // Needed for MinGW on Win10 to get the PRIu64 macros
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define GLAD_GL_IMPLEMENTATION
#include <gl45.h>
#define GL45_HELPERS_IMPLEMENTATION
#include "gl45_helpers.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "timer.h"

#include <SDL/SDL.h>



//
// Utility stuff
//

typedef struct { int64_t  x, y; }       vecl_t;
typedef struct { float    x, y; }       vecf_t;
typedef struct { int16_t  x, y; }       vecs_t;
typedef struct { uint16_t x, y; }       vecus_t;
typedef struct { int64_t  l, t, r, b; } rectl_t;
typedef struct { float    l, t, r, b; } rectf_t;

static inline vecl_t  vecl (int64_t  x, int64_t  y)                       { return (vecl_t ){ .x = x, .y = y };                         }
static inline vecf_t  vecf (float    x, float    y)                       { return (vecf_t ){ .x = x, .y = y };                         }
static inline vecs_t  vecs (int16_t  x, int16_t  y)                       { return (vecs_t ){ .x = x, .y = y };                         }
static inline vecus_t vecus(uint16_t x, uint16_t y)                       { return (vecus_t){ .x = x, .y = y };                         }
static inline rectl_t rectl(int64_t  x, int64_t  y, int64_t w, int64_t h) { return (rectl_t){ .l = x, .t = y, .r = x + w, .b = y + h }; }
static inline rectf_t rectf(float    x, float    y, float   w, float   h) { return (rectf_t){ .l = x, .t = y, .r = x + w, .b = y + h }; }
static inline int64_t rectl_width(rectl_t rect)  { return rect.r - rect.l; }
static inline float   rectf_width(rectf_t rect)  { return rect.r - rect.l; }
static inline int64_t rectl_height(rectl_t rect) { return rect.b - rect.t; }
static inline float   rectf_height(rectf_t rect) { return rect.b - rect.t; }

/**
 * Lifted from projects/voxels/11-rand-particles.c
 * Renamed rand_lcg64_randshift_in() to rand_in() and moved rand_lcg64_randshift() inside the function.
 * 
 * Based on https://gist.github.com/itsmrpeck/0c55bc45c69632c49a480e9c51a2beaa
 * 
 * Notes from the source:
 * 
 * 32 bit random number generator: LCG-64 & Random Shift (basic form of PCG)
 * Example usage code at the bottom of the file.
 * Reference: http://www.youtube.com/watch?v=45Oet5qjlms#t=43m19s
 * (lecture: "PCG: A Family of Better Random Number Generators" by Melissa O'Neill of Harvey Mudd College)
 * 
 * NOTE: I take a bunch of non-uniform shortcuts like using % for generating random numbers in a range, because it's simpler.
 * The official PCG library code doesn't take those shortcuts. Get it here: http://www.pcg-random.org/download.html
 */
uint32_t rand_in(uint64_t* state, uint32_t min, uint32_t max) {
	uint32_t rand_lcg64_randshift(uint64_t* state) {
		*state = (2862933555777941757 * (*state)) + 3037000493;
		uint8_t shift = 29 - (*state >> 61);
		uint32_t value = *state >> shift;
		return value;
	}
	
	uint32_t value = rand_lcg64_randshift(state);
	return min + (value % (max - min));
}

GLuint load_gl_texture(const char* filename) {
	GLuint texture = 0;
	glCreateTextures(GL_TEXTURE_2D, 1, &texture);
	
	int width = 0, height = 0, n = 0;
	uint8_t* pixel_data = stbi_load(filename, &width, &height, &n, 4);
		glTextureStorage2D(texture, 4, GL_RGBA8, width, height);
		glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);
		glGenerateTextureMipmap(texture);
	stbi_image_free(pixel_data);
	
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);  // GL_LINEAR_MIPMAP_NEAREST is also ok
	
	return texture;
}

GLuint load_gl_texture_array(uint32_t texture_width, uint32_t texture_height, uint32_t layer_count, const char* filename_format, uint32_t index_start, uint32_t index_end) {
	assert(layer_count == index_end - index_start + 1);
	GLuint texture_array = 0;
	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texture_array);
	glTextureStorage3D(texture_array, 1, GL_RGBA8, texture_width, texture_height, layer_count);
	for (uint32_t i = index_start; i <= index_end; i++) {
		int width = 0, height = 0, n = 0;
		char filename[255];
		snprintf(filename, sizeof(filename), filename_format, i);
		uint8_t* pixel_data = stbi_load(filename, &width, &height, &n, 4);
			assert((uint32_t)width == texture_width);
			assert((uint32_t)height == texture_height);
			glTextureSubImage3D(texture_array, 0, 0, 0, i - index_start, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);
		stbi_image_free(pixel_data);
	}
	
	glTextureParameteri(texture_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(texture_array, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	return texture_array;
}



//
// Generate rectangles used by all benchmarks
//

typedef struct { uint8_t r, g, b, a; } color_t;
typedef struct {
	rectl_t  pos;
	color_t  background_color;
	bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
	float    border_width;
	color_t  border_color;
	uint32_t corner_radius;
	GLuint   texture_index;
	uint32_t texture_array_index;
	rectf_t  texture_coords;
	uint32_t random;
} rect_t;

typedef struct {
	uint32_t rects_count;
	rect_t* rects_ptr;
	uint32_t frame_count;
	SDL_Window* window;
	GLuint glyph_texture, image_texture, texture_array;
} scenario_args_t;

typedef struct {
	bool transparent_bg_color;
} generate_rects_opts_t;

void generate_rects_random(uint32_t new_rects_count, uint32_t* rects_count, rect_t** rects_ptr, generate_rects_opts_t opts) {
	*rects_count = new_rects_count;
	*rects_ptr = realloc(*rects_ptr, new_rects_count * sizeof((*rects_ptr)[0]));
	
	uint64_t state = 1;
	for (uint32_t i = 0; i < *rects_count; i++) {
		bool use_border = false, use_rounded_conrners = false, use_texture = false, use_texture_array = false;
		
		float x = rand_in(&state, 0, 900), y = rand_in(&state, 0, 600);
		rect_t rect = (rect_t){
			.pos = rectl( rand_in(&state, 0, 900), rand_in(&state, 0, 600), rand_in(&state, 10, 400), rand_in(&state, 10, 400) ),
			.background_color = (color_t){ rand_in(&state, 0, 255), rand_in(&state, 0, 255), rand_in(&state, 0, 255), opts.transparent_bg_color ? rand_in(&state, 64, 255) : 255 },
			.has_border = false
		};
		
		(*rects_ptr)[i] = rect;
	}
}

void generate_rects_sublime_sample(uint32_t* rects_count, rect_t** rects_ptr) {
	// texture_index 0 is the glyph atlas
	// texture_index 1 is the large image
	// texture_index 12 is the texture array with 10 48x48 elements
	rect_t dumped_rects[] = {
		// 26-rects.c generated via `echo -e "\ec" && make 16-render-api && ./16-render-api > 26-rects.c`,
		// switching to scenario 2 via the "2" key, centering the window and then pressing "d" to dump the rects to stdout.
		// switching to scenario 3 via "3" key, dumping via "d" key.
		// That way 26-rects.c contains data for both scenarios. #if and #endif are also outputted so we can select what
		// rects we get via the SCENARIO preprocessor variable.
		#define SCENARIO 1
		#include "26-rects.c"
		#undef SCENARIO
	};
	
	*rects_count = sizeof(dumped_rects) / sizeof(dumped_rects[0]);
	*rects_ptr = realloc(*rects_ptr, sizeof(dumped_rects));
	memcpy(*rects_ptr, dumped_rects, sizeof(dumped_rects));
}

void generate_rects_mediaplayer_sample(uint32_t* rects_count, rect_t** rects_ptr) {
	// texture_index 0 is the glyph atlas
	// texture_index 1 is the large image
	// texture_index 12 is the texture array with 10 48x48 elements
	rect_t dumped_rects[] = {
		// 26-rects.c was generated in the same way as described in generate_rects_sublime_sample().
		#define SCENARIO 2
		#include "26-rects.c"
		#undef SCENARIO
	};
	
	*rects_count = sizeof(dumped_rects) / sizeof(dumped_rects[0]);
	*rects_ptr = realloc(*rects_ptr, sizeof(dumped_rects));
	memcpy(*rects_ptr, dumped_rects, sizeof(dumped_rects));
}

void scenario_dump_stats(const char* name, scenario_args_t* args) {
	uint64_t total_area = 0;
	for (uint32_t i = 0; i < args->rects_count; i++)
		total_area += rectl_width(args->rects_ptr[i].pos) * rectl_height(args->rects_ptr[i].pos);
	
	fprintf(stdout, "scenario %s: %u rects, %.1lfpx avg area\n", name, args->rects_count, total_area / (double)args->rects_count);
}



//
// Stuff to collect statistics
// 
// The reporting works with checkpoints and deltas (differences between various checkpoints).
// The deltas also contain the OpenGL elapsed time query objects since the output from OpenGL already is a time delta.
//

const char* report_current_scenario;
const char* report_current_approach;

typedef struct {
	usec_t   walltime, cpu_time;
	GLuint   gpu_timestamp_id;
	uint64_t gpu_timestamp_ns;
} report_checkpoint_t;
typedef struct {
	usec_t   walltime, cpu_time;
	GLuint   gpu_elapsed_timer_id;
	uint64_t gpu_timestamp_ns, gpu_elapsed_time_ns;
	
	usec_t   accu_walltime, accu_cpu_time;
	uint64_t accu_gpu_timestamp_ns, accu_gpu_elapsed_time_ns;
} report_delta_t;

typedef enum { RC_APPROACH_START = 0, RC_FRAME_START, RC_GEN_BUFFERS_DONE, RC_UPLOAD_DONE, RC_CLEAR_DONE, RC_DRAW_DONE, RC_FRAME_END, RC_APPROACH_END } report_checkpoint_index_t;
typedef enum { RD_NONE = -1, RD_APPROACH = 0, RD_FRAME, RD_GEN_BUFFERS, RD_UPLOAD, RD_CLEAR, RD_DRAW, RD_PRESENT } report_delta_index_t;

report_checkpoint_t report_checkpoints[8];
report_delta_t      report_deltas[7];
usec_t report_last_frame_start_walltime, report_accu_dt_us;

uint32_t report_counter;
uint32_t reported_frame_count;
bool     report_elapsed_timer_running;
uint32_t report_prev_checkpoint_index;

bool reporting_output_csv_headers = true;
bool reporting_capture_last_frames = false;
bool reporting_query_timers = true;
bool reporting_output_per_frame_data = true;

void reporting_setup() {
	report_counter = 0;
	
	// Setup OpenGL timers
	for (uint32_t i = 0; i < sizeof(report_checkpoints) / sizeof(report_checkpoints[0]); i++)
		glCreateQueries(GL_TIMESTAMP, 1, &report_checkpoints[i].gpu_timestamp_id);
	for (uint32_t i = 0; i < sizeof(report_deltas) / sizeof(report_deltas[0]); i++)
		glCreateQueries(GL_TIME_ELAPSED, 1, &report_deltas[i].gpu_elapsed_timer_id);
	
	// Per-frame log header (per-frame data send to stderr)
	if (reporting_output_per_frame_data && reporting_output_csv_headers) {
		fprintf(stderr,
			"scenario        , approach                  , frame ,"
			"   frame_wt ,   frame_ct ,   frame_gt ,         dt ,"
			"  buffer_wt ,  buffer_ct ,  buffer_gt ,  buffer_ge ,"
			"  upload_wt ,  upload_ct ,  upload_gt ,  upload_ge ,"
			"   clear_wt ,   clear_ct ,   clear_gt ,   clear_ge ,"
			"    draw_wt ,    draw_ct ,    draw_gt ,    draw_ge ,"
			"    pres_wt ,    pres_ct ,    pres_gt ,    pres_ge\n"
		);
	}
	
	// Output CSV header for per-approach log (send to stdout)
	if (reporting_output_csv_headers) {
		fprintf(stdout,
			"scenario        , approach                  ,"
			" approach_wt, approach_ct, approach_gt,"
			"   frame_wt ,   frame_ct ,   frame_gt ,         dt ,"
			"  buffer_wt ,  buffer_ct ,  buffer_gt ,  buffer_ge ,"
			"  upload_wt ,  upload_ct ,  upload_gt ,  upload_ge ,"
			"   clear_wt ,   clear_ct ,   clear_gt ,   clear_ge ,"
			"    draw_wt ,    draw_ct ,    draw_gt ,    draw_ge ,"
			"    pres_wt ,    pres_ct ,    pres_gt ,    pres_ge\n"
		);
	}
}

void reporting_cleanup() {
	for (uint32_t i = 0; i < sizeof(report_checkpoints) / sizeof(report_checkpoints[0]); i++)
		glDeleteQueries(1, &report_checkpoints[i].gpu_timestamp_id);
	for (uint32_t i = 0; i < sizeof(report_deltas) / sizeof(report_deltas[0]); i++)
		glDeleteQueries(1, &report_deltas[i].gpu_elapsed_timer_id);
}


void report_reset_checkpoints_and_deltas() {
	for (uint32_t i = 0; i < sizeof(report_checkpoints) / sizeof(report_checkpoints[0]); i++) {
		report_checkpoints[i].walltime         = 0;
		report_checkpoints[i].cpu_time         = 0;
		report_checkpoints[i].gpu_timestamp_ns = 0;
	}
	for (uint32_t i = 0; i < sizeof(report_deltas) / sizeof(report_deltas[0]); i++) {
		report_deltas[i].walltime                 = 0;
		report_deltas[i].cpu_time                 = 0;
		report_deltas[i].gpu_timestamp_ns         = 0;
		report_deltas[i].gpu_elapsed_time_ns      = 0;
		report_deltas[i].accu_walltime            = 0;
		report_deltas[i].accu_cpu_time            = 0;
		report_deltas[i].accu_gpu_timestamp_ns    = 0;
		report_deltas[i].accu_gpu_elapsed_time_ns = 0;
	}
}

void report_trigger_checkpoint_and_elapsed_timer(report_checkpoint_index_t checkpoint, report_delta_index_t delta_with_gpu_elapsed_timer) {
	// Make sure we don't accidentially skip a checkpoint
	assert(checkpoint == report_prev_checkpoint_index + 1);
	report_prev_checkpoint_index = checkpoint;
	
	if (report_elapsed_timer_running) {
		glEndQuery(GL_TIME_ELAPSED);
		report_elapsed_timer_running = false;
	}
	
	glQueryCounter(report_checkpoints[checkpoint].gpu_timestamp_id, GL_TIMESTAMP);
	report_checkpoints[checkpoint].walltime = time_now();
	report_checkpoints[checkpoint].cpu_time = time_process_cpu_time();
	
	if (delta_with_gpu_elapsed_timer != RD_NONE) {
		glBeginQuery(GL_TIME_ELAPSED, report_deltas[delta_with_gpu_elapsed_timer].gpu_elapsed_timer_id);
		report_elapsed_timer_running = true;
	}
}

void report_query_checkpoints_and_delta_elapsed_timers() {
	for (uint32_t i = 0; i < sizeof(report_checkpoints) / sizeof(report_checkpoints[0]); i++)
		glGetQueryObjectui64v(report_checkpoints[i].gpu_timestamp_id, GL_QUERY_RESULT, &report_checkpoints[i].gpu_timestamp_ns);
	for (uint32_t i = 0; i < sizeof(report_deltas) / sizeof(report_deltas[0]); i++)
		glGetQueryObjectui64v(report_deltas[i].gpu_elapsed_timer_id, GL_QUERY_RESULT, &report_deltas[i].gpu_elapsed_time_ns);
}

void report_update_delta(report_delta_index_t delta, report_checkpoint_index_t from, report_checkpoint_index_t to) {
	report_deltas[delta].walltime         = report_checkpoints[to].walltime         - report_checkpoints[from].walltime;
	report_deltas[delta].cpu_time         = report_checkpoints[to].cpu_time         - report_checkpoints[from].cpu_time;
	report_deltas[delta].gpu_timestamp_ns = report_checkpoints[to].gpu_timestamp_ns - report_checkpoints[from].gpu_timestamp_ns;
	// accu_gpu_elapsed_time_ns is already a delta between glBeginQuery() and glEndQuery() calls. No need to calculate anything.
	
	report_deltas[delta].accu_walltime            += report_deltas[delta].walltime;
	report_deltas[delta].accu_cpu_time            += report_deltas[delta].cpu_time;
	report_deltas[delta].accu_gpu_timestamp_ns    += report_deltas[delta].gpu_timestamp_ns;
	report_deltas[delta].accu_gpu_elapsed_time_ns += report_deltas[delta].gpu_elapsed_time_ns;
}

void report_scenario(const char* scenario_name) {
	report_current_scenario = scenario_name;
}

void report_approach_start(const char* approach_name) {
	report_current_approach = approach_name;
	
	report_counter += 1;
	reported_frame_count = 0;
	
	report_reset_checkpoints_and_deltas();
	report_last_frame_start_walltime = time_now();
	report_accu_dt_us = 0;
	
	report_elapsed_timer_running = false;
	report_prev_checkpoint_index = RC_APPROACH_START - 1;
	report_trigger_checkpoint_and_elapsed_timer(RC_APPROACH_START, RD_NONE);
}

void report_frame_start() {
	report_prev_checkpoint_index = RC_FRAME_START - 1;
	report_trigger_checkpoint_and_elapsed_timer(RC_FRAME_START, RD_NONE);
}

void report_gen_buffers_done() {
	report_trigger_checkpoint_and_elapsed_timer(RC_GEN_BUFFERS_DONE, RD_UPLOAD);
}

void report_upload_done() {
	report_trigger_checkpoint_and_elapsed_timer(RC_UPLOAD_DONE, RD_CLEAR);
}

void report_clear_done() {
	report_trigger_checkpoint_and_elapsed_timer(RC_CLEAR_DONE, RD_DRAW);
}

void report_draw_done() {
	report_trigger_checkpoint_and_elapsed_timer(RC_DRAW_DONE, RD_PRESENT);
}

void report_frame_end() {
	report_trigger_checkpoint_and_elapsed_timer(RC_FRAME_END, RD_NONE);
	
	// Measurements done, from here do the per frame data processing.
	reported_frame_count++;
	
	// Set reporting_query_timers to false to remove the pipeline stall at the end of each frame
	if (reporting_query_timers) {
		// Query the last timestamp first (checkpoint frame end) so we get one large pipeline stall. When this is done all other timers are finished because they occured before that. 
		glGetQueryObjectui64v(report_checkpoints[RC_FRAME_END].gpu_timestamp_id, GL_QUERY_RESULT, &report_checkpoints[RC_FRAME_END].gpu_timestamp_ns);
		report_query_checkpoints_and_delta_elapsed_timers();
	}
	
	report_update_delta(RD_FRAME,       RC_FRAME_START,      RC_FRAME_END);
	report_update_delta(RD_GEN_BUFFERS, RC_FRAME_START,      RC_GEN_BUFFERS_DONE);
	report_update_delta(RD_UPLOAD,      RC_GEN_BUFFERS_DONE, RC_UPLOAD_DONE);
	report_update_delta(RD_CLEAR,       RC_UPLOAD_DONE,      RC_CLEAR_DONE);
	report_update_delta(RD_DRAW,        RC_CLEAR_DONE,       RC_DRAW_DONE);
	report_update_delta(RD_PRESENT,     RC_DRAW_DONE,        RC_FRAME_END);
	
	usec_t frame_dt_us = report_checkpoints[RC_FRAME_START].walltime - report_last_frame_start_walltime;
	report_accu_dt_us += frame_dt_us;
	report_last_frame_start_walltime = report_checkpoints[RC_FRAME_START].walltime;
	
	if (reporting_output_per_frame_data) {
		// Per-frame log header (duplicated here for reference)
		//fprintf(stderr,
		//	"scenario        , approach                  , frame ,"
		//	"   frame_wt ,   frame_ct ,   frame_gt ,         dt ,"
		//	"  buffer_wt ,  buffer_ct ,  buffer_gt ,  buffer_ge ,"
		//	"  upload_wt ,  upload_ct ,  upload_gt ,  upload_ge ,"
		//	"   clear_wt ,   clear_ct ,   clear_gt ,   clear_ge ,"
		//	"    draw_wt ,    draw_ct ,    draw_gt ,    draw_ge ,"
		//	"    pres_wt ,    pres_ct ,    pres_gt ,    pres_ge\n"
		//);
		fprintf(stderr,
			"%-15s , %-25s , %5u ,"
			" %8"PRIu64"us , %8"PRIu64"us , %8.3lfus , %8"PRIu64"us ,"
			" %8"PRIu64"us , %8"PRIu64"us , %8.3lfus , %8.3lfus ,"
			" %8"PRIu64"us , %8"PRIu64"us , %8.3lfus , %8.3lfus ,"
			" %8"PRIu64"us , %8"PRIu64"us , %8.3lfus , %8.3lfus ,"
			" %8"PRIu64"us , %8"PRIu64"us , %8.3lfus , %8.3lfus ,"
			" %8"PRIu64"us , %8"PRIu64"us , %8.3lfus , %8.3lfus\n",
			report_current_scenario, report_current_approach, reported_frame_count,
			report_deltas[RD_FRAME      ].walltime, report_deltas[RD_FRAME      ].cpu_time, report_deltas[RD_FRAME      ].gpu_timestamp_ns / 1000.0, frame_dt_us,
			report_deltas[RD_GEN_BUFFERS].walltime, report_deltas[RD_GEN_BUFFERS].cpu_time, report_deltas[RD_GEN_BUFFERS].gpu_timestamp_ns / 1000.0, report_deltas[RD_GEN_BUFFERS].gpu_elapsed_time_ns / 1000.0,
			report_deltas[RD_UPLOAD     ].walltime, report_deltas[RD_UPLOAD     ].cpu_time, report_deltas[RD_UPLOAD     ].gpu_timestamp_ns / 1000.0, report_deltas[RD_UPLOAD     ].gpu_elapsed_time_ns / 1000.0,
			report_deltas[RD_CLEAR      ].walltime, report_deltas[RD_CLEAR      ].cpu_time, report_deltas[RD_CLEAR      ].gpu_timestamp_ns / 1000.0, report_deltas[RD_CLEAR      ].gpu_elapsed_time_ns / 1000.0,
			report_deltas[RD_DRAW       ].walltime, report_deltas[RD_DRAW       ].cpu_time, report_deltas[RD_DRAW       ].gpu_timestamp_ns / 1000.0, report_deltas[RD_DRAW       ].gpu_elapsed_time_ns / 1000.0,
			report_deltas[RD_PRESENT    ].walltime, report_deltas[RD_PRESENT    ].cpu_time, report_deltas[RD_PRESENT    ].gpu_timestamp_ns / 1000.0, report_deltas[RD_PRESENT    ].gpu_elapsed_time_ns / 1000.0
		);
	}
}

void report_approach_end() {
	report_trigger_checkpoint_and_elapsed_timer(RC_APPROACH_END, RD_NONE);
	if (reporting_query_timers)
		glGetQueryObjectui64v(report_checkpoints[RC_APPROACH_END].gpu_timestamp_id, GL_QUERY_RESULT, &report_checkpoints[RC_APPROACH_END].gpu_timestamp_ns);
	
	report_update_delta(RD_APPROACH, RC_APPROACH_START, RC_APPROACH_END);
	
	// Output per-approach log (header duplicated here for reference)
	//fprintf(stdout,
	//	"scenario        , approach                  ,"
	//	" approach_wt, approach_ct, approach_gt,"
	//	"   frame_wt ,   frame_ct ,   frame_gt ,         dt ,"
	//	"  buffer_wt ,  buffer_ct ,  buffer_gt ,  buffer_ge ,"
	//	"  upload_wt ,  upload_ct ,  upload_gt ,  upload_ge ,"
	//	"   clear_wt ,   clear_ct ,   clear_gt ,   clear_ge ,"
	//	"    draw_wt ,    draw_ct ,    draw_gt ,    draw_ge ,"
	//	"    pres_wt ,    pres_ct ,    pres_gt ,    pres_ge\n"
	//);
	fprintf(stdout,
		"%-15s , %-25s ,"
		" %8.3lfms , %8.3lfms , %8.3lfms ,"
		" %8.3lfms , %8.3lfms , %8.3lfms , %8.3lfms ,"
		" %8.3lfms , %8.3lfms , %8.3lfms , %8.3lfms ,"
		" %8.3lfms , %8.3lfms , %8.3lfms , %8.3lfms ,"
		" %8.3lfms , %8.3lfms , %8.3lfms , %8.3lfms ,"
		" %8.3lfms , %8.3lfms , %8.3lfms , %8.3lfms ,"
		" %8.3lfms , %8.3lfms , %8.3lfms , %8.3lfms\n",
		report_current_scenario, report_current_approach,
		report_deltas[RD_APPROACH   ].walltime      / 1000.0, report_deltas[RD_APPROACH   ].cpu_time      / 1000.0, report_deltas[RD_APPROACH   ].gpu_timestamp_ns      / 1000000.0,
		report_deltas[RD_FRAME      ].accu_walltime / 1000.0, report_deltas[RD_FRAME      ].accu_cpu_time / 1000.0, report_deltas[RD_FRAME      ].accu_gpu_timestamp_ns / 1000000.0, report_accu_dt_us / 1000.0,
		report_deltas[RD_GEN_BUFFERS].accu_walltime / 1000.0, report_deltas[RD_GEN_BUFFERS].accu_cpu_time / 1000.0, report_deltas[RD_GEN_BUFFERS].accu_gpu_timestamp_ns / 1000000.0, report_deltas[RD_GEN_BUFFERS].accu_gpu_elapsed_time_ns / 1000000.0,
		report_deltas[RD_UPLOAD     ].accu_walltime / 1000.0, report_deltas[RD_UPLOAD     ].accu_cpu_time / 1000.0, report_deltas[RD_UPLOAD     ].accu_gpu_timestamp_ns / 1000000.0, report_deltas[RD_UPLOAD     ].accu_gpu_elapsed_time_ns / 1000000.0,
		report_deltas[RD_CLEAR      ].accu_walltime / 1000.0, report_deltas[RD_CLEAR      ].accu_cpu_time / 1000.0, report_deltas[RD_CLEAR      ].accu_gpu_timestamp_ns / 1000000.0, report_deltas[RD_CLEAR      ].accu_gpu_elapsed_time_ns / 1000000.0,
		report_deltas[RD_DRAW       ].accu_walltime / 1000.0, report_deltas[RD_DRAW       ].accu_cpu_time / 1000.0, report_deltas[RD_DRAW       ].accu_gpu_timestamp_ns / 1000000.0, report_deltas[RD_DRAW       ].accu_gpu_elapsed_time_ns / 1000000.0,
		report_deltas[RD_PRESENT    ].accu_walltime / 1000.0, report_deltas[RD_PRESENT    ].accu_cpu_time / 1000.0, report_deltas[RD_PRESENT    ].accu_gpu_timestamp_ns / 1000000.0, report_deltas[RD_PRESENT    ].accu_gpu_elapsed_time_ns / 1000000.0
	);
	
	// Dump a screenshot of the benchmark
	if (reporting_capture_last_frames) {
		char filename[255];
		snprintf(filename, sizeof(filename), "26-%02u-%s-%s.ppm", report_counter, report_current_scenario, report_current_approach);
		save_default_framebuffer_as_ppm(filename, GL_FRONT);
	}
}



//
// Benchmarks
//

// Basically just a copy of load_shader_program() from gl45_helpers.h with GL_PROGRAM_SEPARABLE set so we can use the program for pipeline objects
GLuint load_shader_program_separable(bool separable, size_t shader_count, shader_type_and_source_t shaders[shader_count]) {
	const char* shader_type_name(GLenum type) {
		switch (type) {
			case GL_VERTEX_SHADER:   return "vertex shader";
			case GL_GEOMETRY_SHADER: return "geometry shader";
			case GL_FRAGMENT_SHADER: return "fragment shader";
			case GL_COMPUTE_SHADER:  return "compute shader";
			default:                 return "unknown shader";
		}
	}
	
	void fprint_shader_source_with_line_numbers(FILE* f, const char* source) {
		uint32_t line_number = 1;
		const char *line_start = source;
		while (*line_start != '\0') {
			const char* line_end = line_start;
			while ( !(*line_end == '\n' || *line_end == '\0') )
				line_end++;
			
			fprintf(f, "%2u: %.*s\n", line_number, (int)(line_end - line_start), line_start);
			line_number++;
			
			line_start = (*line_end == '\n') ? line_end + 1 : line_end;
		}
	}
	
	GLuint program = glCreateProgram();
	
	for (size_t i = 0; i < shader_count; i++) {
		GLuint shader = glCreateShader(shaders[i].type);
		glShaderSource(shader, 1, (const char*[]){ shaders[i].source }, NULL);
		glCompileShader(shader);
		
		GLint is_compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);
		if (is_compiled) {
			glAttachShader(program, shader);
			glDeleteShader(shader);
		} else {
			GLint log_size = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_size);
			char* log_buffer = malloc(log_size);
				glGetShaderInfoLog(shader, log_size, NULL, log_buffer);
				fprintf(stderr, "ERROR on compiling %s:\n%s\n", shader_type_name(shaders[i].type), log_buffer);
				fprintf(stderr, "Shader source:\n");
				fprint_shader_source_with_line_numbers(stderr, shaders[i].source);
			free(log_buffer);
			
			glDeleteShader(shader);
			unload_shader_program(program);
			goto fail;
		}
	}
	
	glProgramParameteri(program, GL_PROGRAM_SEPARABLE, separable);
	
	// Note: Error reporting needed since linker errors (like missing local group size) are not reported as OpenGL errors
	glLinkProgram(program);
	GLint is_linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &is_linked);
	if (is_linked) {
		return program;
	} else {
		GLint log_size = GL_FALSE;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_size);
		char* log_buffer = malloc(log_size);
			glGetProgramInfoLog(program, log_size, NULL, log_buffer);
			fprintf(stderr, "ERROR on linking shader:\n%s\n", log_buffer);
		free(log_buffer);
		
		for (size_t i = 0; i < shader_count; i++) {
			fprintf(stderr, "%s source:\n", shader_type_name(shaders[i].type));
			fprint_shader_source_with_line_numbers(stderr, shaders[i].source);
		}
		
		unload_shader_program(program);
		goto fail;
	}
	
	fail:
		exit(1);
		return 0;
}

void bench_one_rect_per_draw(scenario_args_t* args, bool use_program_pipeline) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// Create a VBO and VAO to render a rectangle
	GLuint rect_vbo = 0, rect_vao = 0;
	struct { float x, y; } rect_vertices[] = {
		{ 0, 0 }, // left  top
		{ 0, 1 }, // left  bottom
		{ 1, 0 }, // right top
		{ 0, 1 }, // left  bottom
		{ 1, 1 }, // right bottom
		{ 1, 0 }, // right top
	};
	glCreateBuffers(1, &rect_vbo);
	glNamedBufferStorage(rect_vbo, sizeof(rect_vertices), rect_vertices, 0);
	
	GLint pos_index_loc = 0;
	glCreateVertexArrays(1, &rect_vao);
	glVertexArrayVertexBuffer(rect_vao, 0, rect_vbo, 0, sizeof(rect_vertices[0]));  // Use rect_vbo as data source 0
	glEnableVertexArrayAttrib(rect_vao,  pos_index_loc);
	glVertexArrayAttribBinding(rect_vao, pos_index_loc, 0);
	glVertexArrayAttribFormat(rect_vao,  pos_index_loc, 2, GL_FLOAT, GL_FALSE, 0);
	
	GLuint shader_program = load_shader_program_separable(use_program_pipeline ? GL_TRUE : GL_FALSE, 2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec4 pos_ltwh;\n"
			"layout(location = 1) uniform vec4 tex_coords_ltwh;\n"
			"layout(location = 3) uniform vec2 half_window_size;\n"
			"\n"
			"layout(location = 0) in vec2 pos_index;\n"
			"\n"
			"out vec2 vertex_tex_coords;\n"
			"out vec2 vertex_pos_vs;\n"
			"out vec4 vertex_rect_ltrb_vs;\n"
			"\n"
			"// We need to redeclare that block because we want to use the shader in a pipeline object.\n"
			"// That requires GL_PROGRAM_SEPARABLE, which requires this redeclaration when gl_Position is used.\n"
			"out gl_PerVertex\n"
			"{\n"
			"  vec4 gl_Position;\n"
			"};\n"
			"\n"
			"void main() {\n"
			"	vertex_tex_coords = tex_coords_ltwh.xy + pos_index * tex_coords_ltwh.zw;\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vertex_rect_ltrb_vs = vec4(pos_ltwh.xy, pos_ltwh.xy + pos_ltwh.zw);\n"
			"	vertex_pos_vs = pos_ltwh.xy + pos_index * pos_ltwh.zw;\n"
			"	gl_Position = vec4((vertex_pos_vs / half_window_size - 1.0) * axes_flip, 0, 1);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location =  4) uniform bool  use_texture;\n"
			"layout(location =  5) uniform bool  use_texture_array;\n"
			"layout(location =  6) uniform bool  use_glyph;\n"
			"layout(location =  7) uniform bool  use_border;\n"
			"layout(location =  8) uniform vec4  vertex_color;\n"
			"layout(location =  9) uniform uint  texture_array_index;\n"
			"layout(location = 10) uniform float border_width;\n"
			"layout(location = 11) uniform float border_radius;\n"
			"layout(location = 12) uniform vec4  border_color;\n"
			"\n"
			"layout(binding = 0) uniform sampler2D      texture_image;\n"
			"layout(binding = 1) uniform sampler2DArray texture_array;\n"
			"\n"
			"in vec2 vertex_tex_coords;\n"
			"in vec2 vertex_pos_vs;\n"
			"in vec4 vertex_rect_ltrb_vs;\n"
			"\n"
			"out vec4 frag_color;"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_color;\n"
			"	if (use_texture && !use_texture_array) {\n"
			"		content_color = texture(texture_image, vertex_tex_coords / textureSize(texture_image, 0));\n"
			"	} else if (use_texture_array) {\n"
			"		content_color = texture(texture_array, vec3(vertex_tex_coords / textureSize(texture_array, 0).xy, texture_array_index));\n"
			"	}\n"
			"	if (use_glyph) {\n"
			"		frag_color = vec4(vertex_color.rgb, vertex_color.a * content_color.r);\n"
			"	} else if (use_border) {\n"
			"		float r = border_radius + border_width;\n"
			"		float rect_dist = sdAxisAlignedRect(vertex_pos_vs, vertex_rect_ltrb_vs.xy + r, vertex_rect_ltrb_vs.zw - r) - r;\n"
			"		float pixel_width = dFdx(vertex_pos_vs.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		float rect_coverage = 1 - smoothstep(-pixel_width, 0, rect_dist);\n"
			"		float border_inner_transition = 1 - smoothstep(-border_width, -(border_width + pixel_width), rect_dist);\n"
			"		\n"
			"		vec4 rect_color = vec4(mix(content_color.rgb, border_color.rgb, border_inner_transition * border_color.a), content_color.a);\n"
			"		frag_color = vec4(rect_color.rgb, rect_color.a * rect_coverage);\n"
			"	} else {\n"
			"		frag_color = content_color;\n"
			"	}\n"
			"}\n"
		}
	});
	
	// Based on https://www.khronos.org/opengl/wiki/Shader_Compilation#Program_pipelines
	GLuint pipeline = 0;
	if (use_program_pipeline) {
		glCreateProgramPipelines(1, &pipeline);
		glUseProgramStages(pipeline, GL_ALL_SHADER_BITS, shader_program);
	}
	
	report_approach_start(use_program_pipeline ? "1rect_1draw_pipe" : "1rect_1draw");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
		
		// This approach doesn't need a GPU buffer we have to write the rects into, so we just do nothing in those steps.
		report_gen_buffers_done();
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(rect_vao);
				if (use_program_pipeline) {
					glUseProgram(0);  // a bound program takes precedence over the bound pipeline, hence make sure there is no program bound.
					glBindProgramPipeline(pipeline);
				} else {
					glUseProgram(shader_program);
				}
				
				for (uint32_t i = 0; i < args->rects_count; i++) {
					rect_t* r = &args->rects_ptr[i];
					
					glProgramUniform4f(shader_program,  0, r->pos.l, r->pos.t, rectl_width(r->pos), rectl_height(r->pos));  // pos_ltwh
					glProgramUniform4f(shader_program,  1, r->texture_coords.l, r->texture_coords.t, rectf_width(r->texture_coords), rectf_height(r->texture_coords));  // tex_coords_ltwh
					glProgramUniform2f(shader_program,  3, window_width / 2, window_height / 2);  // half_window_size
					glProgramUniform1i(shader_program,  4, r->has_texture);  // use_texture
					glProgramUniform1i(shader_program,  5, r->has_texture_array);  // use_texture_array
					glProgramUniform1i(shader_program,  6, r->has_glyph);  // use_glyph
					glProgramUniform1i(shader_program,  7, r->has_border);  // use_border
					glProgramUniform4f(shader_program,  8, r->background_color.r / 255.0, r->background_color.g / 255.0, r->background_color.b / 255.0, r->background_color.a / 255.0);  // vertex_color
					glProgramUniform1ui(shader_program, 9, r->texture_array_index);  // texture_array_index
					glProgramUniform1f(shader_program, 10, r->border_width);  // border_width
					glProgramUniform1f(shader_program, 11, r->corner_radius);  // border_radius
					glProgramUniform4f(shader_program, 12, r->border_color.r / 255.0, r->border_color.g / 255.0, r->border_color.b / 255.0, r->border_color.a / 255.0);  // border_color
					
					GLuint texture = 0;
					switch (r->texture_index) {
						case 0:  texture = args->glyph_texture; break;
						case 1:  texture = args->image_texture; break;
						case 12: texture = args->texture_array; break;
					}
					if (r->has_texture)
						glBindTextureUnit(r->has_texture_array ? 1 : 0, texture);
					
					glDrawArrays(GL_TRIANGLES, 0, sizeof(rect_vertices) / sizeof(rect_vertices[0]));
				}
				
				if (use_program_pipeline)
					glBindProgramPipeline(0);
				else
					glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	
	report_approach_end();
	
	if (use_program_pipeline)
		glDeleteProgramPipelines(1, &pipeline);
	unload_shader_program(shader_program);
	glDeleteVertexArrays(1, &rect_vao);
	glDeleteBuffers(1, &rect_vbo);
}


void bench_simple_vertex_buffer_for_all_rects(scenario_args_t* args, bool use_buffer_storage) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// Create a VBO and VAO
	typedef struct { float x, y; uint8_t r, g, b, a; } simple_vbo_vertex_t;
	GLuint vbo = 0, vao = 0;
	glCreateBuffers(1, &vbo);
	const int vertices_per_rect = 6, vbo_size = args->rects_count * vertices_per_rect * sizeof(simple_vbo_vertex_t);
	simple_vbo_vertex_t* vertices = malloc(vbo_size);
	
	if (use_buffer_storage)
		glNamedBufferStorage(vbo, vbo_size, NULL, GL_DYNAMIC_STORAGE_BIT);
	
	GLint vertex_pos_vs_loc = 0, vertex_color_loc = 1;
	glCreateVertexArrays(1, &vao);
	glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(vertices[0]));  // Set data source 0 to vbo, with offset 0 and proper stride
	glEnableVertexArrayAttrib( vao, vertex_pos_vs_loc);
	glVertexArrayAttribBinding(vao, vertex_pos_vs_loc, 0);
	glVertexArrayAttribFormat( vao, vertex_pos_vs_loc, 2, GL_FLOAT, GL_FALSE, 0);
	glEnableVertexArrayAttrib( vao, vertex_color_loc);
	glVertexArrayAttribBinding(vao, vertex_color_loc, 0);
	glVertexArrayAttribFormat( vao, vertex_color_loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(simple_vbo_vertex_t, r));
	
	GLuint shader_program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_window_size;\n"
			"\n"
			"layout(location = 0) in vec2 vertex_pos_vs;\n"
			"layout(location = 1) in vec4 vertex_color;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"void main() {\n"
			"	fragment_color = vertex_color;\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	gl_Position = vec4((vertex_pos_vs / half_window_size - 1.0) * axes_flip, 0, 1);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"in  vec4 fragment_color;\n"
			"out vec4 output_color;\n"
			"\n"
			"void main() {\n"
			"	output_color = fragment_color;\n"
			"}\n"
		}
	});
	
	report_approach_start(use_buffer_storage ? "simple_vbo_stor" : "simple_vbo");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update VBO with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				rect_t* r = &args->rects_ptr[i];
				vertices[i*vertices_per_rect + 0] = (simple_vbo_vertex_t){ .x = r->pos.l, .y = r->pos.t, .r = r->background_color.r, .g = r->background_color.g, .b = r->background_color.b, .a = r->background_color.a };  // left  top
				vertices[i*vertices_per_rect + 1] = (simple_vbo_vertex_t){ .x = r->pos.l, .y = r->pos.b, .r = r->background_color.r, .g = r->background_color.g, .b = r->background_color.b, .a = r->background_color.a };  // left  bottom
				vertices[i*vertices_per_rect + 2] = (simple_vbo_vertex_t){ .x = r->pos.r, .y = r->pos.t, .r = r->background_color.r, .g = r->background_color.g, .b = r->background_color.b, .a = r->background_color.a };  // right top
				vertices[i*vertices_per_rect + 3] = (simple_vbo_vertex_t){ .x = r->pos.l, .y = r->pos.b, .r = r->background_color.r, .g = r->background_color.g, .b = r->background_color.b, .a = r->background_color.a };  // left  bottom
				vertices[i*vertices_per_rect + 4] = (simple_vbo_vertex_t){ .x = r->pos.r, .y = r->pos.b, .r = r->background_color.r, .g = r->background_color.g, .b = r->background_color.b, .a = r->background_color.a };  // right bottom
				vertices[i*vertices_per_rect + 5] = (simple_vbo_vertex_t){ .x = r->pos.r, .y = r->pos.t, .r = r->background_color.r, .g = r->background_color.g, .b = r->background_color.b, .a = r->background_color.a };  // right top
			}
			
		report_gen_buffers_done();
			
			if (use_buffer_storage) {
				glInvalidateBufferData(vbo);
				glNamedBufferSubData(vbo, 0, vbo_size, vertices);
			} else {
				// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
				// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
				// pipeline stall on continous refresh.
				glNamedBufferData(vbo, vbo_size, vertices, GL_STREAM_DRAW);
			}
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(shader_program);
					glProgramUniform2f(shader_program, 0, window_width / 2, window_height / 2);
					glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	
	report_approach_end();
	
	free(vertices);
	unload_shader_program(shader_program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
}

void bench_complete_vertex_buffer_for_all_rects(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// Create a VBO and VAO
	typedef struct {
		// Stuff that is actually different per vertex
		float x, y;
		float tex_x, tex_y;
		// Stuff that is just different per rect
		uint8_t layer, flags, texture_index, texture_array_index;
		color_t background_color;
		color_t border_color;
		float border_width;
		float border_radius;
		float pos_l, pos_t, pos_r, pos_b;
	} complete_vbo_vertex_t;
	GLuint vbo = 0, vao = 0;
	glCreateBuffers(1, &vbo);
	const int vertices_per_rect = 6, vbo_size = args->rects_count * vertices_per_rect * sizeof(complete_vbo_vertex_t);
	complete_vbo_vertex_t* vertices = malloc(vbo_size);
	
	glCreateVertexArrays(1, &vao);
	glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(vertices[0]));  // Set data source 0 to vbo, with offset 0 and proper stride
	glEnableVertexArrayAttrib( vao, 0);  // vertex_pos_vs
	glVertexArrayAttribBinding(vao, 0, 0);
	glVertexArrayAttribFormat( vao, 0, 2, GL_FLOAT, GL_FALSE, offsetof(complete_vbo_vertex_t, x));
	glEnableVertexArrayAttrib( vao, 1);  // vertex_flags
	glVertexArrayAttribBinding(vao, 1, 0);
	glVertexArrayAttribIFormat(vao, 1, 1, GL_UNSIGNED_BYTE, offsetof(complete_vbo_vertex_t, flags));
	glEnableVertexArrayAttrib( vao, 2);  // vertex_texture_index
	glVertexArrayAttribBinding(vao, 2, 0);
	glVertexArrayAttribIFormat(vao, 2, 1, GL_UNSIGNED_BYTE, offsetof(complete_vbo_vertex_t, texture_index));
	glEnableVertexArrayAttrib( vao, 3);  // vertex_texture_array_index
	glVertexArrayAttribBinding(vao, 3, 0);
	glVertexArrayAttribIFormat(vao, 3, 1, GL_UNSIGNED_BYTE, offsetof(complete_vbo_vertex_t, texture_array_index));
	glEnableVertexArrayAttrib( vao, 4);  // vertex_background_color
	glVertexArrayAttribBinding(vao, 4, 0);
	glVertexArrayAttribFormat( vao, 4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(complete_vbo_vertex_t, background_color));
	glEnableVertexArrayAttrib( vao, 5);  // vertex_border_color
	glVertexArrayAttribBinding(vao, 5, 0);
	glVertexArrayAttribFormat( vao, 5, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(complete_vbo_vertex_t, border_color));
	glEnableVertexArrayAttrib( vao, 6);  // vertex_border_width
	glVertexArrayAttribBinding(vao, 6, 0);
	glVertexArrayAttribFormat( vao, 6, 1, GL_FLOAT, GL_FALSE, offsetof(complete_vbo_vertex_t, border_width));
	glEnableVertexArrayAttrib( vao, 7);  // vertex_border_radius
	glVertexArrayAttribBinding(vao, 7, 0);
	glVertexArrayAttribFormat( vao, 7, 1, GL_FLOAT, GL_FALSE, offsetof(complete_vbo_vertex_t, border_radius));
	glEnableVertexArrayAttrib( vao, 8);  // vertex_tex_coords
	glVertexArrayAttribBinding(vao, 8, 0);
	glVertexArrayAttribFormat( vao, 8, 2, GL_FLOAT, GL_FALSE, offsetof(complete_vbo_vertex_t, tex_x));
	glEnableVertexArrayAttrib( vao, 9);  // vertex_rect_ltrb
	glVertexArrayAttribBinding(vao, 9, 0);
	glVertexArrayAttribFormat( vao, 9, 4, GL_FLOAT, GL_FALSE, offsetof(complete_vbo_vertex_t, pos_l));
	
	GLuint shader_program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_window_size;\n"
			"\n"
			"layout(location = 0) in vec2  vertex_pos_vs;\n"
			"layout(location = 1) in uint  vertex_flags;\n"
			"layout(location = 2) in uint  vertex_texture_index;\n"
			"layout(location = 3) in uint  vertex_texture_array_index;\n"
			"layout(location = 4) in vec4  vertex_background_color;\n"
			"layout(location = 5) in vec4  vertex_border_color;\n"
			"layout(location = 6) in float vertex_border_width;\n"
			"layout(location = 7) in float vertex_border_radius;\n"
			"layout(location = 8) in vec2  vertex_tex_coords;\n"
			"layout(location = 9) in vec4  vertex_rect_ltrb;\n"
			"\n"
			"     out vec2  fragment_pos_vs;\n"
			"flat out uint  fragment_flags;\n"
			"flat out uint  fragment_texture_index;\n"
			"flat out uint  fragment_texture_array_index;\n"
			"flat out vec4  fragment_background_color;\n"
			"flat out vec4  fragment_border_color;\n"
			"flat out float fragment_border_width;\n"
			"flat out float fragment_border_radius;\n"
			"     out vec2  fragment_tex_coords;\n"
			"flat out vec4  fragment_rect_ltrb;\n"
			"\n"
			"void main() {\n"
			"	fragment_pos_vs              = vertex_pos_vs;\n"
			"	fragment_flags               = vertex_flags;\n"
			"	fragment_texture_index       = vertex_texture_index;\n"
			"	fragment_texture_array_index = vertex_texture_array_index;\n"
			"	fragment_background_color    = vertex_background_color;\n"
			"	fragment_border_color        = vertex_border_color;\n"
			"	fragment_border_width        = vertex_border_width;\n"
			"	fragment_border_radius       = vertex_border_radius;\n"
			"	fragment_tex_coords          = vertex_tex_coords;\n"
			"	fragment_rect_ltrb           = vertex_rect_ltrb;\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	gl_Position = vec4((vertex_pos_vs / half_window_size - 1.0) * axes_flip, 0, 1);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"     in vec2  fragment_pos_vs;\n"
			"flat in uint  fragment_flags;\n"
			"flat in uint  fragment_texture_index;\n"
			"flat in uint  fragment_texture_array_index;\n"
			"flat in vec4  fragment_background_color;\n"
			"flat in vec4  fragment_border_color;\n"
			"flat in float fragment_border_width;\n"
			"flat in float fragment_border_radius;\n"
			"     in vec2  fragment_tex_coords;\n"
			"flat in vec4  fragment_rect_ltrb;\n"
			"\n"
			"out vec4 output_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = fragment_background_color;\n"
			"	if ((fragment_flags & 1u) != 0) {  // RF_USE_TEXTURE\n"
			"		switch(fragment_texture_index) {\n"
			"			case  0:  content_color = texture(texture00, fragment_tex_coords / textureSize(texture00, 0));  break;\n"
			"			case  1:  content_color = texture(texture01, fragment_tex_coords / textureSize(texture01, 0));  break;\n"
			"			case  2:  content_color = texture(texture02, fragment_tex_coords / textureSize(texture02, 0));  break;\n"
			"			case  3:  content_color = texture(texture03, fragment_tex_coords / textureSize(texture03, 0));  break;\n"
			"			case  4:  content_color = texture(texture04, fragment_tex_coords / textureSize(texture04, 0));  break;\n"
			"			case  5:  content_color = texture(texture05, fragment_tex_coords / textureSize(texture05, 0));  break;\n"
			"			case  6:  content_color = texture(texture06, fragment_tex_coords / textureSize(texture06, 0));  break;\n"
			"			case  7:  content_color = texture(texture07, fragment_tex_coords / textureSize(texture07, 0));  break;\n"
			"			case  8:  content_color = texture(texture08, fragment_tex_coords / textureSize(texture08, 0));  break;\n"
			"			case  9:  content_color = texture(texture09, fragment_tex_coords / textureSize(texture09, 0));  break;\n"
			"			case 10:  content_color = texture(texture10, fragment_tex_coords / textureSize(texture10, 0));  break;\n"
			"			case 11:  content_color = texture(texture11, fragment_tex_coords / textureSize(texture11, 0));  break;\n"
			"			case 12:  content_color = texture(texture12, vec3(fragment_tex_coords / textureSize(texture12, 0).xy, fragment_texture_array_index));  break;\n"
			"			case 13:  content_color = texture(texture13, vec3(fragment_tex_coords / textureSize(texture13, 0).xy, fragment_texture_array_index));  break;\n"
			"			case 14:  content_color = texture(texture14, vec3(fragment_tex_coords / textureSize(texture14, 0).xy, fragment_texture_array_index));  break;\n"
			"			case 15:  content_color = texture(texture15, vec3(fragment_tex_coords / textureSize(texture15, 0).xy, fragment_texture_array_index));  break;\n"
			"		}\n"
			"	}\n"
			"	if ((fragment_flags & 2u) != 0) {  // RF_GLYPH\n"
			"		output_color = vec4(fragment_background_color.rgb, fragment_background_color.a * content_color.r);\n"
			"	} else if ((fragment_flags & 4u) != 0) {  // RF_USE_BORDER\n"
			"		float r = fragment_border_radius + fragment_border_width;\n"
			"		float rect_dist = sdAxisAlignedRect(fragment_pos_vs, fragment_rect_ltrb.xy + r, fragment_rect_ltrb.zw - r) - r;\n"
			"		float pixel_width = dFdx(fragment_pos_vs.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		float rect_coverage = 1 - smoothstep(-pixel_width, 0, rect_dist);\n"
			"		float border_inner_transition = 1 - smoothstep(-fragment_border_width, -(fragment_border_width + pixel_width), rect_dist);\n"
			"		\n"
			"		vec4 rect_color = vec4(mix(content_color.rgb, fragment_border_color.rgb, border_inner_transition * fragment_border_color.a), content_color.a);\n"
			"		output_color = vec4(rect_color.rgb, rect_color.a * rect_coverage);\n"
			"	} else {\n"
			"		output_color = content_color;\n"
			"	}\n"
			"}\n"
		}
	});
	
	report_approach_start("complete_vbo");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update VBO with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				rect_t* r = &args->rects_ptr[i];
				uint32_t flags = 0;
				if (r->has_texture)                          flags |= 1;
				if (r->has_glyph)                            flags |= 2;
				if (r->has_border || r->has_rounded_corners) flags |= 4;
				vertices[i*vertices_per_rect + 0] = (complete_vbo_vertex_t){ .x = r->pos.l, .y = r->pos.t, .tex_x = r->texture_coords.l, .tex_y = r->texture_coords.t, .pos_l = r->pos.l, .pos_t = r->pos.t, .pos_r = r->pos.r, .pos_b = r->pos.b, .flags = flags, .texture_index = r->texture_index, .texture_array_index = r->texture_array_index, .background_color = r->background_color, .border_color = r->border_color, .border_width = r->border_width, .border_radius = r->corner_radius };  // left  top
				vertices[i*vertices_per_rect + 1] = (complete_vbo_vertex_t){ .x = r->pos.l, .y = r->pos.b, .tex_x = r->texture_coords.l, .tex_y = r->texture_coords.b, .pos_l = r->pos.l, .pos_t = r->pos.t, .pos_r = r->pos.r, .pos_b = r->pos.b, .flags = flags, .texture_index = r->texture_index, .texture_array_index = r->texture_array_index, .background_color = r->background_color, .border_color = r->border_color, .border_width = r->border_width, .border_radius = r->corner_radius };  // left  bottom
				vertices[i*vertices_per_rect + 2] = (complete_vbo_vertex_t){ .x = r->pos.r, .y = r->pos.t, .tex_x = r->texture_coords.r, .tex_y = r->texture_coords.t, .pos_l = r->pos.l, .pos_t = r->pos.t, .pos_r = r->pos.r, .pos_b = r->pos.b, .flags = flags, .texture_index = r->texture_index, .texture_array_index = r->texture_array_index, .background_color = r->background_color, .border_color = r->border_color, .border_width = r->border_width, .border_radius = r->corner_radius };  // right top
				vertices[i*vertices_per_rect + 3] = (complete_vbo_vertex_t){ .x = r->pos.l, .y = r->pos.b, .tex_x = r->texture_coords.l, .tex_y = r->texture_coords.b, .pos_l = r->pos.l, .pos_t = r->pos.t, .pos_r = r->pos.r, .pos_b = r->pos.b, .flags = flags, .texture_index = r->texture_index, .texture_array_index = r->texture_array_index, .background_color = r->background_color, .border_color = r->border_color, .border_width = r->border_width, .border_radius = r->corner_radius };  // left  bottom
				vertices[i*vertices_per_rect + 4] = (complete_vbo_vertex_t){ .x = r->pos.r, .y = r->pos.b, .tex_x = r->texture_coords.r, .tex_y = r->texture_coords.b, .pos_l = r->pos.l, .pos_t = r->pos.t, .pos_r = r->pos.r, .pos_b = r->pos.b, .flags = flags, .texture_index = r->texture_index, .texture_array_index = r->texture_array_index, .background_color = r->background_color, .border_color = r->border_color, .border_width = r->border_width, .border_radius = r->corner_radius };  // right bottom
				vertices[i*vertices_per_rect + 5] = (complete_vbo_vertex_t){ .x = r->pos.r, .y = r->pos.t, .tex_x = r->texture_coords.r, .tex_y = r->texture_coords.t, .pos_l = r->pos.l, .pos_t = r->pos.t, .pos_r = r->pos.r, .pos_b = r->pos.b, .flags = flags, .texture_index = r->texture_index, .texture_array_index = r->texture_array_index, .background_color = r->background_color, .border_color = r->border_color, .border_width = r->border_width, .border_radius = r->corner_radius };  // right top
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(vbo, vbo_size, vertices, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(shader_program);
					glProgramUniform2f(shader_program, 0, window_width / 2, window_height / 2);
					
					glBindTextureUnit(0, args->glyph_texture);
					glBindTextureUnit(1, args->image_texture);
					glBindTextureUnit(12, args->texture_array);
					
					glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	free(vertices);
	unload_shader_program(shader_program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
}


void bench_one_ssbo(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	enum one_ssbo_flags_t { ONE_SSBO_USE_TEXTURE = (1 << 0), ONE_SSBO_USE_BORDER = (1 << 1), ONE_SSBO_GLYPH = (1 << 2) };
	typedef struct { float x, y, z, w; } one_ssbo_vec4_t __attribute__ ((aligned (16)));
	typedef struct {
		uint8_t layer, flags, texture_index, texture_array_index;
		color_t color;
		color_t border_color;
		float border_width;
		float border_radius;
		// Probably 12 bytes padding / space here.
		// Use one_ssbo_vec4_t because of 16 byte alignment. We use it as a vec4 in the vertex shader and vec4 has to be 16 byte aligned for the std430 layout.
		// This is where GLSL expects the data to be. The compiler should add some padding between this one_ssbo_vec4_t and the previous fields as necessary.
		one_ssbo_vec4_t pos;
		one_ssbo_vec4_t tex_coords;
	} one_ssbo_rect_t;
	one_ssbo_rect_t* rects_cpu_buffer = malloc(args->rects_count * sizeof(rects_cpu_buffer[0]));
	
	
	// All the data goes into the SSBO and we only use an empty VAO for the draw command. The shader then assembles the
	// per-vertex data by itself. An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &ssbo);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2  half_viewport_size;\n"
			"\n"
			"struct rect_data_t {\n"
			"	uint packed_layer_flags_texture_index_texture_array_index;\n"
			"	uint packed_color;\n"
			"	uint packed_border_color;\n"
			"	float border_width;\n"
			"	float border_radius;\n"
			"	// Probably 12 bytes padding / space here, vec4 has to be 16 byte aligned.\n"
			"	vec4 pos_ltrb;  // ltrb means left top right bottom\n"
			"	vec4 tex_coords_ltrb;\n"
			"};\n"
			"layout(std430, binding = 0) readonly buffer RectData {\n"
			"	rect_data_t rects[];\n"
			"};\n"
			"\n"
			"out uint  vertex_flags;\n"
			"out uint  vertex_texture_index;\n"
			"out uint  vertex_texture_array_index;\n"
			"out vec4  vertex_color;\n"
			"out vec4  vertex_border_color;\n"
			"out float vertex_border_radius;\n"
			"out float vertex_border_width;\n"
			"out vec2  vertex_pos_vs;\n"
			"out vec4  vertex_rect_ltrb_ws;\n"
			"out vec2  vertex_tex_coords;\n"
			"\n"
			"// We let glDrawArrays() create 6 vertices per rect\n"
			"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
			"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
			"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
			"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
			"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
			"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
			"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
			");\n"
			"\n"
			"void main() {\n"
			"	uint rect_index = uint(gl_VertexID) / 6;\n"
			"	uint vertex_offset = uint(gl_VertexID) % 6;\n"
			"	\n"
			"	uvec4 layer_flags_texture_index_texture_array_index = ((uvec4(rects[rect_index].packed_layer_flags_texture_index_texture_array_index) >> uvec4(0, 8, 16, 24)) & 0xffu);\n"
			"	uint layer               = layer_flags_texture_index_texture_array_index[0];\n"
			"	uint flags               = layer_flags_texture_index_texture_array_index[1];\n"
			"	uint texture_index       = layer_flags_texture_index_texture_array_index[2];\n"
			"	uint texture_array_index = layer_flags_texture_index_texture_array_index[3];\n"
			"	vec4 color               = ((uvec4(rects[rect_index].packed_color)        >> uvec4(0, 8, 16, 24)) & 0xffu) / vec4(255);\n"
			"	vec4 border_color        = ((uvec4(rects[rect_index].packed_border_color) >> uvec4(0, 8, 16, 24)) & 0xffu) / vec4(255);\n"
			"	float border_radius      = rects[rect_index].border_radius;\n"
			"	float border_width       = rects[rect_index].border_width;\n"
			"	\n"
			"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
			"	vec2 pos_vs     = vec2(rects[rect_index].pos_ltrb[component_index.x],        rects[rect_index].pos_ltrb[component_index.y]);\n"
			"	vec2 tex_coords = vec2(rects[rect_index].tex_coords_ltrb[component_index.x], rects[rect_index].tex_coords_ltrb[component_index.y]);\n"
			"	\n"
			"	//gl_Layer = int(layer);\n"
			"	vertex_flags               = flags;\n"
			"	vertex_texture_index       = texture_index;\n"
			"	vertex_texture_array_index = texture_array_index;\n"
			"	vertex_color               = color;\n"
			"	vertex_border_color        = border_color;\n"
			"	vertex_border_radius       = border_radius;\n"
			"	vertex_border_width        = border_width;\n"
			"	vertex_pos_vs              = pos_vs;\n"
			"	vertex_rect_ltrb_ws        = rects[rect_index].pos_ltrb;\n"
			"	vertex_tex_coords          = tex_coords;\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	//vec2 pos_vs = vec2(pos_ws - map_center_ws) * map_scale * axes_flip + half_viewport_size;\n"
			"	//vec2 pos_ndc = vec2(pos_ws - map_center_ws) * map_scale * axes_flip / half_viewport_size;\n"
			"	vec2 pos_ndc = (pos_vs / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"const uint RF_USE_TEXTURE = (1 << 0), RF_USE_BORDER = (1 << 1), RF_GLYPH = (1 << 2); // enum rect_flags_t;\n"
			"in flat uint  vertex_flags;\n"
			"in flat uint  vertex_texture_index;\n"
			"in flat uint  vertex_texture_array_index;\n"
			"in      vec4  vertex_color;\n"
			"in      vec4  vertex_border_color;\n"
			"in      float vertex_border_radius;\n"
			"in      float vertex_border_width;\n"
			"in      vec2  vertex_pos_vs;\n"
			"in      vec4  vertex_rect_ltrb_ws;\n"
			"in      vec2  vertex_tex_coords;\n"
			"\n"
			"out vec4 frag_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_color;\n"
			"	if ((vertex_flags & RF_USE_TEXTURE) != 0) {\n"
			"		switch(vertex_texture_index) {\n"
			"			case  0:  content_color = texture(texture00, vertex_tex_coords / textureSize(texture00, 0));  break;\n"
			"			case  1:  content_color = texture(texture01, vertex_tex_coords / textureSize(texture01, 0));  break;\n"
			"			case  2:  content_color = texture(texture02, vertex_tex_coords / textureSize(texture02, 0));  break;\n"
			"			case  3:  content_color = texture(texture03, vertex_tex_coords / textureSize(texture03, 0));  break;\n"
			"			case  4:  content_color = texture(texture04, vertex_tex_coords / textureSize(texture04, 0));  break;\n"
			"			case  5:  content_color = texture(texture05, vertex_tex_coords / textureSize(texture05, 0));  break;\n"
			"			case  6:  content_color = texture(texture06, vertex_tex_coords / textureSize(texture06, 0));  break;\n"
			"			case  7:  content_color = texture(texture07, vertex_tex_coords / textureSize(texture07, 0));  break;\n"
			"			case  8:  content_color = texture(texture08, vertex_tex_coords / textureSize(texture08, 0));  break;\n"
			"			case  9:  content_color = texture(texture09, vertex_tex_coords / textureSize(texture09, 0));  break;\n"
			"			case 10:  content_color = texture(texture10, vertex_tex_coords / textureSize(texture10, 0));  break;\n"
			"			case 11:  content_color = texture(texture11, vertex_tex_coords / textureSize(texture11, 0));  break;\n"
			"			case 12:  content_color = texture(texture12, vec3(vertex_tex_coords / textureSize(texture12, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 13:  content_color = texture(texture13, vec3(vertex_tex_coords / textureSize(texture13, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 14:  content_color = texture(texture14, vec3(vertex_tex_coords / textureSize(texture14, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 15:  content_color = texture(texture15, vec3(vertex_tex_coords / textureSize(texture15, 0).xy, vertex_texture_array_index));  break;\n"
			"		}\n"
			"	}\n"
			"	if ((vertex_flags & RF_GLYPH) != 0) {\n"
			"		frag_color = vec4(vertex_color.rgb, vertex_color.a * content_color.r);\n"
			"	} else if ((vertex_flags & RF_USE_BORDER) != 0) {\n"
			"		float r = vertex_border_radius + vertex_border_width;\n"
			"		float rect_dist = sdAxisAlignedRect(vertex_pos_vs, vertex_rect_ltrb_ws.xy + r, vertex_rect_ltrb_ws.zw - r) - r;\n"
			"		float pixel_width = dFdx(vertex_pos_vs.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		float rect_coverage = 1 - smoothstep(-pixel_width, 0, rect_dist);\n"
			"		float border_inner_transition = 1 - smoothstep(-vertex_border_width, -(vertex_border_width + pixel_width), rect_dist);\n"
			"		//float border_coverage = border_inner_transition * rect_coverage;\n"
			"		//float content_coverage = (1 - border_inner_transition) * rect_coverage;\n"
			"		\n"
			"		//vec4 rect_color = mix(content_color, vertex_border_color, border_inner_transition);  // Transition from content to border, transparent borders not blended on top of content.\n"
			"		vec4 rect_color = vec4(mix(content_color.rgb, vertex_border_color.rgb, border_inner_transition * vertex_border_color.a), content_color.a);\n"
			"		frag_color = vec4(rect_color.rgb, rect_color.a * rect_coverage);\n"
			"	} else {\n"
			"		frag_color = content_color;\n"
			"	}\n"
			"}\n"
		}
	});
	
	report_approach_start("one_ssbo");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBO with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				rects_cpu_buffer[i] = (one_ssbo_rect_t){
					.pos = (one_ssbo_vec4_t){ r->pos.l, r->pos.t, r->pos.r, r->pos.b }, .color = r->background_color,
					.border_width = r->border_width, .border_color = r->border_color, .border_radius = r->corner_radius,
					.texture_index = r->texture_index, .texture_array_index = r->texture_array_index, .tex_coords = (one_ssbo_vec4_t){ r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b },
					.flags = ((r->has_texture || r->has_texture_array) ? ONE_SSBO_USE_TEXTURE : 0) | ((r->has_border || r->has_rounded_corners) ? ONE_SSBO_USE_BORDER : 0) | (r->has_glyph ? ONE_SSBO_GLYPH : 0)
				};
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(ssbo, args->rects_count * sizeof(rects_cpu_buffer[0]), rects_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit(0, args->glyph_texture);
						glBindTextureUnit(1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &ssbo);
	free(rects_cpu_buffer);
}


void bench_ssbo_instruction_list(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// Moves bits into a specific part of the value. The arguments start_bit_lsb and bit_count are the same as used in
	// the GLSL function bitfieldExtract() to unpack them (`offset` and `bits`).
	uint32_t bits(uint32_t value, uint32_t start_bit_lsb, uint32_t bit_count) {
		assert(bit_count < 32);  // The bitshift below would zero the value when shifted by 32, hence the assert.
		uint32_t mask = (1 << bit_count) - 1;  // Using x - 1 to flip all lesser significant bits when just one bit is set
		return (value & mask) << start_bit_lsb;
	}
	
	typedef struct {       //        24         16          8          0
		uint32_t header1;  // LLLL LLLL  llll llll  llll tttt  tttt tttt      // L = layer (unused right now), l = left, t = top
		uint32_t header2;  // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb      // r = right, b = bottom
		uint32_t color;    // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa      // r = red, g = green, b = blue, a = alpha
		uint32_t instr;    // oooo oooo  oooo oooo  oooo oooo  cccc cccc      // o = offset, c = count
	} ssbo_instr_list_rect_t;
	ssbo_instr_list_rect_t pack_rect(uint32_t layer, rectl_t pos, color_t color, uint32_t instr_offset, uint32_t instr_count) {
		return (ssbo_instr_list_rect_t){
			.header1 = bits(layer, 24, 8) | bits(pos.l, 12, 12) | bits(pos.t, 0, 12),
			.header2 =                      bits(pos.r, 12, 12) | bits(pos.b, 0, 12),
			.color   = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8),
			.instr   = bits(instr_offset, 8, 24) | bits(instr_count, 0, 8),
		};
	}
	
	typedef struct { uint32_t x, y; } ssbo_instr_list_instr_t;
	enum { SSBOIL_T_GLYPH = 0, SSBOIL_T_TEXTURE, SSBOIL_T_ROUNDED_RECT_EQU, SSBOIL_T_LINE_EQU, SSBOIL_T_CIRCLE_EQU, SSBOIL_T_BORDER };
	ssbo_instr_list_instr_t pack_glyph(uint32_t texture_unit, rectl_t tex_coords) {                                                   // glyph
		return (ssbo_instr_list_instr_t){                                                                                             //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_GLYPH, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),    // type unit  llll llll  llll tttt  tttt tttt
			.y =                                                           bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)     // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	ssbo_instr_list_instr_t pack_texture(uint32_t texture_unit, uint32_t texture_array_index, rectl_t tex_coords) {                   // texture
		return (ssbo_instr_list_instr_t){                                                                                             //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_TEXTURE, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),  // type unit  llll llll  llll tttt  tttt tttt
			.y = bits(texture_array_index, 24, 8)                          | bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)   // iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	ssbo_instr_list_instr_t pack_rounded_rect_equ(rectl_t rect, uint32_t corner_radius) {                                             // rounded_rect_equ
		return (ssbo_instr_list_instr_t){                                                                                             //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_ROUNDED_RECT_EQU, 28, 4) | bits(rect.l, 12, 12) | bits(rect.t, 0, 12),                                 // type ____  llll llll  llll tttt  tttt tttt
			.y = bits(corner_radius, 24, 8)             | bits(rect.r, 12, 12) | bits(rect.b, 0, 12)                                  // cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius
		};
	}
	ssbo_instr_list_instr_t pack_line_equ(vecl_t pos1, vecl_t pos2) {                                                                 // line_equ
		return (ssbo_instr_list_instr_t){                                                                                             //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_LINE_EQU, 28, 4) | bits(pos1.x, 12, 12) | bits(pos1.y, 0, 12),                                         // type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos1 x y
			.y =                                  bits(pos2.x, 12, 12) | bits(pos2.y, 0, 12)                                          // ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos2 x y
		};
	}
	ssbo_instr_list_instr_t pack_circle_equ(vecl_t center, uint32_t outer_radius, uint32_t inner_radius) {                            // circle_equ
		return (ssbo_instr_list_instr_t){                                                                                             //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_CIRCLE_EQU, 28, 4) | bits(outer_radius, 12, 12) | bits(inner_radius, 0, 12),                           // type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr  // R = outer radius, r = inner radius
			.y = bits(center.x, 16, 16) | bits(center.y, 0, 16)                                                                       // xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints
		};
	}
	ssbo_instr_list_instr_t pack_border(int32_t start_dist, int32_t end_dist, color_t color) {                                        // border
		return (ssbo_instr_list_instr_t){                                                                                             //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_BORDER, 28, 4) | bits(start_dist, 12, 12) | bits(end_dist, 0, 12),                                     // type ____  ssss ssss  ssss eeee  eeee eeee  // s and e are signed ints (field start and field end)
			.y = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8)                              // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // border color
		};
	}
	
	// CPU side buffers. For the experiment we use a fixed instruction buffer with 4 times the count of the rects buffer and an assert below. Good enough for the experiment.
	uint32_t max_instr_count = args->rects_count * 4, rects_buffer_size = args->rects_count * sizeof(ssbo_instr_list_rect_t), instr_buffer_size = max_instr_count * sizeof(ssbo_instr_list_instr_t);
	ssbo_instr_list_rect_t*  rects_cpu_buffer = malloc(rects_buffer_size);
	ssbo_instr_list_instr_t* instr_cpu_buffer = malloc(instr_buffer_size);
	
	// All the data goes into the SSBOs and we only use an empty VAO for the draw command. The shader then assembles the per-vertex data by itself.
	// An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, rects_ssbo = 0, instr_ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &rects_ssbo);
	glCreateBuffers(1, &instr_ssbo);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2  half_viewport_size;\n"
			"\n"
			"layout(std430, binding = 0) readonly buffer RectData {\n"
			"	uvec4 rects[];\n"
			"};\n"
			"\n"
			"out vec2  vertex_pos_vs;\n"
			"out vec2  vertex_pos_in_rect_normalized;\n"
			"out vec4  vertex_color;\n"
			"out uint  vertex_instr_offset;\n"
			"out uint  vertex_instr_count;\n"
			"\n"
			"// We let glDrawArrays() create 6 vertices per rect\n"
			"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
			"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
			"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
			"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
			"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
			"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
			"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
			");\n"
			"\n"
			"void main() {\n"
			"	uint rect_index    = uint(gl_VertexID) / 6;\n"
			"	uint vertex_offset = uint(gl_VertexID) % 6;\n"
			"	\n"
			"	uvec4 rect_data     = rects[rect_index];\n"
			"	uint  layer         = bitfieldExtract(rect_data.x, 24, 8);\n"
			"	vec4  rect_ltrb_vs  = bitfieldExtract(rect_data.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"	vertex_color        = bitfieldExtract(rect_data.zzzz >> uvec4(24, 16, 8, 0), 0, 8) / vec4(255);\n"
			"	vertex_instr_offset = bitfieldExtract(rect_data.w, 8, 24);\n"
			"	vertex_instr_count  = bitfieldExtract(rect_data.w, 0, 8);\n"
			"	\n"
			"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
			"	vertex_pos_vs         = vec2(rect_ltrb_vs[component_index.x], rect_ltrb_vs[component_index.y]);\n"
			"	// Here the idea is that we get (0,0) for left top and (1,1) for right bottom\n"
			"	vertex_pos_in_rect_normalized = uvec2(equal(component_index, uvec2(2, 3)));\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_ndc = (vertex_pos_vs / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"	//gl_Layer = int(layer);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(std430, binding = 1) readonly buffer InstData {\n"
			"	uvec2 instructions[];\n"
			"};\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"vec4 read_texture_unit(uint texture_unit, uint texture_array_index, vec2 texture_coords) {\n"
			"	switch(texture_unit) {\n"
			"		case  0:  return texture(texture00,      texture_coords / textureSize(texture00, 0)                                );\n"
			"		case  1:  return texture(texture01,      texture_coords / textureSize(texture01, 0)                                );\n"
			"		case  2:  return texture(texture02,      texture_coords / textureSize(texture02, 0)                                );\n"
			"		case  3:  return texture(texture03,      texture_coords / textureSize(texture03, 0)                                );\n"
			"		case  4:  return texture(texture04,      texture_coords / textureSize(texture04, 0)                                );\n"
			"		case  5:  return texture(texture05,      texture_coords / textureSize(texture05, 0)                                );\n"
			"		case  6:  return texture(texture06,      texture_coords / textureSize(texture06, 0)                                );\n"
			"		case  7:  return texture(texture07,      texture_coords / textureSize(texture07, 0)                                );\n"
			"		case  8:  return texture(texture08,      texture_coords / textureSize(texture08, 0)                                );\n"
			"		case  9:  return texture(texture09,      texture_coords / textureSize(texture09, 0)                                );\n"
			"		case 10:  return texture(texture10,      texture_coords / textureSize(texture10, 0)                                );\n"
			"		case 11:  return texture(texture11,      texture_coords / textureSize(texture11, 0)                                );\n"
			"		case 12:  return texture(texture12, vec3(texture_coords / textureSize(texture12, 0).xy, float(texture_array_index)));\n"
			"		case 13:  return texture(texture13, vec3(texture_coords / textureSize(texture13, 0).xy, float(texture_array_index)));\n"
			"		case 14:  return texture(texture14, vec3(texture_coords / textureSize(texture14, 0).xy, float(texture_array_index)));\n"
			"		case 15:  return texture(texture15, vec3(texture_coords / textureSize(texture15, 0).xy, float(texture_array_index)));\n"
			"	}\n"
			"}\n"
			"\n"
			"in      vec2  vertex_pos_vs;\n"
			"in      vec2  vertex_pos_in_rect_normalized;\n"
			"in flat vec4  vertex_color;\n"
			"in flat uint  vertex_instr_offset;\n"
			"in flat uint  vertex_instr_count;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 pos, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-pos, pos-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_color;\n"
			"	float coverage = 1, distance = 0;\n"
			"	float pixel_width = dFdx(vertex_pos_vs.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"	\n"
			"	for (uint i = vertex_instr_offset; i < vertex_instr_offset + vertex_instr_count; i++) {\n"
			"		uvec2 instr = instructions[i];\n"
			"		uint  type  = bitfieldExtract(instr.x, 28, 4);\n"
			"		\n"
			"		switch (type) {\n"
			"			// glyph             type unit  llll llll  llll tttt  tttt tttt    ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
			"			case 0u: {\n"
			"				uint  texture_unit    = bitfieldExtract(instr.x, 24, 4);\n"
			"				uvec4 tex_coords_ltrb = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"				vec2  tex_coords      = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
			"				coverage = read_texture_unit(texture_unit, 0, tex_coords).r;\n"
			"				} break;\n"
			"			// texture           type unit  llll llll  llll tttt  tttt tttt    iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
			"			case 1u: {\n"
			"				uint  texture_unit        = bitfieldExtract(instr.x, 24, 4);\n"
			"				uvec4 tex_coords_ltrb     = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"				uint  texture_array_index = bitfieldExtract(instr.y, 24, 8);\n"
			"				vec2  tex_coords          = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
			"				content_color = read_texture_unit(texture_unit, texture_array_index, tex_coords);\n"
			"				} break;\n"
			"			// rounded_rect_equ  type ____  llll llll  llll tttt  tttt tttt    cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius\n"
			"			case 2u: {\n"
			"				uvec4 rect_ltrb   = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"				uint  radius      = bitfieldExtract(instr.y, 24, 8);\n"
			"				distance = sdAxisAlignedRect(vertex_pos_vs, rect_ltrb.xy + radius, rect_ltrb.zw - radius) - radius;\n"
			"				coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
			"				} break;\n"
			"			// line_equ          type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy    ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy\n"
			"			case 3u: {\n"
			"				// TODO\n"
			"				} break;\n"
			"			// circle_equ        type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr    xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints, R = outer radius, r = inner radius\n"
			"			case 4u: {\n"
			"				// TODO\n"
			"				} break;\n"
			"			// border            type ____  ssss ssss  ssss eeee  eeee eeee    rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // s and e are signed ints (field start and field end)\n"
			"			case 5u: {\n"
			"				ivec2 border_start_end_dist = ivec2(bitfieldExtract(instr.xx   >> uvec2(0, 12),        0, 12));\n"
			"				vec4  border_color          =       bitfieldExtract(instr.yyyy >> uvec4(24, 16, 8, 0), 0,  8);\n"
			"				float border_coverage       = smoothstep(border_start_end_dist.x, border_start_end_dist.x + pixel_width, distance) * smoothstep(border_start_end_dist.y, border_start_end_dist.y + pixel_width, distance);\n"
			"				content_color = mix(content_color, border_color, border_coverage);\n"
			"				} break;\n"
			"		}\n"
			"	}\n"
			"	\n"
			"	fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
			"}\n"
		}
	});
	
	report_approach_start("ssbo_instr_list");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBOs with new data (doesn't change here but would with real usecases)
			uint32_t instr_count = 0;
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				uint32_t instr_offset = instr_count;
				if (r->has_glyph)
					instr_cpu_buffer[instr_count++] = pack_glyph(r->texture_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
				else {
					if (r->has_texture)
						instr_cpu_buffer[instr_count++] = pack_texture(r->texture_index, r->texture_array_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
					if (r->has_rounded_corners)
						instr_cpu_buffer[instr_count++] = pack_rounded_rect_equ(r->pos, r->corner_radius);
					if (r->has_border)
						instr_cpu_buffer[instr_count++] = pack_border(0, -(r->border_width), r->border_color);
				}
				rects_cpu_buffer[i] = pack_rect(0, r->pos, r->background_color, instr_offset, instr_count - instr_offset);
				assert(instr_count <= max_instr_count);
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(rects_ssbo, rects_buffer_size, rects_cpu_buffer, GL_STREAM_DRAW);
			glNamedBufferData(instr_ssbo, instr_count * sizeof(instr_cpu_buffer[0]), instr_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rects_ssbo);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, instr_ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit( 0, args->glyph_texture);
						glBindTextureUnit( 1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &instr_ssbo);
	glDeleteBuffers(1, &rects_ssbo);
	free(instr_cpu_buffer);
	free(rects_cpu_buffer);
}

void bench_ssbo_inlined_instr_6(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// Moves bits into a specific part of the value. The arguments start_bit_lsb and bit_count are the same as used in
	// the GLSL function bitfieldExtract() to unpack them (`offset` and `bits`).
	uint32_t bits(uint32_t value, uint32_t start_bit_lsb, uint32_t bit_count) {
		assert(bit_count < 32);  // The bitshift below would zero the value when shifted by 32, hence the assert.
		uint32_t mask = (1 << bit_count) - 1;  // Using x - 1 to flip all lesser significant bits when just one bit is set
		return (value & mask) << start_bit_lsb;
	}
	
	typedef struct { uint32_t x, y; } ssbo_inlined_instr_instr_t;
	typedef struct {       //        24         16          8          0
		uint32_t header1;  // LLLL LLLL  llll llll  llll tttt  tttt tttt      // L = layer (unused right now), l = left, t = top
		uint32_t header2;  // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb      // r = right, b = bottom
		uint32_t color;    // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa      // r = red, g = green, b = blue, a = alpha
		uint32_t padding;  // ____ ____  ____ ____  ____ ____  ____ ____
		ssbo_inlined_instr_instr_t instr[6];
	} ssbo_inlined_instr_rect_t;
	ssbo_inlined_instr_rect_t pack_rect(uint32_t layer, rectl_t pos, color_t color) {
		return (ssbo_inlined_instr_rect_t){
			.header1 = bits(layer, 24, 8) | bits(pos.l, 12, 12) | bits(pos.t, 0, 12),
			.header2 =                      bits(pos.r, 12, 12) | bits(pos.b, 0, 12),
			.color   = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8)
		};
	}
	
	enum { SSBOIL_T_GLYPH = 1, SSBOIL_T_TEXTURE, SSBOIL_T_ROUNDED_RECT_EQU, SSBOIL_T_LINE_EQU, SSBOIL_T_CIRCLE_EQU, SSBOIL_T_BORDER };
	ssbo_inlined_instr_instr_t pack_glyph(uint32_t texture_unit, rectl_t tex_coords) {                                                // glyph
		return (ssbo_inlined_instr_instr_t){                                                                                          //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_GLYPH, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),    // type unit  llll llll  llll tttt  tttt tttt
			.y =                                                           bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)     // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	ssbo_inlined_instr_instr_t pack_texture(uint32_t texture_unit, uint32_t texture_array_index, rectl_t tex_coords) {                // texture
		return (ssbo_inlined_instr_instr_t){                                                                                          //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_TEXTURE, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),  // type unit  llll llll  llll tttt  tttt tttt
			.y = bits(texture_array_index, 24, 8)                          | bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)   // iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	ssbo_inlined_instr_instr_t pack_rounded_rect_equ(rectl_t rect, uint32_t corner_radius) {                                          // rounded_rect_equ
		return (ssbo_inlined_instr_instr_t){                                                                                          //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_ROUNDED_RECT_EQU, 28, 4) | bits(rect.l, 12, 12) | bits(rect.t, 0, 12),                                 // type ____  llll llll  llll tttt  tttt tttt
			.y = bits(corner_radius, 24, 8)             | bits(rect.r, 12, 12) | bits(rect.b, 0, 12)                                  // cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius
		};
	}
	ssbo_inlined_instr_instr_t pack_line_equ(vecl_t pos1, vecl_t pos2) {                                                              // line_equ
		return (ssbo_inlined_instr_instr_t){                                                                                          //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_LINE_EQU, 28, 4) | bits(pos1.x, 12, 12) | bits(pos1.y, 0, 12),                                         // type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos1 x y
			.y =                                  bits(pos2.x, 12, 12) | bits(pos2.y, 0, 12)                                          // ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos2 x y
		};
	}
	ssbo_inlined_instr_instr_t pack_circle_equ(vecl_t center, uint32_t outer_radius, uint32_t inner_radius) {                         // circle_equ
		return (ssbo_inlined_instr_instr_t){                                                                                          //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_CIRCLE_EQU, 28, 4) | bits(outer_radius, 12, 12) | bits(inner_radius, 0, 12),                           // type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr  // R = outer radius, r = inner radius
			.y = bits(center.x, 16, 16) | bits(center.y, 0, 16)                                                                       // xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints
		};
	}
	ssbo_inlined_instr_instr_t pack_border(int32_t start_dist, int32_t end_dist, color_t color) {                                     // border
		return (ssbo_inlined_instr_instr_t){                                                                                          //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_BORDER, 28, 4) | bits(start_dist, 12, 12) | bits(end_dist, 0, 12),                                     // type ____  ssss ssss  ssss eeee  eeee eeee  // s and e are signed ints (field start and field end)
			.y = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8)                              // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // border color
		};
	}
	
	// CPU side buffer
	uint32_t rects_buffer_size = args->rects_count * sizeof(ssbo_inlined_instr_rect_t);
	ssbo_inlined_instr_rect_t* rects_cpu_buffer = malloc(rects_buffer_size);
	
	// All the data goes into the SSBO and we only use an empty VAO for the draw command. The shader then assembles the per-vertex data by itself.
	// An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, rects_ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &rects_ssbo);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"\n"
			"struct Rect {\n"
			"	uvec4    header;\n"
			"	uvec2[6] instr;\n"
			"};\n"
			"layout(std430, binding = 0) readonly buffer RectBlock {\n"
			"	Rect rects[];\n"
			"};\n"
			"\n"
			"out vec2     vertex_pos_vs;\n"
			"out vec2     vertex_pos_in_rect_normalized;\n"
			"out vec4     vertex_color;\n"
			"out uvec2[6] vertex_instr;\n"
			"\n"
			"// We let glDrawArrays() create 6 vertices per rect\n"
			"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
			"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
			"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
			"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
			"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
			"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
			"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
			");\n"
			"\n"
			"void main() {\n"
			"	uint rect_index    = uint(gl_VertexID) / 6;\n"
			"	uint vertex_offset = uint(gl_VertexID) % 6;\n"
			"	\n"
			"	Rect  rect          = rects[rect_index];\n"
			"	uint  layer         = bitfieldExtract(rect.header.x, 24, 8);\n"
			"	vec4  rect_ltrb_vs  = bitfieldExtract(rect.header.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"	vertex_color        = bitfieldExtract(rect.header.zzzz >> uvec4(24, 16, 8, 0), 0, 8) / vec4(255);\n"
			"	vertex_instr        = rect.instr;  // This only works if vertex_instr and rect.instr are the same size\n"
			"	\n"
			"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
			"	vertex_pos_vs         = vec2(rect_ltrb_vs[component_index.x], rect_ltrb_vs[component_index.y]);\n"
			"	// Here the idea is that we get (0,0) for left top and (1,1) for right bottom\n"
			"	vertex_pos_in_rect_normalized = uvec2(equal(component_index, uvec2(2, 3)));\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_ndc = (vertex_pos_vs / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"	//gl_Layer = int(layer);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"vec4 read_texture_unit(uint texture_unit, uint texture_array_index, vec2 texture_coords) {\n"
			"	switch(texture_unit) {\n"
			"		case  0:  return texture(texture00,      texture_coords / textureSize(texture00, 0)                                );\n"
			"		case  1:  return texture(texture01,      texture_coords / textureSize(texture01, 0)                                );\n"
			"		case  2:  return texture(texture02,      texture_coords / textureSize(texture02, 0)                                );\n"
			"		case  3:  return texture(texture03,      texture_coords / textureSize(texture03, 0)                                );\n"
			"		case  4:  return texture(texture04,      texture_coords / textureSize(texture04, 0)                                );\n"
			"		case  5:  return texture(texture05,      texture_coords / textureSize(texture05, 0)                                );\n"
			"		case  6:  return texture(texture06,      texture_coords / textureSize(texture06, 0)                                );\n"
			"		case  7:  return texture(texture07,      texture_coords / textureSize(texture07, 0)                                );\n"
			"		case  8:  return texture(texture08,      texture_coords / textureSize(texture08, 0)                                );\n"
			"		case  9:  return texture(texture09,      texture_coords / textureSize(texture09, 0)                                );\n"
			"		case 10:  return texture(texture10,      texture_coords / textureSize(texture10, 0)                                );\n"
			"		case 11:  return texture(texture11,      texture_coords / textureSize(texture11, 0)                                );\n"
			"		case 12:  return texture(texture12, vec3(texture_coords / textureSize(texture12, 0).xy, float(texture_array_index)));\n"
			"		case 13:  return texture(texture13, vec3(texture_coords / textureSize(texture13, 0).xy, float(texture_array_index)));\n"
			"		case 14:  return texture(texture14, vec3(texture_coords / textureSize(texture14, 0).xy, float(texture_array_index)));\n"
			"		case 15:  return texture(texture15, vec3(texture_coords / textureSize(texture15, 0).xy, float(texture_array_index)));\n"
			"	}\n"
			"}\n"
			"\n"
			"in      vec2     vertex_pos_vs;\n"
			"in      vec2     vertex_pos_in_rect_normalized;\n"
			"in flat vec4     vertex_color;\n"
			"in flat uvec2[6] vertex_instr;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 pos, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-pos, pos-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_color;\n"
			"	float coverage = 1, distance = 0;\n"
			"	float pixel_width = dFdx(vertex_pos_vs.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"	\n"
			"	for (uint i = 0; i < vertex_instr.length(); i++) {\n"
			"		uvec2 instr = vertex_instr[i];\n"
			"		uint  type  = bitfieldExtract(instr.x, 28, 4);\n"
			"		\n"
			"		switch (type) {\n"
			"			// empty instruction, just skip it\n"
			"			case 0u:\n"
			"				//i = vertex_instr.length();  // Break the outer loop, halfs performance. Hence commented out.\n"
			"				break;\n"
			"			// glyph             type unit  llll llll  llll tttt  tttt tttt    ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
			"			case 1u: {\n"
			"				uint  texture_unit    = bitfieldExtract(instr.x, 24, 4);\n"
			"				uvec4 tex_coords_ltrb = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"				vec2  tex_coords      = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
			"				coverage = read_texture_unit(texture_unit, 0, tex_coords).r;\n"
			"				} break;\n"
			"			// texture           type unit  llll llll  llll tttt  tttt tttt    iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
			"			case 2u: {\n"
			"				uint  texture_unit        = bitfieldExtract(instr.x, 24, 4);\n"
			"				uvec4 tex_coords_ltrb     = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"				uint  texture_array_index = bitfieldExtract(instr.y, 24, 8);\n"
			"				vec2  tex_coords          = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
			"				content_color = read_texture_unit(texture_unit, texture_array_index, tex_coords);\n"
			"				} break;\n"
			"			// rounded_rect_equ  type ____  llll llll  llll tttt  tttt tttt    cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius\n"
			"			case 3u: {\n"
			"				uvec4 rect_ltrb   = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
			"				uint  radius      = bitfieldExtract(instr.y, 24, 8);\n"
			"				distance = sdAxisAlignedRect(vertex_pos_vs, rect_ltrb.xy + radius, rect_ltrb.zw - radius) - radius;\n"
			"				coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
			"				} break;\n"
			"			// line_equ          type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy    ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy\n"
			"			case 4u: {\n"
			"				// TODO\n"
			"				} break;\n"
			"			// circle_equ        type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr    xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints, R = outer radius, r = inner radius\n"
			"			case 5u: {\n"
			"				// TODO\n"
			"				} break;\n"
			"			// border            type ____  ssss ssss  ssss eeee  eeee eeee    rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // s and e are signed ints (field start and field end)\n"
			"			case 6u: {\n"
			"				ivec2 border_start_end_dist = ivec2(bitfieldExtract(instr.xx   >> uvec2(0, 12),        0, 12));\n"
			"				vec4  border_color          =       bitfieldExtract(instr.yyyy >> uvec4(24, 16, 8, 0), 0,  8);\n"
			"				float border_coverage       = smoothstep(border_start_end_dist.x, border_start_end_dist.x + pixel_width, distance) * smoothstep(border_start_end_dist.y, border_start_end_dist.y + pixel_width, distance);\n"
			"				content_color = mix(content_color, border_color, border_coverage);\n"
			"				} break;\n"
			"		}\n"
			"	}\n"
			"	\n"
			"	fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
			"}\n"
		}
	});
	
	report_approach_start("ssbo_inlined_instr_6");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBOs with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				ssbo_inlined_instr_rect_t rect = pack_rect(0, r->pos, r->background_color);
				
				uint32_t instr_count = 0;
				if (r->has_glyph)
					rect.instr[instr_count++] = pack_glyph(r->texture_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
				else {
					if (r->has_texture)
						rect.instr[instr_count++] = pack_texture(r->texture_index, r->texture_array_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
					if (r->has_rounded_corners)
						rect.instr[instr_count++] = pack_rounded_rect_equ(r->pos, r->corner_radius);
					if (r->has_border)
						rect.instr[instr_count++] = pack_border(0, -(r->border_width), r->border_color);
				}
				assert(instr_count <= 6);
				
				rects_cpu_buffer[i] = rect;
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(rects_ssbo, rects_buffer_size, rects_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rects_ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit( 0, args->glyph_texture);
						glBindTextureUnit( 1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &rects_ssbo);
	free(rects_cpu_buffer);
}

void bench_ssbo_inlined_instr(scenario_args_t* args, uint32_t rect_instr_count, uint32_t vertex_instr_count) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// CPU side buffer
	typedef struct { uint32_t x, y; } vecui_t;
	uint32_t buffer_elements_per_rect = (2 + rect_instr_count);
	uint32_t buffer_size = args->rects_count * buffer_elements_per_rect * sizeof(vecui_t);
	vecui_t* buffer = malloc(buffer_size);
	uint32_t buffer_curr_index = 0;
	
	void buffer_reset() {
		buffer_curr_index = 0;
		memset(buffer, 0, buffer_size);
	}
	
	// Moves bits into a specific part of the value. The arguments start_bit_lsb and bit_count are the same as used in
	// the GLSL function bitfieldExtract() to unpack them (`offset` and `bits`).
	uint32_t bits(uint32_t value, uint32_t start_bit_lsb, uint32_t bit_count) {
		assert(bit_count < 32);  // The bitshift below would zero the value when shifted by 32, hence the assert.
		uint32_t mask = (1 << bit_count) - 1;  // Using x - 1 to flip all lesser significant bits when just one bit is set
		return (value & mask) << start_bit_lsb;
	}
	
	void pack_rect(uint32_t layer, rectl_t pos, color_t color) {
		// Align rect to a rect boundary. In other words skip over any unused instructions from the previous rect.
		if (buffer_curr_index % buffer_elements_per_rect != 0)
			buffer_curr_index += buffer_elements_per_rect - (buffer_curr_index % buffer_elements_per_rect);
		buffer[buffer_curr_index++] = (vecui_t){                                                           //   28   24         16    12    8          0
			.x = bits(layer, 24, 8) | bits(pos.l, 12, 12) | bits(pos.t, 0, 12),                            // LLLL LLLL  llll llll  llll tttt  tttt tttt      // L = layer (unused right now), l = left, t = top
			.y =                      bits(pos.r, 12, 12) | bits(pos.b, 0, 12)                             // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb      // r = right, b = bottom
		};
		buffer[buffer_curr_index++] = (vecui_t){                                                           //   28   24         16    12    8          0
			.x = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8),  // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa      // r = red, g = green, b = blue, a = alpha
			.y = 0                                                                                         // ____ ____  ____ ____  ____ ____  ____ ____
		};
	}
	
	enum { SSBOIL_T_GLYPH = 1, SSBOIL_T_TEXTURE, SSBOIL_T_ROUNDED_RECT_EQU, SSBOIL_T_LINE_EQU, SSBOIL_T_CIRCLE_EQU, SSBOIL_T_BORDER };
	void pack_glyph(uint32_t texture_unit, rectl_t tex_coords) {                                                                      // glyph
		buffer[buffer_curr_index++] = (vecui_t){                                                                                      //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_GLYPH, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),    // type unit  llll llll  llll tttt  tttt tttt
			.y =                                                           bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)     // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	void pack_texture(uint32_t texture_unit, uint32_t texture_array_index, rectl_t tex_coords) {                                      // texture
		buffer[buffer_curr_index++] = (vecui_t){                                                                                      //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_TEXTURE, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),  // type unit  llll llll  llll tttt  tttt tttt
			.y = bits(texture_array_index, 24, 8)                          | bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)   // iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	void pack_rounded_rect_equ(rectl_t rect, uint32_t corner_radius) {                                                                // rounded_rect_equ
		buffer[buffer_curr_index++] = (vecui_t){                                                                                      //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_ROUNDED_RECT_EQU, 28, 4) | bits(rect.l, 12, 12) | bits(rect.t, 0, 12),                                 // type ____  llll llll  llll tttt  tttt tttt
			.y = bits(corner_radius, 24, 8)             | bits(rect.r, 12, 12) | bits(rect.b, 0, 12)                                  // cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius
		};
	}
	void pack_line_equ(vecl_t pos1, vecl_t pos2) {                                                                                    // line_equ
		buffer[buffer_curr_index++] = (vecui_t){                                                                                      //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_LINE_EQU, 28, 4) | bits(pos1.x, 12, 12) | bits(pos1.y, 0, 12),                                         // type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos1 x y
			.y =                                  bits(pos2.x, 12, 12) | bits(pos2.y, 0, 12)                                          // ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos2 x y
		};
	}
	void pack_circle_equ(vecl_t center, uint32_t outer_radius, uint32_t inner_radius) {                                               // circle_equ
		buffer[buffer_curr_index++] = (vecui_t){                                                                                      //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_CIRCLE_EQU, 28, 4) | bits(outer_radius, 12, 12) | bits(inner_radius, 0, 12),                           // type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr  // R = outer radius, r = inner radius
			.y = bits(center.x, 16, 16) | bits(center.y, 0, 16)                                                                       // xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints
		};
	}
	void pack_border(int32_t start_dist, int32_t end_dist, color_t color) {                                                           // border
		buffer[buffer_curr_index++] = (vecui_t){                                                                                      //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_BORDER, 28, 4) | bits(start_dist, 12, 12) | bits(end_dist, 0, 12),                                     // type ____  ssss ssss  ssss eeee  eeee eeee  // s and e are signed ints (field start and field end)
			.y = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8)                              // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // border color
		};
	}
	
	
	// All the data goes into the SSBO and we only use an empty VAO for the draw command. The shader then assembles the per-vertex data by itself.
	// An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, rects_ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &rects_ssbo);
	// printf() style placeholders in the shader code: %1$u = rect_instr_count, $2$u = vertex_instr_count (see man 3 printf "Format of the format string").
	// Removed those placeholders because that doesn't work on windows with MinGW.
	char *vertex_shader_code = NULL, *fragment_shader_code = NULL;
	asprintf(&vertex_shader_code,
		"#version 450 core\n"
		"\n"
		"layout(location = 0) uniform vec2  half_viewport_size;\n"
		"\n"
		"struct Rect {\n"
		"	uvec4     header;\n"
		"	uvec2[%u] instr;\n"
		"};\n"
		"layout(std430, binding = 0) readonly buffer RectBlock {\n"
		"	Rect rects[];\n"
		"};\n"
		"\n"
		"out vec2      vertex_pos_vs;\n"
		"out vec2      vertex_pos_in_rect_normalized;\n"
		"out vec4      vertex_color;\n"
		"out uvec2[%u] vertex_instr;\n"
		"\n"
		"// We let glDrawArrays() create 6 vertices per rect\n"
		"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
		"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
		"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
		"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
		"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
		"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
		"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
		"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
		"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
		");\n"
		"\n"
		"void main() {\n"
		"	uint rect_index    = uint(gl_VertexID) / 6;\n"
		"	uint vertex_offset = uint(gl_VertexID) %% 6;\n"
		"	\n"
		"	Rect  rect          = rects[rect_index];\n"
		"	uint  layer         = bitfieldExtract(rect.header.x, 24, 8);\n"
		"	vec4  rect_ltrb_vs  = bitfieldExtract(rect.header.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"	vertex_color        = bitfieldExtract(rect.header.zzzz >> uvec4(24, 16, 8, 0), 0, 8) / vec4(255);\n"
		"	\n"
		"	uint min_instr_count = min(%u, %u);\n"
		"	for (uint i = 0; i < min_instr_count; i++)"
		"		vertex_instr[i] = rect.instr[i];\n"
		"	\n"
		"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
		"	vertex_pos_vs         = vec2(rect_ltrb_vs[component_index.x], rect_ltrb_vs[component_index.y]);\n"
		"	// Here the idea is that we get (0,0) for left top and (1,1) for right bottom\n"
		"	vertex_pos_in_rect_normalized = uvec2(equal(component_index, uvec2(2, 3)));\n"
		"	\n"
		"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
		"	vec2 pos_ndc = (vertex_pos_vs / half_viewport_size - 1.0) * axes_flip;\n"
		"	gl_Position = vec4(pos_ndc, 0, 1);\n"
		"	//gl_Layer = int(layer);\n"
		"}\n",
		rect_instr_count, vertex_instr_count, rect_instr_count, vertex_instr_count);
	asprintf(&fragment_shader_code,
		"#version 450 core\n"
		"\n"
		"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
		"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
		"layout(binding =  0) uniform sampler2D      texture00;\n"
		"layout(binding =  1) uniform sampler2D      texture01;\n"
		"layout(binding =  2) uniform sampler2D      texture02;\n"
		"layout(binding =  3) uniform sampler2D      texture03;\n"
		"layout(binding =  4) uniform sampler2D      texture04;\n"
		"layout(binding =  5) uniform sampler2D      texture05;\n"
		"layout(binding =  6) uniform sampler2D      texture06;\n"
		"layout(binding =  7) uniform sampler2D      texture07;\n"
		"layout(binding =  8) uniform sampler2D      texture08;\n"
		"layout(binding =  9) uniform sampler2D      texture09;\n"
		"layout(binding = 10) uniform sampler2D      texture10;\n"
		"layout(binding = 11) uniform sampler2D      texture11;\n"
		"layout(binding = 12) uniform sampler2DArray texture12;\n"
		"layout(binding = 13) uniform sampler2DArray texture13;\n"
		"layout(binding = 14) uniform sampler2DArray texture14;\n"
		"layout(binding = 15) uniform sampler2DArray texture15;\n"
		"\n"
		"vec4 read_texture_unit(uint texture_unit, uint texture_array_index, vec2 texture_coords) {\n"
		"	switch(texture_unit) {\n"
		"		case  0:  return texture(texture00,      texture_coords / textureSize(texture00, 0)                                );\n"
		"		case  1:  return texture(texture01,      texture_coords / textureSize(texture01, 0)                                );\n"
		"		case  2:  return texture(texture02,      texture_coords / textureSize(texture02, 0)                                );\n"
		"		case  3:  return texture(texture03,      texture_coords / textureSize(texture03, 0)                                );\n"
		"		case  4:  return texture(texture04,      texture_coords / textureSize(texture04, 0)                                );\n"
		"		case  5:  return texture(texture05,      texture_coords / textureSize(texture05, 0)                                );\n"
		"		case  6:  return texture(texture06,      texture_coords / textureSize(texture06, 0)                                );\n"
		"		case  7:  return texture(texture07,      texture_coords / textureSize(texture07, 0)                                );\n"
		"		case  8:  return texture(texture08,      texture_coords / textureSize(texture08, 0)                                );\n"
		"		case  9:  return texture(texture09,      texture_coords / textureSize(texture09, 0)                                );\n"
		"		case 10:  return texture(texture10,      texture_coords / textureSize(texture10, 0)                                );\n"
		"		case 11:  return texture(texture11,      texture_coords / textureSize(texture11, 0)                                );\n"
		"		case 12:  return texture(texture12, vec3(texture_coords / textureSize(texture12, 0).xy, float(texture_array_index)));\n"
		"		case 13:  return texture(texture13, vec3(texture_coords / textureSize(texture13, 0).xy, float(texture_array_index)));\n"
		"		case 14:  return texture(texture14, vec3(texture_coords / textureSize(texture14, 0).xy, float(texture_array_index)));\n"
		"		case 15:  return texture(texture15, vec3(texture_coords / textureSize(texture15, 0).xy, float(texture_array_index)));\n"
		"	}\n"
		"}\n"
		"\n"
		"in      vec2      vertex_pos_vs;\n"
		"in      vec2      vertex_pos_in_rect_normalized;\n"
		"in flat vec4      vertex_color;\n"
		"in flat uvec2[%u] vertex_instr;\n"
		"\n"
		"out vec4 fragment_color;\n"
		"\n"
		"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
		"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
		"float sdAxisAlignedRect(vec2 pos, vec2 lt, vec2 rb) {\n"
		"	vec2 d = max(lt-pos, pos-rb);\n"
		"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
		"}\n"
		"\n"
		"void main() {\n"
		"	vec4 content_color = vertex_color;\n"
		"	float coverage = 1, distance = 0;\n"
		"	float pixel_width = dFdx(vertex_pos_vs.x) * 1;  // Use 2.0 for a smoother AA look\n"
		"	\n"
		"	for (uint i = 0; i < vertex_instr.length(); i++) {\n"
		"		uvec2 instr = vertex_instr[i];\n"
		"		uint  type  = bitfieldExtract(instr.x, 28, 4);\n"
		"		\n"
		"		switch (type) {\n"
		"			// empty instruction, just skip it\n"
		"			case 0u:\n"
		"				//i = vertex_instr.length();  // Break the outer loop, halfs performance. Hence commented out.\n"
		"				break;\n"
		"			// glyph             type unit  llll llll  llll tttt  tttt tttt    ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
		"			case 1u: {\n"
		"				uint  texture_unit    = bitfieldExtract(instr.x, 24, 4);\n"
		"				uvec4 tex_coords_ltrb = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"				vec2  tex_coords      = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
		"				coverage = read_texture_unit(texture_unit, 0, tex_coords).r;\n"
		"				} break;\n"
		"			// texture           type unit  llll llll  llll tttt  tttt tttt    iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
		"			case 2u: {\n"
		"				uint  texture_unit        = bitfieldExtract(instr.x, 24, 4);\n"
		"				uvec4 tex_coords_ltrb     = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"				uint  texture_array_index = bitfieldExtract(instr.y, 24, 8);\n"
		"				vec2  tex_coords          = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
		"				content_color = read_texture_unit(texture_unit, texture_array_index, tex_coords);\n"
		"				} break;\n"
		"			// rounded_rect_equ  type ____  llll llll  llll tttt  tttt tttt    cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius\n"
		"			case 3u: {\n"
		"				uvec4 rect_ltrb   = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"				uint  radius      = bitfieldExtract(instr.y, 24, 8);\n"
		"				distance = sdAxisAlignedRect(vertex_pos_vs, rect_ltrb.xy + radius, rect_ltrb.zw - radius) - radius;\n"
		"				coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
		"				} break;\n"
		"			// line_equ          type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy    ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy\n"
		"			case 4u: {\n"
		"				// TODO\n"
		"				} break;\n"
		"			// circle_equ        type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr    xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints, R = outer radius, r = inner radius\n"
		"			case 5u: {\n"
		"				// TODO\n"
		"				} break;\n"
		"			// border            type ____  ssss ssss  ssss eeee  eeee eeee    rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // s and e are signed ints (field start and field end)\n"
		"			case 6u: {\n"
		"				ivec2 border_start_end_dist = ivec2(bitfieldExtract(instr.xx   >> uvec2(0, 12),        0, 12));\n"
		"				vec4  border_color          =       bitfieldExtract(instr.yyyy >> uvec4(24, 16, 8, 0), 0,  8);\n"
		"				float border_coverage       = smoothstep(border_start_end_dist.x, border_start_end_dist.x + pixel_width, distance) * smoothstep(border_start_end_dist.y, border_start_end_dist.y + pixel_width, distance);\n"
		"				content_color = mix(content_color, border_color, border_coverage);\n"
		"				} break;\n"
		"		}\n"
		"	}\n"
		"	\n"
		"	fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
		"}\n",
		vertex_instr_count);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,   vertex_shader_code},
		{ GL_FRAGMENT_SHADER, fragment_shader_code }
	});
	free(vertex_shader_code);
	free(fragment_shader_code);
	
	char* approach_name = NULL;
	asprintf(&approach_name, "ssbo_inlined_instr_%u_%u", rect_instr_count, vertex_instr_count);
	report_approach_start(approach_name);
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBOs with new data (doesn't change here but would with real usecases)
			buffer_reset();
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				pack_rect(0, r->pos, r->background_color);
				
				if (r->has_glyph)
					pack_glyph(r->texture_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
				else {
					if (r->has_texture)
						pack_texture(r->texture_index, r->texture_array_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
					if (r->has_rounded_corners)
						pack_rounded_rect_equ(r->pos, r->corner_radius);
					if (r->has_border)
						pack_border(0, -(r->border_width), r->border_color);
				}
			}
			assert(buffer_curr_index < args->rects_count * buffer_elements_per_rect);
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(rects_ssbo, buffer_size, buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rects_ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit( 0, args->glyph_texture);
						glBindTextureUnit( 1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &rects_ssbo);
	free(buffer);
	free(approach_name);
}

void bench_ssbo_fixed_vertex_to_fragment_buffer(scenario_args_t* args, uint32_t vertex_instr_count) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// Moves bits into a specific part of the value. The arguments start_bit_lsb and bit_count are the same as used in
	// the GLSL function bitfieldExtract() to unpack them (`offset` and `bits`).
	uint32_t bits(uint32_t value, uint32_t start_bit_lsb, uint32_t bit_count) {
		assert(bit_count < 32);  // The bitshift below would zero the value when shifted by 32, hence the assert.
		uint32_t mask = (1 << bit_count) - 1;  // Using x - 1 to flip all lesser significant bits when just one bit is set
		return (value & mask) << start_bit_lsb;
	}
	
	typedef struct {       //        24         16          8          0
		uint32_t header1;  // LLLL LLLL  llll llll  llll tttt  tttt tttt      // L = layer (unused right now), l = left, t = top
		uint32_t header2;  // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb      // r = right, b = bottom
		uint32_t color;    // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa      // r = red, g = green, b = blue, a = alpha
		uint32_t instr;    // oooo oooo  oooo oooo  oooo oooo  cccc cccc      // o = offset, c = count
	} ssbo_instr_combo_rect_t;
	ssbo_instr_combo_rect_t pack_rect(uint32_t layer, rectl_t pos, color_t color, uint32_t instr_offset, uint32_t instr_count) {
		return (ssbo_instr_combo_rect_t){
			.header1 = bits(layer, 24, 8) | bits(pos.l, 12, 12) | bits(pos.t, 0, 12),
			.header2 =                      bits(pos.r, 12, 12) | bits(pos.b, 0, 12),
			.color   = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8),
			.instr   = bits(instr_offset, 8, 24) | bits(instr_count, 0, 8),
		};
	}
	
	typedef struct { uint32_t x, y; } ssbo_instr_combo_instr_t;
	enum { SSBOIL_T_GLYPH = 1, SSBOIL_T_TEXTURE, SSBOIL_T_ROUNDED_RECT_EQU, SSBOIL_T_LINE_EQU, SSBOIL_T_CIRCLE_EQU, SSBOIL_T_BORDER };
	ssbo_instr_combo_instr_t pack_glyph(uint32_t texture_unit, rectl_t tex_coords) {                                                  // glyph
		return (ssbo_instr_combo_instr_t){                                                                                            //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_GLYPH, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),    // type unit  llll llll  llll tttt  tttt tttt
			.y =                                                           bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)     // ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	ssbo_instr_combo_instr_t pack_texture(uint32_t texture_unit, uint32_t texture_array_index, rectl_t tex_coords) {                  // texture
		return (ssbo_instr_combo_instr_t){                                                                                            //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_TEXTURE, 28, 4) | bits(texture_unit, 24, 4) | bits(tex_coords.l, 12, 12) | bits(tex_coords.t, 0, 12),  // type unit  llll llll  llll tttt  tttt tttt
			.y = bits(texture_array_index, 24, 8)                          | bits(tex_coords.r, 12, 12) | bits(tex_coords.b, 0, 12)   // iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb
		};
	}
	ssbo_instr_combo_instr_t pack_rounded_rect_equ(rectl_t rect, uint32_t corner_radius) {                                            // rounded_rect_equ
		return (ssbo_instr_combo_instr_t){                                                                                            //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_ROUNDED_RECT_EQU, 28, 4) | bits(rect.l, 12, 12) | bits(rect.t, 0, 12),                                 // type ____  llll llll  llll tttt  tttt tttt
			.y = bits(corner_radius, 24, 8)             | bits(rect.r, 12, 12) | bits(rect.b, 0, 12)                                  // cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius
		};
	}
	ssbo_instr_combo_instr_t pack_line_equ(vecl_t pos1, vecl_t pos2) {                                                                // line_equ
		return (ssbo_instr_combo_instr_t){                                                                                            //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_LINE_EQU, 28, 4) | bits(pos1.x, 12, 12) | bits(pos1.y, 0, 12),                                         // type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos1 x y
			.y =                                  bits(pos2.x, 12, 12) | bits(pos2.y, 0, 12)                                          // ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy  // pos2 x y
		};
	}
	ssbo_instr_combo_instr_t pack_circle_equ(vecl_t center, uint32_t outer_radius, uint32_t inner_radius) {                           // circle_equ
		return (ssbo_instr_combo_instr_t){                                                                                            //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_CIRCLE_EQU, 28, 4) | bits(outer_radius, 12, 12) | bits(inner_radius, 0, 12),                           // type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr  // R = outer radius, r = inner radius
			.y = bits(center.x, 16, 16) | bits(center.y, 0, 16)                                                                       // xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints
		};
	}
	ssbo_instr_combo_instr_t pack_border(int32_t start_dist, int32_t end_dist, color_t color) {                                       // border
		return (ssbo_instr_combo_instr_t){                                                                                            //   28   24         16    12    8          0
			.x = bits(SSBOIL_T_BORDER, 28, 4) | bits(start_dist, 12, 12) | bits(end_dist, 0, 12),                                     // type ____  ssss ssss  ssss eeee  eeee eeee  // s and e are signed ints (field start and field end)
			.y = bits(color.r, 24, 8) | bits(color.g, 16, 8) | bits(color.b, 8, 8) | bits(color.a, 0, 8)                              // rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // border color
		};
	}
	
	// CPU side buffers. For the experiment we use a fixed instruction buffer with 4 times the count of the rects buffer and an assert below. Good enough for the experiment.
	uint32_t max_instr_count = args->rects_count * 4, rects_buffer_size = args->rects_count * sizeof(ssbo_instr_combo_rect_t), instr_buffer_size = max_instr_count * sizeof(ssbo_instr_combo_instr_t);
	ssbo_instr_combo_rect_t*  rects_cpu_buffer = malloc(rects_buffer_size);
	ssbo_instr_combo_instr_t* instr_cpu_buffer = malloc(instr_buffer_size);
	
	// All the data goes into the SSBOs and we only use an empty VAO for the draw command. The shader then assembles the per-vertex data by itself.
	// An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, rects_ssbo = 0, instr_ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &rects_ssbo);
	glCreateBuffers(1, &instr_ssbo);
	// printf() style placeholders in the shader code: %1$u = vertex_instr_count (see man 3 printf "Format of the format string").
	// Removed those placeholders because that doesn't work on windows with MinGW.
	char *vertex_shader_code = NULL, *fragment_shader_code = NULL;
	asprintf(&vertex_shader_code,
		"#version 450 core\n"
		"\n"
		"layout(location = 0) uniform vec2  half_viewport_size;\n"
		"\n"
		"layout(std430, binding = 0) readonly buffer RectData {\n"
		"	uvec4 rects[];\n"
		"};\n"
		"\n"
		"layout(std430, binding = 1) readonly buffer InstData {\n"
		"	uvec2 instructions[];\n"
		"};\n"
		"\n"
		"out vec2      vertex_pos_vs;\n"
		"out vec2      vertex_pos_in_rect_normalized;\n"
		"out vec4      vertex_color;\n"
		"out uvec2[%u] vertex_instr;\n"
		"\n"
		"// We let glDrawArrays() create 6 vertices per rect\n"
		"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
		"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
		"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
		"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
		"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
		"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
		"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
		"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
		"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
		");\n"
		"\n"
		"void main() {\n"
		"	uint rect_index    = uint(gl_VertexID) / 6;\n"
		"	uint vertex_offset = uint(gl_VertexID) %% 6;\n"
		"	\n"
		"	uvec4 rect_data          = rects[rect_index];\n"
		"	uint  layer              = bitfieldExtract(rect_data.x, 24, 8);\n"
		"	vec4  rect_ltrb_vs       = bitfieldExtract(rect_data.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"	vertex_color             = bitfieldExtract(rect_data.zzzz >> uvec4(24, 16, 8, 0), 0, 8) / vec4(255);\n"
		"	uint vertex_instr_offset = bitfieldExtract(rect_data.w, 8, 24);\n"
		"	uint vertex_instr_count  = bitfieldExtract(rect_data.w, 0, 8);\n"
		"	\n"
		"	// Copy instructions from global memory into vertex-to-fragment buffer\n"
		"	for (uint i = 0; i < %u; i++)\n"
		"		vertex_instr[i] = (i < vertex_instr_count) ? instructions[vertex_instr_offset + i] : uvec2(0);\n"
		"	\n"
		"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
		"	vertex_pos_vs         = vec2(rect_ltrb_vs[component_index.x], rect_ltrb_vs[component_index.y]);\n"
		"	// Here the idea is that we get (0,0) for left top and (1,1) for right bottom\n"
		"	vertex_pos_in_rect_normalized = uvec2(equal(component_index, uvec2(2, 3)));\n"
		"	\n"
		"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
		"	vec2 pos_ndc = (vertex_pos_vs / half_viewport_size - 1.0) * axes_flip;\n"
		"	gl_Position = vec4(pos_ndc, 0, 1);\n"
		"	//gl_Layer = int(layer);\n"
		"}\n",
		vertex_instr_count, vertex_instr_count);
	asprintf(&fragment_shader_code,
		"#version 450 core\n"
		"\n"
		"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
		"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
		"layout(binding =  0) uniform sampler2D      texture00;\n"
		"layout(binding =  1) uniform sampler2D      texture01;\n"
		"layout(binding =  2) uniform sampler2D      texture02;\n"
		"layout(binding =  3) uniform sampler2D      texture03;\n"
		"layout(binding =  4) uniform sampler2D      texture04;\n"
		"layout(binding =  5) uniform sampler2D      texture05;\n"
		"layout(binding =  6) uniform sampler2D      texture06;\n"
		"layout(binding =  7) uniform sampler2D      texture07;\n"
		"layout(binding =  8) uniform sampler2D      texture08;\n"
		"layout(binding =  9) uniform sampler2D      texture09;\n"
		"layout(binding = 10) uniform sampler2D      texture10;\n"
		"layout(binding = 11) uniform sampler2D      texture11;\n"
		"layout(binding = 12) uniform sampler2DArray texture12;\n"
		"layout(binding = 13) uniform sampler2DArray texture13;\n"
		"layout(binding = 14) uniform sampler2DArray texture14;\n"
		"layout(binding = 15) uniform sampler2DArray texture15;\n"
		"\n"
		"vec4 read_texture_unit(uint texture_unit, uint texture_array_index, vec2 texture_coords) {\n"
		"	switch(texture_unit) {\n"
		"		case  0:  return texture(texture00,      texture_coords / textureSize(texture00, 0)                                );\n"
		"		case  1:  return texture(texture01,      texture_coords / textureSize(texture01, 0)                                );\n"
		"		case  2:  return texture(texture02,      texture_coords / textureSize(texture02, 0)                                );\n"
		"		case  3:  return texture(texture03,      texture_coords / textureSize(texture03, 0)                                );\n"
		"		case  4:  return texture(texture04,      texture_coords / textureSize(texture04, 0)                                );\n"
		"		case  5:  return texture(texture05,      texture_coords / textureSize(texture05, 0)                                );\n"
		"		case  6:  return texture(texture06,      texture_coords / textureSize(texture06, 0)                                );\n"
		"		case  7:  return texture(texture07,      texture_coords / textureSize(texture07, 0)                                );\n"
		"		case  8:  return texture(texture08,      texture_coords / textureSize(texture08, 0)                                );\n"
		"		case  9:  return texture(texture09,      texture_coords / textureSize(texture09, 0)                                );\n"
		"		case 10:  return texture(texture10,      texture_coords / textureSize(texture10, 0)                                );\n"
		"		case 11:  return texture(texture11,      texture_coords / textureSize(texture11, 0)                                );\n"
		"		case 12:  return texture(texture12, vec3(texture_coords / textureSize(texture12, 0).xy, float(texture_array_index)));\n"
		"		case 13:  return texture(texture13, vec3(texture_coords / textureSize(texture13, 0).xy, float(texture_array_index)));\n"
		"		case 14:  return texture(texture14, vec3(texture_coords / textureSize(texture14, 0).xy, float(texture_array_index)));\n"
		"		case 15:  return texture(texture15, vec3(texture_coords / textureSize(texture15, 0).xy, float(texture_array_index)));\n"
		"	}\n"
		"}\n"
		"\n"
		"in      vec2      vertex_pos_vs;\n"
		"in      vec2      vertex_pos_in_rect_normalized;\n"
		"in flat vec4      vertex_color;\n"
		"in flat uvec2[%u] vertex_instr;\n"
		"\n"
		"out vec4 fragment_color;\n"
		"\n"
		"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
		"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
		"float sdAxisAlignedRect(vec2 pos, vec2 lt, vec2 rb) {\n"
		"	vec2 d = max(lt-pos, pos-rb);\n"
		"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
		"}\n"
		"\n"
		"void main() {\n"
		"	vec4 content_color = vertex_color;\n"
		"	float coverage = 1, distance = 0;\n"
		"	float pixel_width = dFdx(vertex_pos_vs.x) * 1;  // Use 2.0 for a smoother AA look\n"
		"	\n"
		"	for (uint i = 0; i < vertex_instr.length(); i++) {\n"
		"		uvec2 instr = vertex_instr[i];\n"
		"		uint  type  = bitfieldExtract(instr.x, 28, 4);\n"
		"		\n"
		"		switch (type) {\n"
		"			// empty instruction, just skip it\n"
		"			case 0u:\n"
		"				//i = vertex_instr.length();  // Break the outer loop, halfs performance. Hence commented out.\n"
		"				break;\n"
		"			// glyph             type unit  llll llll  llll tttt  tttt tttt    ____ ____  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
		"			case 1u: {\n"
		"				uint  texture_unit    = bitfieldExtract(instr.x, 24, 4);\n"
		"				uvec4 tex_coords_ltrb = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"				vec2  tex_coords      = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
		"				coverage = read_texture_unit(texture_unit, 0, tex_coords).r;\n"
		"				} break;\n"
		"			// texture           type unit  llll llll  llll tttt  tttt tttt    iiii iiii  rrrr rrrr  rrrr bbbb  bbbb bbbb\n"
		"			case 2u: {\n"
		"				uint  texture_unit        = bitfieldExtract(instr.x, 24, 4);\n"
		"				uvec4 tex_coords_ltrb     = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"				uint  texture_array_index = bitfieldExtract(instr.y, 24, 8);\n"
		"				vec2  tex_coords          = tex_coords_ltrb.xy + vertex_pos_in_rect_normalized * vec2(tex_coords_ltrb.zw - tex_coords_ltrb.xy);\n"
		"				content_color = read_texture_unit(texture_unit, texture_array_index, tex_coords);\n"
		"				} break;\n"
		"			// rounded_rect_equ  type ____  llll llll  llll tttt  tttt tttt    cccc cccc  rrrr rrrr  rrrr bbbb  bbbb bbbb  // c is corner_radius\n"
		"			case 3u: {\n"
		"				uvec4 rect_ltrb   = bitfieldExtract(instr.xxyy >> uvec4(12, 0, 12, 0), 0, 12);\n"
		"				uint  radius      = bitfieldExtract(instr.y, 24, 8);\n"
		"				distance = sdAxisAlignedRect(vertex_pos_vs, rect_ltrb.xy + radius, rect_ltrb.zw - radius) - radius;\n"
		"				coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
		"				} break;\n"
		"			// line_equ          type ____  xxxx xxxx  xxxx yyyy  yyyy yyyy    ____ ____  xxxx xxxx  xxxx yyyy  yyyy yyyy\n"
		"			case 4u: {\n"
		"				// TODO\n"
		"				} break;\n"
		"			// circle_equ        type ____  RRRR RRRR  RRRR rrrr  rrrr rrrr    xxxx xxxx  xxxx xxxx  yyyy yyyy  yyyy yyyy  // x and y are signed ints, R = outer radius, r = inner radius\n"
		"			case 5u: {\n"
		"				// TODO\n"
		"				} break;\n"
		"			// border            type ____  ssss ssss  ssss eeee  eeee eeee    rrrr rrrr  gggg gggg  bbbb bbbb  aaaa aaaa  // s and e are signed ints (field start and field end)\n"
		"			case 6u: {\n"
		"				ivec2 border_start_end_dist = ivec2(bitfieldExtract(instr.xx   >> uvec2(0, 12),        0, 12));\n"
		"				vec4  border_color          =       bitfieldExtract(instr.yyyy >> uvec4(24, 16, 8, 0), 0,  8);\n"
		"				float border_coverage       = smoothstep(border_start_end_dist.x, border_start_end_dist.x + pixel_width, distance) * smoothstep(border_start_end_dist.y, border_start_end_dist.y + pixel_width, distance);\n"
		"				content_color = mix(content_color, border_color, border_coverage);\n"
		"				} break;\n"
		"		}\n"
		"	}\n"
		"	\n"
		"	fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
		"}\n",
		vertex_instr_count);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,   vertex_shader_code},
		{ GL_FRAGMENT_SHADER, fragment_shader_code }
	});
	free(vertex_shader_code);
	free(fragment_shader_code);
	
	char* approach_name = NULL;
	asprintf(&approach_name, "ssbo_instr_combo_%u", vertex_instr_count);
	report_approach_start(approach_name);
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBOs with new data (doesn't change here but would with real usecases)
			uint32_t instr_count = 0;
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				uint32_t instr_offset = instr_count;
				if (r->has_glyph)
					instr_cpu_buffer[instr_count++] = pack_glyph(r->texture_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
				else {
					if (r->has_texture)
						instr_cpu_buffer[instr_count++] = pack_texture(r->texture_index, r->texture_array_index, (rectl_t){r->texture_coords.l, r->texture_coords.t, r->texture_coords.r, r->texture_coords.b});
					if (r->has_rounded_corners)
						instr_cpu_buffer[instr_count++] = pack_rounded_rect_equ(r->pos, r->corner_radius);
					if (r->has_border)
						instr_cpu_buffer[instr_count++] = pack_border(0, -(r->border_width), r->border_color);
				}
				rects_cpu_buffer[i] = pack_rect(0, r->pos, r->background_color, instr_offset, instr_count - instr_offset);
				assert(instr_count - instr_offset < 10);
				assert(instr_count <= max_instr_count);
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(rects_ssbo, rects_buffer_size, rects_cpu_buffer, GL_STREAM_DRAW);
			glNamedBufferData(instr_ssbo, instr_count * sizeof(instr_cpu_buffer[0]), instr_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rects_ssbo);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, instr_ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit( 0, args->glyph_texture);
						glBindTextureUnit( 1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &instr_ssbo);
	glDeleteBuffers(1, &rects_ssbo);
	free(instr_cpu_buffer);
	free(rects_cpu_buffer);
	free(approach_name);
}



void bench_one_ssbo_ext_no_sdf(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	enum one_ssbo_flags_t { ONE_SSBO_USE_TEXTURE = (1 << 0), ONE_SSBO_USE_BORDER = (1 << 1), ONE_SSBO_GLYPH = (1 << 2), ONE_SSBO_SDF_FUNCS = (1 << 3) };
	typedef struct {
		uint8_t  flags, layer, tex_unit, tex_array_index;
		color_t  base_color;
		uint16_t left, top;
		uint16_t right, bottom;
		
		uint16_t tex_left, tex_top;
		uint16_t tex_right, tex_bottom;
		color_t  border_color;
		uint8_t  border_width, corner_radius, padding1, padding2;
	} one_ssbo_rect_t;
	one_ssbo_rect_t* rects_cpu_buffer = malloc(args->rects_count * sizeof(rects_cpu_buffer[0]));
	
	// All the data goes into the SSBO and we only use an empty VAO for the draw command. The shader then assembles the
	// per-vertex data by itself. An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &ssbo);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"\n"
			"struct rect_t {\n"
			"	uint    packed_flags_layer_tex_unit_tex_array_index;\n"
			"	uint    packed_base_color;\n"
			"	uvec2   packed_ltrb;\n"
			"	uvec2   packed_tex_ltrb;\n"
			"	uint    packed_border_color;\n"
			"	uint    packed_border_width_corner_radius;\n"
			"};\n"
			"layout(std430, binding = 0) readonly buffer rect_buffer {\n"
			"	rect_t rects[];\n"
			"};\n"
			"\n"
			"out uint  vertex_flags;\n"
			"out uint  vertex_texture_unit;\n"
			"out uint  vertex_texture_array_index;\n"
			"out vec4  vertex_base_color;\n"
			"out vec2  vertex_pos;\n"
			"out vec2  vertex_tex_coords;\n"
			"out vec4  vertex_border_color;\n"
			"out float vertex_border_width;\n"
			"out float vertex_corner_radius;\n"
			"out vec4  vertex_rect_tlrb;\n"
			"\n"
			"// We let glDrawArrays() create 6 vertices per rect\n"
			"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
			"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
			"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
			"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
			"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
			"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
			"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
			");\n"
			"\n"
			"void main() {\n"
			"	uint rect_index    = uint(gl_VertexID) / 6;\n"
			"	uint vertex_offset = uint(gl_VertexID) % 6;\n"
			"	\n"
			"	vertex_flags               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  0, 8);\n"
			"	uint   layer               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  8, 8);\n"
			"	vertex_texture_unit        = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 16, 8);\n"
			"	vertex_texture_array_index = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 24, 8);\n"
			"	vertex_base_color          = bitfieldExtract(uvec4(rects[rect_index].packed_base_color)   >> uvec4(0, 8, 16, 24), 0, 8) / vec4(255);\n"
			"	vertex_border_color        = bitfieldExtract(uvec4(rects[rect_index].packed_border_color) >> uvec4(0, 8, 16, 24), 0, 8) / vec4(255);\n"
			"	vertex_border_width        = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius, 0, 8);\n"
			"	vertex_corner_radius       = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius, 8, 8);\n"
			"	\n"
			"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
			"	vertex_rect_tlrb      = bitfieldExtract(rects[rect_index].packed_ltrb.xxyy     >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vertex_pos            = vec2(vertex_rect_tlrb[component_index.x], vertex_rect_tlrb[component_index.y]);\n"
			"	vec4 tex_ltrb         = bitfieldExtract(rects[rect_index].packed_tex_ltrb.xxyy >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vertex_tex_coords     = vec2(tex_ltrb[component_index.x], tex_ltrb[component_index.y]);\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_ndc   = (vertex_pos / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"	//gl_Layer = int(layer);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"const uint RF_USE_TEXTURE = (1 << 0), RF_USE_BORDER = (1 << 1), RF_GLYPH = (1 << 2), RF_SDF_FUNCS = (1 << 3); // enum rect_flags_t;\n"
			"in flat uint  vertex_flags;\n"
			"in flat uint  vertex_texture_unit;\n"
			"in flat uint  vertex_texture_array_index;\n"
			"in flat vec4  vertex_base_color;\n"
			"in      vec2  vertex_pos;\n"
			"in      vec2  vertex_tex_coords;\n"
			"in flat vec4  vertex_border_color;\n"
			"in flat float vertex_border_width;\n"
			"in flat float vertex_corner_radius;\n"
			"in flat vec4  vertex_rect_tlrb;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_base_color;\n"
			"	if ((vertex_flags & RF_USE_TEXTURE) != 0) {\n"
			"		switch(vertex_texture_unit) {\n"
			"			case  0:  content_color = texture(texture00, vertex_tex_coords / textureSize(texture00, 0));  break;\n"
			"			case  1:  content_color = texture(texture01, vertex_tex_coords / textureSize(texture01, 0));  break;\n"
			"			case  2:  content_color = texture(texture02, vertex_tex_coords / textureSize(texture02, 0));  break;\n"
			"			case  3:  content_color = texture(texture03, vertex_tex_coords / textureSize(texture03, 0));  break;\n"
			"			case  4:  content_color = texture(texture04, vertex_tex_coords / textureSize(texture04, 0));  break;\n"
			"			case  5:  content_color = texture(texture05, vertex_tex_coords / textureSize(texture05, 0));  break;\n"
			"			case  6:  content_color = texture(texture06, vertex_tex_coords / textureSize(texture06, 0));  break;\n"
			"			case  7:  content_color = texture(texture07, vertex_tex_coords / textureSize(texture07, 0));  break;\n"
			"			case  8:  content_color = texture(texture08, vertex_tex_coords / textureSize(texture08, 0));  break;\n"
			"			case  9:  content_color = texture(texture09, vertex_tex_coords / textureSize(texture09, 0));  break;\n"
			"			case 10:  content_color = texture(texture10, vertex_tex_coords / textureSize(texture10, 0));  break;\n"
			"			case 11:  content_color = texture(texture11, vertex_tex_coords / textureSize(texture11, 0));  break;\n"
			"			case 12:  content_color = texture(texture12, vec3(vertex_tex_coords / textureSize(texture12, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 13:  content_color = texture(texture13, vec3(vertex_tex_coords / textureSize(texture13, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 14:  content_color = texture(texture14, vec3(vertex_tex_coords / textureSize(texture14, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 15:  content_color = texture(texture15, vec3(vertex_tex_coords / textureSize(texture15, 0).xy, vertex_texture_array_index));  break;\n"
			"		}\n"
			"	}\n"
			"	if ((vertex_flags & RF_GLYPH) != 0) {\n"
			"		fragment_color = vec4(vertex_base_color.rgb, vertex_base_color.a * content_color.r);\n"
			"	} else if ((vertex_flags & RF_USE_BORDER) != 0) {\n"
			"		float pixel_width = dFdx(vertex_pos.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		float distance    = sdAxisAlignedRect(vertex_pos, vertex_rect_tlrb.xy + vertex_corner_radius, vertex_rect_tlrb.zw - vertex_corner_radius) - vertex_corner_radius;\n"
			"		\n"
			"		float border_inner_transition = 1 - smoothstep(-vertex_border_width, -(vertex_border_width + pixel_width), distance);\n"
			"		content_color = vec4(mix(content_color.rgb, vertex_border_color.rgb, border_inner_transition * vertex_border_color.a), content_color.a);\n"
			"		\n"
			"		float coverage = 1 - smoothstep(-pixel_width, 0, distance);"
			"		fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
			"	} else {\n"
			"		fragment_color = content_color;\n"
			"	}\n"
			"}\n"
		}
	});
	
	report_approach_start("one_ssbo_ext_no_sdf");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBO with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				rects_cpu_buffer[i] = (one_ssbo_rect_t){
					.flags = ((r->has_texture || r->has_texture_array) ? ONE_SSBO_USE_TEXTURE : 0) | ((r->has_border || r->has_rounded_corners) ? ONE_SSBO_USE_BORDER : 0) | (r->has_glyph ? ONE_SSBO_GLYPH : 0),
					.layer = 0, .tex_unit = r->texture_index, .tex_array_index = r->texture_array_index,
					.base_color = r->background_color,
					.left = r->pos.l, .top = r->pos.t, .right = r->pos.r, .bottom = r->pos.b,
					
					.tex_left = r->texture_coords.l, .tex_top = r->texture_coords.t, .tex_right = r->texture_coords.r, .tex_bottom = r->texture_coords.b,
					.border_color = r->border_color, .border_width = r->border_width, .corner_radius = r->corner_radius
				};
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(ssbo, args->rects_count * sizeof(rects_cpu_buffer[0]), rects_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit(0, args->glyph_texture);
						glBindTextureUnit(1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &ssbo);
	free(rects_cpu_buffer);
}

void bench_one_ssbo_ext_sdf_list(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	enum one_ssbo_flags_t { ONE_SSBO_USE_TEXTURE = (1 << 0), ONE_SSBO_USE_BORDER = (1 << 1), ONE_SSBO_GLYPH = (1 << 2), ONE_SSBO_SDF_FUNCS = (1 << 3) };
	typedef struct {
		uint8_t  flags, layer, tex_unit, tex_array_index;
		color_t  base_color;
		uint16_t left, top;
		uint16_t right, bottom;
		
		uint16_t tex_left, tex_top;
		uint16_t tex_right, tex_bottom;
		color_t  border_color;
		uint8_t  border_width, corner_radius, padding1, padding2;
		
		uint16_t sdf_functions[4];
		vecs_t   points[6];
	} one_ssbo_rect_t;
	one_ssbo_rect_t* rects_cpu_buffer = malloc(args->rects_count * sizeof(rects_cpu_buffer[0]));
	
	// Moves bits into a specific part of the value. The arguments start_bit_lsb and bit_count are the same as used in
	// the GLSL function bitfieldExtract() to unpack them (`offset` and `bits`).
	uint32_t bits(uint32_t value, uint32_t start_bit_lsb, uint32_t bit_count) {
		assert(bit_count < 32);  // The bitshift below would zero the value when shifted by 32, hence the assert.
		uint32_t mask = (1 << bit_count) - 1;  // Using x - 1 to flip all lesser significant bits when just one bit is set
		return (value & mask) << start_bit_lsb;
	}
	
	enum { SDF_NONE = 0, SDF_ROUNDED_RECT };
	enum { SDF_OP_REPLACE = 0, SDF_OP_UNION, SDF_OP_SUBSTRACTION, SDF_OP_INTERSECTION };
	uint16_t pack_sdf_function(uint32_t type, uint32_t operation, uint32_t a, uint32_t b) {  // Layout: ____ ttto  ooaa abbb
		return bits(type, 9, 3) | bits(operation, 6, 3) | bits(a, 3, 3) | bits(b, 0, 3);
	}
	
	// All the data goes into the SSBO and we only use an empty VAO for the draw command. The shader then assembles the
	// per-vertex data by itself. An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &ssbo);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"\n"
			"struct rect_t {\n"
			"	uint    packed_flags_layer_tex_unit_tex_array_index;\n"
			"	uint    packed_base_color;\n"
			"	uvec2   packed_ltrb;\n"
			"	uvec2   packed_tex_ltrb;\n"
			"	uint    packed_border_color;\n"
			"	uint    packed_border_width_corner_radius;\n"
			"	uvec2   packed_sdf_functions;\n"
			"	uint[6] points;\n"
			"};\n"
			"layout(std430, binding = 0) readonly buffer rect_buffer {\n"
			"	rect_t rects[];\n"
			"};\n"
			"\n"
			"out uint  vertex_flags;\n"
			"out uint  vertex_texture_unit;\n"
			"out uint  vertex_texture_array_index;\n"
			"out vec4  vertex_base_color;\n"
			"out vec2  vertex_pos;\n"
			"out vec2  vertex_tex_coords;\n"
			"out vec4  vertex_border_color;\n"
			"out float vertex_border_width;\n"
			"out float vertex_corner_radius;\n"
			"out uvec4[4] vertex_sdf_functions;\n"
			"out vec2[6]  vertex_points;\n"
			"\n"
			"// We let glDrawArrays() create 6 vertices per rect\n"
			"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
			"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
			"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
			"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
			"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
			"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
			"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
			");\n"
			"\n"
			"void main() {\n"
			"	uint rect_index    = uint(gl_VertexID) / 6;\n"
			"	uint vertex_offset = uint(gl_VertexID) % 6;\n"
			"	\n"
			"	vertex_flags               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  0, 8);\n"
			"	uint   layer               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  8, 8);\n"
			"	vertex_texture_unit        = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 16, 8);\n"
			"	vertex_texture_array_index = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 24, 8);\n"
			"	vertex_base_color          = bitfieldExtract(uvec4(rects[rect_index].packed_base_color)   >> uvec4(0, 8, 16, 24), 0, 8) / vec4(255);\n"
			"	vertex_border_color        = bitfieldExtract(uvec4(rects[rect_index].packed_border_color) >> uvec4(0, 8, 16, 24), 0, 8) / vec4(255);\n"
			"	vertex_border_width        = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius, 0, 8);\n"
			"	vertex_corner_radius       = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius, 8, 8);\n"
			"	\n"
			"	uvec4 sdf_functions     = bitfieldExtract(rects[rect_index].packed_sdf_functions.xxyy >> uvec4(16, 0, 16, 0), 0, 16);\n"
			"	vertex_sdf_functions[0] = bitfieldExtract(sdf_functions.xxxx >> uvec4(9, 6, 3, 0), 0, 3);\n"
			"	vertex_sdf_functions[1] = bitfieldExtract(sdf_functions.yyyy >> uvec4(9, 6, 3, 0), 0, 3);\n"
			"	vertex_sdf_functions[2] = bitfieldExtract(sdf_functions.zzzz >> uvec4(9, 6, 3, 0), 0, 3);\n"
			"	vertex_sdf_functions[3] = bitfieldExtract(sdf_functions.wwww >> uvec4(9, 6, 3, 0), 0, 3);\n"
			"	\n"
			"	for (uint i = 0; i < rects[rect_index].points.length(); i++)\n"
			"		vertex_points[i] = vec2(ivec2(uvec2(rects[rect_index].points[i]) >> uvec2(0, 16) & 0xffffu));\n"
			"	\n"
			"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
			"	vec4  rect_ltrb       = bitfieldExtract(rects[rect_index].packed_ltrb.xxyy     >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vec4  tex_ltrb        = bitfieldExtract(rects[rect_index].packed_tex_ltrb.xxyy >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vertex_pos            = vec2(rect_ltrb[component_index.x], rect_ltrb[component_index.y]);\n"
			"	vertex_tex_coords     = vec2(tex_ltrb[component_index.x], tex_ltrb[component_index.y]);\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_ndc   = (vertex_pos / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"	//gl_Layer = int(layer);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"const uint RF_USE_TEXTURE = (1 << 0), RF_USE_BORDER = (1 << 1), RF_GLYPH = (1 << 2), RF_SDF_FUNCS = (1 << 3); // enum rect_flags_t;\n"
			"in flat uint  vertex_flags;\n"
			"in flat uint  vertex_texture_unit;\n"
			"in flat uint  vertex_texture_array_index;\n"
			"in flat vec4  vertex_base_color;\n"
			"in      vec2  vertex_pos;\n"
			"in      vec2  vertex_tex_coords;\n"
			"in flat vec4  vertex_border_color;\n"
			"in flat float vertex_border_width;\n"
			"in flat float vertex_corner_radius;\n"
			"in flat uvec4[4] vertex_sdf_functions;\n"
			"in flat vec2[6]  vertex_points;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_base_color;\n"
			"	if ((vertex_flags & RF_USE_TEXTURE) != 0) {\n"
			"		switch(vertex_texture_unit) {\n"
			"			case  0:  content_color = texture(texture00, vertex_tex_coords / textureSize(texture00, 0));  break;\n"
			"			case  1:  content_color = texture(texture01, vertex_tex_coords / textureSize(texture01, 0));  break;\n"
			"			case  2:  content_color = texture(texture02, vertex_tex_coords / textureSize(texture02, 0));  break;\n"
			"			case  3:  content_color = texture(texture03, vertex_tex_coords / textureSize(texture03, 0));  break;\n"
			"			case  4:  content_color = texture(texture04, vertex_tex_coords / textureSize(texture04, 0));  break;\n"
			"			case  5:  content_color = texture(texture05, vertex_tex_coords / textureSize(texture05, 0));  break;\n"
			"			case  6:  content_color = texture(texture06, vertex_tex_coords / textureSize(texture06, 0));  break;\n"
			"			case  7:  content_color = texture(texture07, vertex_tex_coords / textureSize(texture07, 0));  break;\n"
			"			case  8:  content_color = texture(texture08, vertex_tex_coords / textureSize(texture08, 0));  break;\n"
			"			case  9:  content_color = texture(texture09, vertex_tex_coords / textureSize(texture09, 0));  break;\n"
			"			case 10:  content_color = texture(texture10, vertex_tex_coords / textureSize(texture10, 0));  break;\n"
			"			case 11:  content_color = texture(texture11, vertex_tex_coords / textureSize(texture11, 0));  break;\n"
			"			case 12:  content_color = texture(texture12, vec3(vertex_tex_coords / textureSize(texture12, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 13:  content_color = texture(texture13, vec3(vertex_tex_coords / textureSize(texture13, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 14:  content_color = texture(texture14, vec3(vertex_tex_coords / textureSize(texture14, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 15:  content_color = texture(texture15, vec3(vertex_tex_coords / textureSize(texture15, 0).xy, vertex_texture_array_index));  break;\n"
			"		}\n"
			"	}\n"
			"	if ((vertex_flags & RF_GLYPH) != 0) {\n"
			"		fragment_color = vec4(vertex_base_color.rgb, vertex_base_color.a * content_color.r);\n"
			"	} else {\n"
			"		float distance = -1;\n"
			"		float pixel_width = dFdx(vertex_pos.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		\n"
			"		for (uint i = 0; i < vertex_sdf_functions.length(); i++) {\n"
			"			uvec4 sdf_func = vertex_sdf_functions[i];\n"
			"			switch(sdf_func.x) {\n"
			"				case 0u:  // Empty, ignore\n"
			"					break;\n"
			"				case 1u:  // Rounded rect\n"
			"					distance = sdAxisAlignedRect(vertex_pos, vertex_points[sdf_func.z] + vertex_corner_radius, vertex_points[sdf_func.w] - vertex_corner_radius) - vertex_corner_radius;\n"
			"					break;\n"
			"				//case 1u:\n"
			"				//	\n"
			"				//	break;\n"
			"				//case 1u:\n"
			"				//	\n"
			"				//	break;\n"
			"				//case 1u:\n"
			"				//	\n"
			"				//	break;\n"
			"				//case 1u:\n"
			"				//	\n"
			"				//	break;\n"
			"				//case 1u:\n"
			"				//	\n"
			"				//	break;\n"
			"				//case 1u:\n"
			"				//	\n"
			"				//	break;\n"
			"			}\n"
			"		}\n"
			"		\n"
			"		float coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
			"		\n"
			"		if ((vertex_flags & RF_USE_BORDER) != 0) {\n"
			"			float border_inner_transition = 1 - smoothstep(-vertex_border_width, -(vertex_border_width + pixel_width), distance);\n"
			"			content_color = vec4(mix(content_color.rgb, vertex_border_color.rgb, border_inner_transition * vertex_border_color.a), content_color.a);\n"
			"		}\n"
			"		\n"
			"		fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
			"	}\n"
			"}\n"
		}
	});
	
	report_approach_start("one_ssbo_ext_sdf_list");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBO with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				rects_cpu_buffer[i] = (one_ssbo_rect_t){
					.flags = ((r->has_texture || r->has_texture_array) ? ONE_SSBO_USE_TEXTURE : 0) | ((r->has_border || r->has_rounded_corners) ? ONE_SSBO_USE_BORDER : 0) | (r->has_glyph ? ONE_SSBO_GLYPH : 0),
					.layer = 0, .tex_unit = r->texture_index, .tex_array_index = r->texture_array_index,
					.base_color = r->background_color,
					.left = r->pos.l, .top = r->pos.t, .right = r->pos.r, .bottom = r->pos.b,
					
					.tex_left = r->texture_coords.l, .tex_top = r->texture_coords.t, .tex_right = r->texture_coords.r, .tex_bottom = r->texture_coords.b,
					.border_color = r->border_color, .border_width = r->border_width, .corner_radius = r->corner_radius
				};
				
				if (r->corner_radius > 0) {
					rects_cpu_buffer[i].points[0] = vecs(r->pos.l, r->pos.t);
					rects_cpu_buffer[i].points[1] = vecs(r->pos.r, r->pos.b);
					rects_cpu_buffer[i].sdf_functions[0] = pack_sdf_function(SDF_ROUNDED_RECT, SDF_OP_REPLACE, 0, 1);
					rects_cpu_buffer[i].flags |= ONE_SSBO_SDF_FUNCS;
				}
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(ssbo, args->rects_count * sizeof(rects_cpu_buffer[0]), rects_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit(0, args->glyph_texture);
						glBindTextureUnit(1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &ssbo);
	free(rects_cpu_buffer);
}

void bench_one_ssbo_ext_one_sdf(scenario_args_t* args, bool use_builtin_scenario) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	enum { ONE_SSBO_USE_TEXTURE = (1 << 0), ONE_SSBO_USE_BORDER = (1 << 1), ONE_SSBO_GLYPH = (1 << 2) };
	enum { SDF_NONE = 0, SDF_ROUNDED_RECT, SDF_CIRCLE, SDF_INV_CIRCLE, SDF_POLYGON, SDF_TEXTURE, SDF_CIRCLE_SEGMENT, SDF_RECT };
	typedef struct {
		uint8_t  flags, layer, tex_unit, tex_array_index;
		color_t  base_color;
		uint16_t left, top;
		uint16_t right, bottom;
		
		uint16_t tex_left, tex_top;
		uint16_t tex_right, tex_bottom;
		color_t  border_color;
		uint8_t  border_width, corner_radius, sdf_type, point_count;
		
		vecs_t   points[8];
	} one_ssbo_rect_t;
	one_ssbo_rect_t* rects_cpu_buffer = malloc(args->rects_count * sizeof(rects_cpu_buffer[0]));
	
	// All the data goes into the SSBO and we only use an empty VAO for the draw command. The shader then assembles the
	// per-vertex data by itself. An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &ssbo);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"\n"
			"struct rect_t {\n"
			"	uint    packed_flags_layer_tex_unit_tex_array_index;\n"
			"	uint    packed_base_color;\n"
			"	uvec2   packed_ltrb;\n"
			"	uvec2   packed_tex_ltrb;\n"
			"	uint    packed_border_color;\n"
			"	uint    packed_border_width_corner_radius_sdf_type_point_count;\n"
			"	uint[8] points;\n"
			"};\n"
			"layout(std430, binding = 0) readonly buffer rect_buffer {\n"
			"	rect_t rects[];\n"
			"};\n"
			"\n"
			"out uint    vertex_flags;\n"
			"out uint    vertex_texture_unit;\n"
			"out uint    vertex_texture_array_index;\n"
			"out vec4    vertex_base_color;\n"
			"out vec2    vertex_pos;\n"
			"out vec2    vertex_tex_coords;\n"
			"out vec4    vertex_border_color;\n"
			"out float   vertex_border_width;\n"
			"out float   vertex_corner_radius;\n"
			"out uint    vertex_sdf_type;\n"
			"out uint    vertex_point_count;\n"
			"out vec2[8] vertex_points;\n"
			"\n"
			"// We let glDrawArrays() create 6 vertices per rect\n"
			"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
			"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
			"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
			"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
			"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
			"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
			"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
			");\n"
			"\n"
			"void main() {\n"
			"	uint rect_index    = uint(gl_VertexID) / 6;\n"
			"	uint vertex_offset = uint(gl_VertexID) % 6;\n"
			"	\n"
			"	vertex_flags               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  0, 8);\n"
			"	uint   layer               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  8, 8);\n"
			"	vertex_texture_unit        = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 16, 8);\n"
			"	vertex_texture_array_index = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 24, 8);\n"
			"	vertex_base_color          = unpackUnorm4x8(rects[rect_index].packed_base_color);\n"
			"	vertex_border_color        = unpackUnorm4x8(rects[rect_index].packed_border_color);\n"
			"	vertex_border_width        = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count,  0, 8);\n"
			"	vertex_corner_radius       = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count,  8, 8);\n"
			"	vertex_sdf_type            = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count, 16, 8);\n"
			"	vertex_point_count         = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count, 24, 8);\n"
			"	\n"
			"	for (uint i = 0; i < rects[rect_index].points.length(); i++)\n"
			"		vertex_points[i] = vec2(ivec2(uvec2(rects[rect_index].points[i]) >> uvec2(0, 16) & 0xffffu));\n"
			"	\n"
			"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
			"	vec4  rect_ltrb       = bitfieldExtract(rects[rect_index].packed_ltrb.xxyy     >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vec4  tex_ltrb        = bitfieldExtract(rects[rect_index].packed_tex_ltrb.xxyy >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vertex_pos            = vec2(rect_ltrb[component_index.x], rect_ltrb[component_index.y]);\n"
			"	vertex_tex_coords     = vec2(tex_ltrb[component_index.x], tex_ltrb[component_index.y]);\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_ndc   = (vertex_pos / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"	//gl_Layer = int(layer);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"const uint RF_USE_TEXTURE = (1 << 0), RF_USE_BORDER = (1 << 1), RF_GLYPH = (1 << 2); // enum rect_flags_t;\n"
			"in flat uint    vertex_flags;\n"
			"in flat uint    vertex_texture_unit;\n"
			"in flat uint    vertex_texture_array_index;\n"
			"in flat vec4    vertex_base_color;\n"
			"in      vec2    vertex_pos;\n"
			"in      vec2    vertex_tex_coords;\n"
			"in flat vec4    vertex_border_color;\n"
			"in flat float   vertex_border_width;\n"
			"in flat float   vertex_corner_radius;\n"
			"in flat uint    vertex_sdf_type;\n"
			"in flat uint    vertex_point_count;\n"
			"in flat vec2[8] vertex_points;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"// 'Polygon - exact' function from https://iquilezles.org/articles/distfunctions2d/\n"
			"// Slightly modified to make it work with GLSL 4.5\n"
			"float sdPolygon(in uint N, in vec2[8] v, in vec2 p) {\n"
			"	float d = dot(p-v[0],p-v[0]);\n"
			"	float s = 1.0;\n"
			"	for(uint i=0, j=N-1; i<N; j=i, i++) {\n"
			"		vec2 e = v[j] - v[i];\n"
			"		vec2 w =    p - v[i];\n"
			"		vec2 b = w - e*clamp( dot(w,e)/dot(e,e), 0.0, 1.0 );\n"
			"		d = min( d, dot(b,b) );\n"
			"		bvec3 c = bvec3(p.y>=v[i].y,p.y<v[j].y,e.x*w.y>e.y*w.x);\n"
			"		if( all(c) || all(not(c)) ) s*=-1.0;  \n"
			"	}\n"
			"	return s*sqrt(d);\n"
			"}\n"
			"\n"
			"// Signed line distance function from '[SH17C] 2D line distance field' at https://www.shadertoy.com/view/4dBfzG\n"
			"float crossnorm_product(vec2 vec_a, vec2 vec_b){\n"
			"	return vec_a.x * vec_b.y - vec_a.y * vec_b.x;\n"
			"}\n"
			"\n"
			"// SDF for a line, found in a comment by valentingalea on https://www.shadertoy.com/view/XllGDs\n"
			"// So far, the most elegant version! Also the sexiest, as it leverages the power of\n"
			"// the exterior algebra =)\n"
			"// Also, 10 internet cookies to whoever can figure out how to make this work for line SEGMENTS! =D\n"
			"float sdf_line6(vec2 st, vec2 vert_a, vec2 vert_b){\n"
			"	vec2 dvec_ap = st - vert_a;      // Displacement vector from vert_a to our current pixel!\n"
			"	vec2 dvec_ab = vert_b - vert_a;  // Displacement vector from vert_a to vert_b\n"
			"	vec2 direction = normalize(dvec_ab);  // We find a direction vector, which has unit norm by definition!\n"
			"	return crossnorm_product(dvec_ap, direction);  // Ah, the mighty cross-norm product!\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_base_color;\n"
			"	if ((vertex_flags & RF_USE_TEXTURE) != 0) {\n"
			"		switch(vertex_texture_unit) {\n"
			"			case  0:  content_color = texture(texture00, vertex_tex_coords / textureSize(texture00, 0));  break;\n"
			"			case  1:  content_color = texture(texture01, vertex_tex_coords / textureSize(texture01, 0));  break;\n"
			"			case  2:  content_color = texture(texture02, vertex_tex_coords / textureSize(texture02, 0));  break;\n"
			"			case  3:  content_color = texture(texture03, vertex_tex_coords / textureSize(texture03, 0));  break;\n"
			"			case  4:  content_color = texture(texture04, vertex_tex_coords / textureSize(texture04, 0));  break;\n"
			"			case  5:  content_color = texture(texture05, vertex_tex_coords / textureSize(texture05, 0));  break;\n"
			"			case  6:  content_color = texture(texture06, vertex_tex_coords / textureSize(texture06, 0));  break;\n"
			"			case  7:  content_color = texture(texture07, vertex_tex_coords / textureSize(texture07, 0));  break;\n"
			"			case  8:  content_color = texture(texture08, vertex_tex_coords / textureSize(texture08, 0));  break;\n"
			"			case  9:  content_color = texture(texture09, vertex_tex_coords / textureSize(texture09, 0));  break;\n"
			"			case 10:  content_color = texture(texture10, vertex_tex_coords / textureSize(texture10, 0));  break;\n"
			"			case 11:  content_color = texture(texture11, vertex_tex_coords / textureSize(texture11, 0));  break;\n"
			"			case 12:  content_color = texture(texture12, vec3(vertex_tex_coords / textureSize(texture12, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 13:  content_color = texture(texture13, vec3(vertex_tex_coords / textureSize(texture13, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 14:  content_color = texture(texture14, vec3(vertex_tex_coords / textureSize(texture14, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 15:  content_color = texture(texture15, vec3(vertex_tex_coords / textureSize(texture15, 0).xy, vertex_texture_array_index));  break;\n"
			"		}\n"
			"	}\n"
			"	if ((vertex_flags & RF_GLYPH) != 0) {\n"
			"		fragment_color = vec4(vertex_base_color.rgb, vertex_base_color.a * content_color.r);\n"
			"	} else if (vertex_sdf_type != 0) {\n"
			"		float distance = -1;\n"
			"		switch(vertex_sdf_type) {\n"
			"			case 1u:  // SDF_ROUNDED_RECT\n"
			"				distance = sdAxisAlignedRect(vertex_pos, vertex_points[0] + vertex_corner_radius, vertex_points[1] - vertex_corner_radius) - vertex_corner_radius;\n"
			"				break;\n"
			"			case 2u:  // SDF_CIRCLE\n"
			"				distance = length(vertex_pos - vertex_points[0]) - vertex_corner_radius;\n"
			"				break;\n"
			"			case 3u:  // SDF_INV_CIRCLE\n"
			"				distance = -(length(vertex_pos - vertex_points[0]) - vertex_corner_radius);\n"
			"				break;\n"
			"			case 4u:  // SDF_POLYGON\n"
			"				distance = sdPolygon(uint(vertex_point_count), vertex_points, vertex_pos) - vertex_corner_radius;"
			"				break;\n"
			"			case 5u:  // SDF_TEXTURE\n"
			"				distance = (content_color.r - 0.5) * 8;\n"
			"				content_color = vertex_base_color;\n"
			"				break;\n"
			"			case 6u: {  // SDF_CIRCLE_SEGMENT\n"
			"				// vertex_points[0]: center, vertex_points[1]: outer_radius, inner_radius, vertex_points[2]: line A (center to this point), vertex_points[3]: line B (this point to center)\n"
			"				float outer_circle_dist = length(vertex_pos - vertex_points[0]) - vertex_points[1].x;\n"
			"				float inner_circle_dist = length(vertex_pos - vertex_points[0]) - vertex_points[1].y;\n"
			"				float line_a_dist = sdf_line6(vertex_pos, vertex_points[2], vertex_points[0]);\n"
			"				float line_b_dist = sdf_line6(vertex_pos, vertex_points[0], vertex_points[3]);\n"
			"				// (inner_circle_dist substract from outer_circle_dist ) intersect (line_a_dist intersect line_b_dist)\n"
			"				distance = max( max( -inner_circle_dist, outer_circle_dist ), max(line_a_dist, line_b_dist) );\n"
			"				} break;\n"
			"			case 7u:  // SDF_RECT\n"
			"				distance = sdAxisAlignedRect(vertex_pos, vertex_points[0], vertex_points[1]);\n"
			"				break;\n"
			"		}\n"
			"		float pixel_width = dFdx(vertex_pos.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		float coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
			"		\n"
			"		if ((vertex_flags & RF_USE_BORDER) != 0) {\n"
			"			float border_inner_transition = 1 - smoothstep(-vertex_border_width, -(vertex_border_width + pixel_width), distance);\n"
			"			content_color = vec4(mix(content_color.rgb, vertex_border_color.rgb, border_inner_transition * vertex_border_color.a), content_color.a);\n"
			"		}\n"
			"		\n"
			"		fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
			"	} else {\n"
			"		fragment_color = content_color;\n"
			"	}\n"
			"}\n"
		}
	});
	
	report_approach_start(use_builtin_scenario ? "one_ssbo_ext_one_sdf_demo" : "one_ssbo_ext_one_sdf");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBO with new data (doesn't change here but would with real usecases)
			uint32_t rects_count = 0;
			if (use_builtin_scenario) {
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 10, .top = 10, .right = 20, .bottom = 20
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 30, .top = 10, .right = 40, .bottom = 20,
					.sdf_type = SDF_RECT, .points[0] = vecs(30, 10), .points[1] = vecs(40, 20)
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 50, .top = 10, .right = 60, .bottom = 20,
					.sdf_type = SDF_RECT, .points[0] = vecs(50, 10), .points[1] = vecs(60, 20),
					.border_color = (color_t){ 255, 0, 0, 255 }, .border_width = 1,
					.flags = ONE_SSBO_USE_BORDER
				};
				
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 100, .top = 100, .right = 200, .bottom = 200,
					.sdf_type = SDF_ROUNDED_RECT, .points[0] = vecs(100, 100), .points[1] = vecs(200, 200), .corner_radius = 10
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 300, .top = 100, .right = 400, .bottom = 200,
					.sdf_type = SDF_CIRCLE, .points[0] = vecs(350, 150), .corner_radius = 50
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 500, .top = 100, .right = 600, .bottom = 200,
					.sdf_type = SDF_INV_CIRCLE, .points[0] = vecs(550, 150), .corner_radius = 50
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 700, .top = 100, .right = 800, .bottom = 200,
					.sdf_type = SDF_POLYGON, .point_count = 7,
					.points[0] = vecs(700, 120), .points[1] = vecs(720, 100), .points[2] = vecs(740, 100), .points[3] = vecs(760, 120),
					.points[4] = vecs(800, 120), .points[5] = vecs(800, 140), .points[6] = vecs(700, 140)
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 900, .top = 100, .right = 1000, .bottom = 200,
					.sdf_type = SDF_POLYGON, .point_count = 7, .corner_radius = 8,
					.points[0] = vecs(900+8, 120+8), .points[1] = vecs(920+8, 100+8), .points[2] = vecs(940-8, 100+8), .points[3] = vecs(960-8, 120+8),
					.points[4] = vecs(1000-8, 120+8), .points[5] = vecs(1000-8, 140-8), .points[6] = vecs(900+8, 140-8)
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 1100, .top = 100, .right = 1100+7*4, .bottom = 100+16*4,
					.sdf_type = SDF_TEXTURE, .flags = ONE_SSBO_USE_TEXTURE, .tex_unit = 0,
					.tex_left = 168, .tex_top = 0, .tex_right = 168+7, .tex_bottom = 0+16
				};
				rects_cpu_buffer[rects_count++] = (one_ssbo_rect_t){
					.base_color = (color_t){ 16, 16, 16, 255 },
					.left = 1300, .top = 100, .right = 1400, .bottom = 200,
					.sdf_type = SDF_CIRCLE_SEGMENT, .points[0] = vecs(1350, 150), .points[1] = vecs(50, 30), .points[2] = vecs(1300, 130), .points[3] = vecs(1300, 170)
				};
			} else {
				for (uint32_t i = 0; i < args->rects_count; i++) {
					// rectl_t  pos;
					// color_t  background_color;
					// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
					// float    border_width;
					// color_t  border_color;
					// uint32_t corner_radius;
					// GLuint   texture_index;
					// uint32_t texture_array_index;
					// rectf_t  texture_coords;
					// uint32_t random;
					rect_t* r = &args->rects_ptr[i];
					rects_cpu_buffer[i] = (one_ssbo_rect_t){
						.flags = ((r->has_texture || r->has_texture_array) ? ONE_SSBO_USE_TEXTURE : 0) | ((r->has_border || r->has_rounded_corners) ? ONE_SSBO_USE_BORDER : 0) | (r->has_glyph ? ONE_SSBO_GLYPH : 0),
						.layer = 0, .tex_unit = r->texture_index, .tex_array_index = r->texture_array_index,
						.base_color = r->background_color,
						.left = r->pos.l, .top = r->pos.t, .right = r->pos.r, .bottom = r->pos.b,
						
						.tex_left = r->texture_coords.l, .tex_top = r->texture_coords.t, .tex_right = r->texture_coords.r, .tex_bottom = r->texture_coords.b,
						.border_color = r->border_color, .border_width = r->border_width, .corner_radius = r->corner_radius
					};
					
					if (r->corner_radius > 0) {
						rects_cpu_buffer[i].sdf_type = SDF_ROUNDED_RECT;
						rects_cpu_buffer[i].points[0] = vecs(r->pos.l, r->pos.t);
						rects_cpu_buffer[i].points[1] = vecs(r->pos.r, r->pos.b);
						rects_cpu_buffer[i].point_count = 2;
					}
				}
				rects_count = args->rects_count;
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glInvalidateBufferData(ssbo);
			glNamedBufferData(ssbo, rects_count * sizeof(rects_cpu_buffer[0]), rects_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit(0, args->glyph_texture);
						glBindTextureUnit(1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &ssbo);
	free(rects_cpu_buffer);
}

void bench_one_ssbo_ext_one_sdf_pack(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	enum { ONE_SSBO_USE_TEXTURE = (1 << 0), ONE_SSBO_USE_BORDER = (1 << 1), ONE_SSBO_GLYPH = (1 << 2) };
	enum { SDF_NONE = 0, SDF_ROUNDED_RECT, SDF_CIRCLE, SDF_INV_CIRCLE, SDF_POLYGON, SDF_TEXTURE, SDF_CIRCLE_SEGMENT, SDF_RECT };
	typedef struct {
		uint8_t  flags, layer, tex_unit, tex_array_index;
		color_t  base_color;
		uint16_t left, top;
		uint16_t right, bottom;
		
		uint16_t tex_left, tex_top;
		uint16_t tex_right, tex_bottom;
		color_t  border_color;
		uint8_t  border_width, corner_radius, sdf_type, point_count;
		
		vecs_t   points[8];
	} one_ssbo_rect_t;
	one_ssbo_rect_t* rects_cpu_buffer = malloc(args->rects_count * sizeof(rects_cpu_buffer[0]));
	
	// All the data goes into the SSBO and we only use an empty VAO for the draw command. The shader then assembles the
	// per-vertex data by itself. An empty VAO should work according to spec, see https://community.khronos.org/t/running-a-vertex-shader-without-any-per-vertex-attribute/69568/4.
	GLuint vao = 0, ssbo = 0;
	glCreateVertexArrays(1, &vao);
	glCreateBuffers(1, &ssbo);
	GLuint program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"\n"
			"struct rect_t {\n"
			"	uint    packed_flags_layer_tex_unit_tex_array_index;\n"
			"	uint    packed_base_color;\n"
			"	uvec2   packed_ltrb;\n"
			"	uvec2   packed_tex_ltrb;\n"
			"	uint    packed_border_color;\n"
			"	uint    packed_border_width_corner_radius_sdf_type_point_count;\n"
			"	uint[8] points;\n"
			"};\n"
			"layout(std430, binding = 0) readonly buffer rect_buffer {\n"
			"	rect_t rects[];\n"
			"};\n"
			"\n"
			"layout(location = 0, component = 0) out uint    vertex_flags;\n"
			"layout(location = 0, component = 1) out uint    vertex_texture_unit;\n"
			"layout(location = 0, component = 2) out uint    vertex_texture_array_index;\n"
			"layout(location = 0, component = 3) out uint    vertex_sdf_type;\n"
			"layout(location = 1               ) out vec4    vertex_base_color;\n"
			"layout(location = 2, component = 0) out vec2    vertex_pos;\n"
			"layout(location = 2, component = 2) out vec2    vertex_tex_coords;\n"
			"layout(location = 3               ) out vec4    vertex_border_color;\n"
			"layout(location = 4, component = 0) out float   vertex_border_width;\n"
			"layout(location = 4, component = 1) out float   vertex_corner_radius;\n"
			"layout(location = 4, component = 2) out float   vertex_point_count;\n"
			"layout(location = 5               ) out vec2[8] vertex_points;\n"
			"\n"
			"// We let glDrawArrays() create 6 vertices per rect\n"
			"// Index into an vec4 containing left, top, right, bottom (x1 y1 x2 y2) of the rect\n"
			"uvec2 vertex_offset_to_rect_component_index[6] = uvec2[6](\n"
			"	// ltrb index for x,  ltrb index for y,  for vertex offset     visual          xywh       x1y1x2y2    ltrb    ltrb index\n"
			"	uvec2(            0,                 1), //            [0]     left  top       x   y      x1 y1       l t     0 1\n"
			"	uvec2(            0,                 3), //            [1]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 1), //            [2]     right top       x+w y      x2 y1       r t     2 1\n"
			"	uvec2(            0,                 3), //            [3]     left  bottom    x   y+h    x1 y2       l b     0 3\n"
			"	uvec2(            2,                 3), //            [4]     right bottom    x+w y+h    x2 y2       r b     2 3\n"
			"	uvec2(            2,                 1)  //            [5]     right top       x+w y      x2 y1       r t     2 1\n"
			");\n"
			"\n"
			"void main() {\n"
			"	uint rect_index    = uint(gl_VertexID) / 6;\n"
			"	uint vertex_offset = uint(gl_VertexID) % 6;\n"
			"	\n"
			"	vertex_flags               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  0, 8);\n"
			"	uint   layer               = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index,  8, 8);\n"
			"	vertex_texture_unit        = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 16, 8);\n"
			"	vertex_texture_array_index = bitfieldExtract(rects[rect_index].packed_flags_layer_tex_unit_tex_array_index, 24, 8);\n"
			"	vertex_base_color          = unpackUnorm4x8(rects[rect_index].packed_base_color);\n"
			"	vertex_border_color        = unpackUnorm4x8(rects[rect_index].packed_border_color);\n"
			"	vertex_border_width        = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count,  0, 8);\n"
			"	vertex_corner_radius       = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count,  8, 8);\n"
			"	vertex_sdf_type            = bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count, 16, 8);\n"
			"	vertex_point_count         = float(bitfieldExtract(rects[rect_index].packed_border_width_corner_radius_sdf_type_point_count, 24, 8));\n"
			"	\n"
			"	for (uint i = 0; i < rects[rect_index].points.length(); i++)\n"
			"		vertex_points[i] = vec2(ivec2(uvec2(rects[rect_index].points[i]) >> uvec2(0, 16) & 0xffffu));\n"
			"	\n"
			"	uvec2 component_index = vertex_offset_to_rect_component_index[vertex_offset];\n"
			"	vec4  rect_ltrb       = bitfieldExtract(rects[rect_index].packed_ltrb.xxyy     >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vec4  tex_ltrb        = bitfieldExtract(rects[rect_index].packed_tex_ltrb.xxyy >> uvec4(0, 16, 0, 16), 0, 16);\n"
			"	vertex_pos            = vec2(rect_ltrb[component_index.x], rect_ltrb[component_index.y]);\n"
			"	vertex_tex_coords     = vec2(tex_ltrb[component_index.x], tex_ltrb[component_index.y]);\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_ndc   = (vertex_pos / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"	//gl_Layer = int(layer);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"const uint RF_USE_TEXTURE = (1 << 0), RF_USE_BORDER = (1 << 1), RF_GLYPH = (1 << 2); // enum rect_flags_t;\n"
			"layout(location = 0, component = 0) in flat uint    vertex_flags;\n"
			"layout(location = 0, component = 1) in flat uint    vertex_texture_unit;\n"
			"layout(location = 0, component = 2) in flat uint    vertex_texture_array_index;\n"
			"layout(location = 0, component = 3) in flat uint    vertex_sdf_type;\n"
			"layout(location = 1               ) in flat vec4    vertex_base_color;\n"
			"layout(location = 2, component = 0) in      vec2    vertex_pos;\n"
			"layout(location = 2, component = 2) in      vec2    vertex_tex_coords;\n"
			"layout(location = 3               ) in flat vec4    vertex_border_color;\n"
			"layout(location = 4, component = 0) in flat float   vertex_border_width;\n"
			"layout(location = 4, component = 1) in flat float   vertex_corner_radius;\n"
			"layout(location = 4, component = 2) in flat float   vertex_point_count;\n"
			"layout(location = 5               ) in flat vec2[8] vertex_points;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"// 'Polygon - exact' function from https://iquilezles.org/articles/distfunctions2d/\n"
			"// Slightly modified to make it work with GLSL 4.5\n"
			"float sdPolygon(in uint N, in vec2[8] v, in vec2 p) {\n"
			"	float d = dot(p-v[0],p-v[0]);\n"
			"	float s = 1.0;\n"
			"	for(uint i=0, j=N-1; i<N; j=i, i++) {\n"
			"		vec2 e = v[j] - v[i];\n"
			"		vec2 w =    p - v[i];\n"
			"		vec2 b = w - e*clamp( dot(w,e)/dot(e,e), 0.0, 1.0 );\n"
			"		d = min( d, dot(b,b) );\n"
			"		bvec3 c = bvec3(p.y>=v[i].y,p.y<v[j].y,e.x*w.y>e.y*w.x);\n"
			"		if( all(c) || all(not(c)) ) s*=-1.0;  \n"
			"	}\n"
			"	return s*sqrt(d);\n"
			"}\n"
			"\n"
			"// Signed line distance function from '[SH17C] 2D line distance field' at https://www.shadertoy.com/view/4dBfzG\n"
			"float crossnorm_product(vec2 vec_a, vec2 vec_b){\n"
			"	return vec_a.x * vec_b.y - vec_a.y * vec_b.x;\n"
			"}\n"
			"\n"
			"// SDF for a line, found in a comment by valentingalea on https://www.shadertoy.com/view/XllGDs\n"
			"// So far, the most elegant version! Also the sexiest, as it leverages the power of\n"
			"// the exterior algebra =)\n"
			"// Also, 10 internet cookies to whoever can figure out how to make this work for line SEGMENTS! =D\n"
			"float sdf_line6(vec2 st, vec2 vert_a, vec2 vert_b){\n"
			"	vec2 dvec_ap = st - vert_a;      // Displacement vector from vert_a to our current pixel!\n"
			"	vec2 dvec_ab = vert_b - vert_a;  // Displacement vector from vert_a to vert_b\n"
			"	vec2 direction = normalize(dvec_ab);  // We find a direction vector, which has unit norm by definition!\n"
			"	return crossnorm_product(dvec_ap, direction);  // Ah, the mighty cross-norm product!\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_base_color;\n"
			"	if ((vertex_flags & RF_USE_TEXTURE) != 0) {\n"
			"		switch(vertex_texture_unit) {\n"
			"			case  0:  content_color = texture(texture00, vertex_tex_coords / textureSize(texture00, 0));  break;\n"
			"			case  1:  content_color = texture(texture01, vertex_tex_coords / textureSize(texture01, 0));  break;\n"
			"			case  2:  content_color = texture(texture02, vertex_tex_coords / textureSize(texture02, 0));  break;\n"
			"			case  3:  content_color = texture(texture03, vertex_tex_coords / textureSize(texture03, 0));  break;\n"
			"			case  4:  content_color = texture(texture04, vertex_tex_coords / textureSize(texture04, 0));  break;\n"
			"			case  5:  content_color = texture(texture05, vertex_tex_coords / textureSize(texture05, 0));  break;\n"
			"			case  6:  content_color = texture(texture06, vertex_tex_coords / textureSize(texture06, 0));  break;\n"
			"			case  7:  content_color = texture(texture07, vertex_tex_coords / textureSize(texture07, 0));  break;\n"
			"			case  8:  content_color = texture(texture08, vertex_tex_coords / textureSize(texture08, 0));  break;\n"
			"			case  9:  content_color = texture(texture09, vertex_tex_coords / textureSize(texture09, 0));  break;\n"
			"			case 10:  content_color = texture(texture10, vertex_tex_coords / textureSize(texture10, 0));  break;\n"
			"			case 11:  content_color = texture(texture11, vertex_tex_coords / textureSize(texture11, 0));  break;\n"
			"			case 12:  content_color = texture(texture12, vec3(vertex_tex_coords / textureSize(texture12, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 13:  content_color = texture(texture13, vec3(vertex_tex_coords / textureSize(texture13, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 14:  content_color = texture(texture14, vec3(vertex_tex_coords / textureSize(texture14, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 15:  content_color = texture(texture15, vec3(vertex_tex_coords / textureSize(texture15, 0).xy, vertex_texture_array_index));  break;\n"
			"		}\n"
			"	}\n"
			"	if ((vertex_flags & RF_GLYPH) != 0) {\n"
			"		fragment_color = vec4(vertex_base_color.rgb, vertex_base_color.a * content_color.r);\n"
			"	} else if (vertex_sdf_type != 0) {\n"
			"		float distance = -1;\n"
			"		switch(vertex_sdf_type) {\n"
			"			case 1u:  // SDF_ROUNDED_RECT\n"
			"				distance = sdAxisAlignedRect(vertex_pos, vertex_points[0] + vertex_corner_radius, vertex_points[1] - vertex_corner_radius) - vertex_corner_radius;\n"
			"				break;\n"
			"			case 2u:  // SDF_CIRCLE\n"
			"				distance = length(vertex_pos - vertex_points[0]) - vertex_corner_radius;\n"
			"				break;\n"
			"			case 3u:  // SDF_INV_CIRCLE\n"
			"				distance = -(length(vertex_pos - vertex_points[0]) - vertex_corner_radius);\n"
			"				break;\n"
			"			case 4u:  // SDF_POLYGON\n"
			"				distance = sdPolygon(uint(vertex_point_count), vertex_points, vertex_pos) - vertex_corner_radius;"
			"				break;\n"
			"			case 5u:  // SDF_TEXTURE\n"
			"				distance = (content_color.r - 0.5) * 8;\n"
			"				content_color = vertex_base_color;\n"
			"				break;\n"
			"			case 6u: {  // SDF_CIRCLE_SEGMENT\n"
			"				// vertex_points[0]: center, vertex_points[1]: outer_radius, inner_radius, vertex_points[2]: line A (center to this point), vertex_points[3]: line B (this point to center)\n"
			"				float outer_circle_dist = length(vertex_pos - vertex_points[0]) - vertex_points[1].x;\n"
			"				float inner_circle_dist = length(vertex_pos - vertex_points[0]) - vertex_points[1].y;\n"
			"				float line_a_dist = sdf_line6(vertex_pos, vertex_points[2], vertex_points[0]);\n"
			"				float line_b_dist = sdf_line6(vertex_pos, vertex_points[0], vertex_points[3]);\n"
			"				// (inner_circle_dist substract from outer_circle_dist ) intersect (line_a_dist intersect line_b_dist)\n"
			"				distance = max( max( -inner_circle_dist, outer_circle_dist ), max(line_a_dist, line_b_dist) );\n"
			"				} break;\n"
			"			case 7u:  // SDF_RECT\n"
			"				distance = sdAxisAlignedRect(vertex_pos, vertex_points[0], vertex_points[1]);\n"
			"				break;\n"
			"		}\n"
			"		float pixel_width = dFdx(vertex_pos.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		float coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
			"		\n"
			"		if ((vertex_flags & RF_USE_BORDER) != 0) {\n"
			"			float border_inner_transition = 1 - smoothstep(-vertex_border_width, -(vertex_border_width + pixel_width), distance);\n"
			"			content_color = vec4(mix(content_color.rgb, vertex_border_color.rgb, border_inner_transition * vertex_border_color.a), content_color.a);\n"
			"		}\n"
			"		\n"
			"		fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
			"	} else {\n"
			"		fragment_color = content_color;\n"
			"	}\n"
			"}\n"
		}
	});
	
	report_approach_start("one_ssbo_ext_one_sdf_pack");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update SSBO with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				rects_cpu_buffer[i] = (one_ssbo_rect_t){
					.flags = ((r->has_texture || r->has_texture_array) ? ONE_SSBO_USE_TEXTURE : 0) | ((r->has_border || r->has_rounded_corners) ? ONE_SSBO_USE_BORDER : 0) | (r->has_glyph ? ONE_SSBO_GLYPH : 0),
					.layer = 0, .tex_unit = r->texture_index, .tex_array_index = r->texture_array_index,
					.base_color = r->background_color,
					.left = r->pos.l, .top = r->pos.t, .right = r->pos.r, .bottom = r->pos.b,
					
					.tex_left = r->texture_coords.l, .tex_top = r->texture_coords.t, .tex_right = r->texture_coords.r, .tex_bottom = r->texture_coords.b,
					.border_color = r->border_color, .border_width = r->border_width, .corner_radius = r->corner_radius
				};
				
				if (r->corner_radius > 0) {
					rects_cpu_buffer[i].sdf_type = SDF_ROUNDED_RECT;
					rects_cpu_buffer[i].points[0] = vecs(r->pos.l, r->pos.t);
					rects_cpu_buffer[i].points[1] = vecs(r->pos.r, r->pos.b);
					rects_cpu_buffer[i].point_count = 2;
				}
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glInvalidateBufferData(ssbo);
			glNamedBufferData(ssbo, args->rects_count * sizeof(rects_cpu_buffer[0]), rects_cpu_buffer, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
		
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(program);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
						glProgramUniform2f(program, 0, window_width / 2, window_height / 2);
						
						glBindTextureUnit(0, args->glyph_texture);
						glBindTextureUnit(1, args->image_texture);
						glBindTextureUnit(12, args->texture_array);
						
						const int vertices_per_rect = 6;
						glDrawArrays(GL_TRIANGLES, 0, args->rects_count * vertices_per_rect);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(program);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &ssbo);
	free(rects_cpu_buffer);
}



void bench_instancing_and_divisor(scenario_args_t* args) {
	// Setup
	int window_width = 0, window_height = 0;
	SDL_GetWindowSize(args->window, &window_width, &window_height);
	
	// Create a VBO for instanced rect rendering (render that VBO for each rect)
	GLuint one_rect_vbo = 0;
	struct { uint16_t x, y; } one_rect_vertices[] = {
		{ 0, 1 }, // left  top
		{ 0, 3 }, // left  bottom
		{ 2, 1 }, // right top
		{ 0, 3 }, // left  bottom
		{ 2, 3 }, // right bottom
		{ 2, 1 }, // right top
	};
	glCreateBuffers(1, &one_rect_vbo);
	glNamedBufferStorage(one_rect_vbo, sizeof(one_rect_vertices), one_rect_vertices, 0);
	
	// Create the VBO for the individual rect data
	GLuint rects_vbo = 0;
	enum { ONE_SSBO_USE_TEXTURE = (1 << 0), ONE_SSBO_USE_BORDER = (1 << 1), ONE_SSBO_GLYPH = (1 << 2) };
	enum { SDF_NONE = 0, SDF_ROUNDED_RECT, SDF_CIRCLE, SDF_INV_CIRCLE, SDF_POLYGON, SDF_TEXTURE, SDF_CIRCLE_SEGMENT, SDF_RECT };
	typedef struct {
		uint8_t  flags, layer, tex_unit, tex_array_index;
		color_t  base_color;
		uint16_t left, top;
		uint16_t right, bottom;
		
		uint16_t tex_left, tex_top;
		uint16_t tex_right, tex_bottom;
		color_t  border_color;
		uint8_t  border_width, corner_radius, sdf_type, point_count;
		
		vecs_t   points[8];
	} vbo_rect_t;
	glCreateBuffers(1, &rects_vbo);
	const uint32_t rects_size = args->rects_count * sizeof(vbo_rect_t);
	vbo_rect_t* rects_ptr = malloc(rects_size);
	
	// Create a vertex array object (VAO) that reads from the one_rect_vbo for each instance and one entry from rects_vbo for each instance
	GLuint vao = 0;
	glCreateVertexArrays(1, &vao);
		glVertexArrayVertexBuffer(vao, 0, one_rect_vbo, 0, sizeof(one_rect_vertices[0]));  // Set data source 0 to vbo, with offset 0 and proper stride
		glVertexArrayVertexBuffer(vao, 1, rects_vbo,    0, sizeof(rects_ptr[0]));          // Set data source 1 to rects_vbo, with offset 0 and proper stride
		glVertexArrayBindingDivisor(vao, 1, 1);  // Advance data source 1 every 1 instance instead of for every vertex (3rd argument would be 0)
	// layout(location =  0) in uvec2   corner_rel_pos
		glEnableVertexArrayAttrib( vao, 0);     // corner_rel_pos
		glVertexArrayAttribBinding(vao, 0, 0);  // read from data source 0
		glVertexArrayAttribIFormat(vao, 0, 2, GL_UNSIGNED_SHORT, 0);
	// layout(location = 1) in uvec4 rect_flags_layer_tex_unit_tex_array_index
		glEnableVertexArrayAttrib( vao, 1);     // read rect_flags from a data source
		glVertexArrayAttribBinding(vao, 1, 1);  // read from data source 1
		glVertexArrayAttribIFormat(vao, 1, 4, GL_UNSIGNED_BYTE, offsetof(vbo_rect_t, flags));
	// layout(location = 2) in vec4  rect_base_color
		glEnableVertexArrayAttrib( vao, 2);     // read it from a data source
		glVertexArrayAttribBinding(vao, 2, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 2, 4, GL_UNSIGNED_BYTE, true, offsetof(vbo_rect_t, base_color));
	// layout(location = 3) in vec4  rect_ltrb
		glEnableVertexArrayAttrib( vao, 3);     // read it from a data source
		glVertexArrayAttribBinding(vao, 3, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 3, 4, GL_UNSIGNED_SHORT, false, offsetof(vbo_rect_t, left));
	// layout(location = 4) in vec4  rect_tex_ltrb
		glEnableVertexArrayAttrib( vao, 4);     // read it from a data source
		glVertexArrayAttribBinding(vao, 4, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 4, 4, GL_UNSIGNED_SHORT, false, offsetof(vbo_rect_t, tex_left));
	// layout(location = 5) in vec4  rect_border_color
		glEnableVertexArrayAttrib( vao, 5);     // read it from a data source
		glVertexArrayAttribBinding(vao, 5, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 5, 4, GL_UNSIGNED_BYTE, true, offsetof(vbo_rect_t, border_color));
	// layout(location = 6) in float rect_border_width
		glEnableVertexArrayAttrib( vao, 6);     // read it from a data source
		glVertexArrayAttribBinding(vao, 6, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 6, 1, GL_UNSIGNED_BYTE, false, offsetof(vbo_rect_t, border_width));
	// layout(location = 7) in float rect_corner_radius
		glEnableVertexArrayAttrib( vao, 7);     // read it from a data source
		glVertexArrayAttribBinding(vao, 7, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 7, 1, GL_UNSIGNED_BYTE, false, offsetof(vbo_rect_t, corner_radius));
	// layout(location = 8) in uint  rect_sdf_type
		glEnableVertexArrayAttrib( vao, 8);     // read it from a data source
		glVertexArrayAttribBinding(vao, 8, 1);  // read from data source 1
		glVertexArrayAttribIFormat(vao, 8, 1, GL_UNSIGNED_BYTE, offsetof(vbo_rect_t, sdf_type));
	// layout(location = 9) in uint  rect_point_count
		glEnableVertexArrayAttrib( vao, 9);     // read it from a data source
		glVertexArrayAttribBinding(vao, 9, 1);  // read from data source 1
		glVertexArrayAttribIFormat(vao, 9, 1, GL_UNSIGNED_BYTE, offsetof(vbo_rect_t, point_count));
	// layout(location = 10) in vec2[8] rect_points
		// With arrays each element in the array is one consecutive attribute and we need to setup each one
		// Problem: Can only use up to 16 vertex attributes (GL_MAX_VERTEX_ATTRIBS = 16 on system). Hence skip the last two points.
		// Does skew the benchmark a bit but let's see how the performance of that approach will be.
		for (uint32_t i = 0; i < 6; i++) {
			glEnableVertexArrayAttrib( vao, 10 + i);     // read it from a data source
			glVertexArrayAttribBinding(vao, 10 + i, 1);  // read from data source 1
			glVertexArrayAttribFormat( vao, 10 + i, 2, GL_UNSIGNED_SHORT, false, offsetof(vbo_rect_t, points) + i*sizeof(vecs_t));
		}
	
	GLuint shader_program = load_shader_program(2, (shader_type_and_source_t[]){
		{ GL_VERTEX_SHADER,
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"\n"
			"layout(location =  0) in uvec2   corner_rel_pos;\n"
			"layout(location =  1) in uvec4   rect_flags_layer_tex_unit_tex_array_index;\n"
			"layout(location =  2) in vec4    rect_base_color;\n"
			"layout(location =  3) in vec4    rect_ltrb;\n"
			"layout(location =  4) in vec4    rect_tex_ltrb;\n"
			"layout(location =  5) in vec4    rect_border_color;\n"
			"layout(location =  6) in float   rect_border_width;\n"
			"layout(location =  7) in float   rect_corner_radius;\n"
			"layout(location =  8) in uint    rect_sdf_type;\n"
			"layout(location =  9) in uint    rect_point_count;\n"
			"layout(location = 10) in vec2[6] rect_points;\n"
			"\n"
			"out uint    vertex_flags;\n"
			"out uint    vertex_texture_unit;\n"
			"out uint    vertex_texture_array_index;\n"
			"out vec4    vertex_base_color;\n"
			"out vec2    vertex_pos;\n"
			"out vec2    vertex_tex_coords;\n"
			"out vec4    vertex_border_color;\n"
			"out float   vertex_border_width;\n"
			"out float   vertex_corner_radius;\n"
			"out uint    vertex_sdf_type;\n"
			"out uint    vertex_point_count;\n"
			"out vec2[6] vertex_points;\n"
			"\n"
			"void main() {\n"
			"	vertex_flags               = rect_flags_layer_tex_unit_tex_array_index.x;\n"
			"	uint   layer               = rect_flags_layer_tex_unit_tex_array_index.y;\n"
			"	vertex_texture_unit        = rect_flags_layer_tex_unit_tex_array_index.z;\n"
			"	vertex_texture_array_index = rect_flags_layer_tex_unit_tex_array_index.w;\n"
			"	vertex_base_color          = rect_base_color;\n"
			"	vertex_border_color        = rect_border_color;\n"
			"	vertex_border_width        = rect_border_width;\n"
			"	vertex_corner_radius       = rect_corner_radius;\n"
			"	vertex_sdf_type            = rect_sdf_type;\n"
			"	vertex_point_count         = rect_point_count;\n"
			"	vertex_points              = rect_points;\n"
			"	\n"
			"	vertex_pos        = vec2(rect_ltrb[corner_rel_pos.x], rect_ltrb[corner_rel_pos.y]);\n"
			"	vertex_tex_coords = vec2(rect_tex_ltrb[corner_rel_pos.x], rect_tex_ltrb[corner_rel_pos.y]);\n"
			"	\n"
			"	vec2 axes_flip = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_ndc   = (vertex_pos / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_ndc, 0, 1);\n"
			"	//gl_Layer = int(layer);\n"
			"}\n"
		}, { GL_FRAGMENT_SHADER,
			"#version 450 core\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding =  0) uniform sampler2D      texture00;\n"
			"layout(binding =  1) uniform sampler2D      texture01;\n"
			"layout(binding =  2) uniform sampler2D      texture02;\n"
			"layout(binding =  3) uniform sampler2D      texture03;\n"
			"layout(binding =  4) uniform sampler2D      texture04;\n"
			"layout(binding =  5) uniform sampler2D      texture05;\n"
			"layout(binding =  6) uniform sampler2D      texture06;\n"
			"layout(binding =  7) uniform sampler2D      texture07;\n"
			"layout(binding =  8) uniform sampler2D      texture08;\n"
			"layout(binding =  9) uniform sampler2D      texture09;\n"
			"layout(binding = 10) uniform sampler2D      texture10;\n"
			"layout(binding = 11) uniform sampler2D      texture11;\n"
			"layout(binding = 12) uniform sampler2DArray texture12;\n"
			"layout(binding = 13) uniform sampler2DArray texture13;\n"
			"layout(binding = 14) uniform sampler2DArray texture14;\n"
			"layout(binding = 15) uniform sampler2DArray texture15;\n"
			"\n"
			"const uint RF_USE_TEXTURE = (1 << 0), RF_USE_BORDER = (1 << 1), RF_GLYPH = (1 << 2); // enum rect_flags_t;\n"
			"in flat uint    vertex_flags;\n"
			"in flat uint    vertex_texture_unit;\n"
			"in flat uint    vertex_texture_array_index;\n"
			"in flat vec4    vertex_base_color;\n"
			"in      vec2    vertex_pos;\n"
			"in      vec2    vertex_tex_coords;\n"
			"in flat vec4    vertex_border_color;\n"
			"in flat float   vertex_border_width;\n"
			"in flat float   vertex_corner_radius;\n"
			"in flat uint    vertex_sdf_type;\n"
			"in flat uint    vertex_point_count;\n"
			"in flat vec2[6] vertex_points;\n"
			"\n"
			"out vec4 fragment_color;\n"
			"\n"
			"// Function by jozxyqk from https://stackoverflow.com/questions/30545052/calculate-signed-distance-between-point-and-rectangle\n"
			"// Renamed tl to lt and br to rb to make the meaning of the individual components more obvious\n"
			"float sdAxisAlignedRect(vec2 uv, vec2 lt, vec2 rb) {\n"
			"	vec2 d = max(lt-uv, uv-rb);\n"
			"	return length(max(vec2(0.0), d)) + min(0.0, max(d.x, d.y));\n"
			"}\n"
			"\n"
			"// 'Polygon - exact' function from https://iquilezles.org/articles/distfunctions2d/\n"
			"// Slightly modified to make it work with GLSL 4.5\n"
			"float sdPolygon(in uint N, in vec2[6] v, in vec2 p) {\n"
			"	float d = dot(p-v[0],p-v[0]);\n"
			"	float s = 1.0;\n"
			"	for(uint i=0, j=N-1; i<N; j=i, i++) {\n"
			"		vec2 e = v[j] - v[i];\n"
			"		vec2 w =    p - v[i];\n"
			"		vec2 b = w - e*clamp( dot(w,e)/dot(e,e), 0.0, 1.0 );\n"
			"		d = min( d, dot(b,b) );\n"
			"		bvec3 c = bvec3(p.y>=v[i].y,p.y<v[j].y,e.x*w.y>e.y*w.x);\n"
			"		if( all(c) || all(not(c)) ) s*=-1.0;  \n"
			"	}\n"
			"	return s*sqrt(d);\n"
			"}\n"
			"\n"
			"// Signed line distance function from '[SH17C] 2D line distance field' at https://www.shadertoy.com/view/4dBfzG\n"
			"float crossnorm_product(vec2 vec_a, vec2 vec_b){\n"
			"	return vec_a.x * vec_b.y - vec_a.y * vec_b.x;\n"
			"}\n"
			"\n"
			"// SDF for a line, found in a comment by valentingalea on https://www.shadertoy.com/view/XllGDs\n"
			"// So far, the most elegant version! Also the sexiest, as it leverages the power of\n"
			"// the exterior algebra =)\n"
			"// Also, 10 internet cookies to whoever can figure out how to make this work for line SEGMENTS! =D\n"
			"float sdf_line6(vec2 st, vec2 vert_a, vec2 vert_b){\n"
			"	vec2 dvec_ap = st - vert_a;      // Displacement vector from vert_a to our current pixel!\n"
			"	vec2 dvec_ab = vert_b - vert_a;  // Displacement vector from vert_a to vert_b\n"
			"	vec2 direction = normalize(dvec_ab);  // We find a direction vector, which has unit norm by definition!\n"
			"	return crossnorm_product(dvec_ap, direction);  // Ah, the mighty cross-norm product!\n"
			"}\n"
			"\n"
			"void main() {\n"
			"	vec4 content_color = vertex_base_color;\n"
			"	if ((vertex_flags & RF_USE_TEXTURE) != 0) {\n"
			"		switch(vertex_texture_unit) {\n"
			"			case  0:  content_color = texture(texture00, vertex_tex_coords / textureSize(texture00, 0));  break;\n"
			"			case  1:  content_color = texture(texture01, vertex_tex_coords / textureSize(texture01, 0));  break;\n"
			"			case  2:  content_color = texture(texture02, vertex_tex_coords / textureSize(texture02, 0));  break;\n"
			"			case  3:  content_color = texture(texture03, vertex_tex_coords / textureSize(texture03, 0));  break;\n"
			"			case  4:  content_color = texture(texture04, vertex_tex_coords / textureSize(texture04, 0));  break;\n"
			"			case  5:  content_color = texture(texture05, vertex_tex_coords / textureSize(texture05, 0));  break;\n"
			"			case  6:  content_color = texture(texture06, vertex_tex_coords / textureSize(texture06, 0));  break;\n"
			"			case  7:  content_color = texture(texture07, vertex_tex_coords / textureSize(texture07, 0));  break;\n"
			"			case  8:  content_color = texture(texture08, vertex_tex_coords / textureSize(texture08, 0));  break;\n"
			"			case  9:  content_color = texture(texture09, vertex_tex_coords / textureSize(texture09, 0));  break;\n"
			"			case 10:  content_color = texture(texture10, vertex_tex_coords / textureSize(texture10, 0));  break;\n"
			"			case 11:  content_color = texture(texture11, vertex_tex_coords / textureSize(texture11, 0));  break;\n"
			"			case 12:  content_color = texture(texture12, vec3(vertex_tex_coords / textureSize(texture12, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 13:  content_color = texture(texture13, vec3(vertex_tex_coords / textureSize(texture13, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 14:  content_color = texture(texture14, vec3(vertex_tex_coords / textureSize(texture14, 0).xy, vertex_texture_array_index));  break;\n"
			"			case 15:  content_color = texture(texture15, vec3(vertex_tex_coords / textureSize(texture15, 0).xy, vertex_texture_array_index));  break;\n"
			"		}\n"
			"	}\n"
			"	if ((vertex_flags & RF_GLYPH) != 0) {\n"
			"		fragment_color = vec4(vertex_base_color.rgb, vertex_base_color.a * content_color.r);\n"
			"	} else if (vertex_sdf_type != 0) {\n"
			"		float distance = -1;\n"
			"		switch(vertex_sdf_type) {\n"
			"			case 1u:  // SDF_ROUNDED_RECT\n"
			"				distance = sdAxisAlignedRect(vertex_pos, vertex_points[0] + vertex_corner_radius, vertex_points[1] - vertex_corner_radius) - vertex_corner_radius;\n"
			"				break;\n"
			"			case 2u:  // SDF_CIRCLE\n"
			"				distance = length(vertex_pos - vertex_points[0]) - vertex_corner_radius;\n"
			"				break;\n"
			"			case 3u:  // SDF_INV_CIRCLE\n"
			"				distance = -(length(vertex_pos - vertex_points[0]) - vertex_corner_radius);\n"
			"				break;\n"
			"			case 4u:  // SDF_POLYGON\n"
			"				distance = sdPolygon(uint(vertex_point_count), vertex_points, vertex_pos) - vertex_corner_radius;"
			"				break;\n"
			"			case 5u:  // SDF_TEXTURE\n"
			"				distance = (content_color.r - 0.5) * 8;\n"
			"				content_color = vertex_base_color;\n"
			"				break;\n"
			"			case 6u: {  // SDF_CIRCLE_SEGMENT\n"
			"				// vertex_points[0]: center, vertex_points[1]: outer_radius, inner_radius, vertex_points[2]: line A (center to this point), vertex_points[3]: line B (this point to center)\n"
			"				float outer_circle_dist = length(vertex_pos - vertex_points[0]) - vertex_points[1].x;\n"
			"				float inner_circle_dist = length(vertex_pos - vertex_points[0]) - vertex_points[1].y;\n"
			"				float line_a_dist = sdf_line6(vertex_pos, vertex_points[2], vertex_points[0]);\n"
			"				float line_b_dist = sdf_line6(vertex_pos, vertex_points[0], vertex_points[3]);\n"
			"				// (inner_circle_dist substract from outer_circle_dist ) intersect (line_a_dist intersect line_b_dist)\n"
			"				distance = max( max( -inner_circle_dist, outer_circle_dist ), max(line_a_dist, line_b_dist) );\n"
			"				} break;\n"
			"			case 7u:  // SDF_RECT\n"
			"				distance = sdAxisAlignedRect(vertex_pos, vertex_points[0], vertex_points[1]);\n"
			"				break;\n"
			"		}\n"
			"		float pixel_width = dFdx(vertex_pos.x) * 1;  // Use 2.0 for a smoother AA look\n"
			"		float coverage = 1 - smoothstep(-pixel_width, 0, distance);\n"
			"		\n"
			"		if ((vertex_flags & RF_USE_BORDER) != 0) {\n"
			"			float border_inner_transition = 1 - smoothstep(-vertex_border_width, -(vertex_border_width + pixel_width), distance);\n"
			"			content_color = vec4(mix(content_color.rgb, vertex_border_color.rgb, border_inner_transition * vertex_border_color.a), content_color.a);\n"
			"		}\n"
			"		\n"
			"		fragment_color = vec4(content_color.rgb, content_color.a * coverage);\n"
			"	} else {\n"
			"		fragment_color = content_color;\n"
			"	}\n"
			"}\n"
		}
	});
	
	report_approach_start("inst_div");
	
	for (uint32_t frame_index = 0; frame_index < args->frame_count; frame_index++) {
		report_frame_start();
			
			// Update VBO with new data (doesn't change here but would with real usecases)
			for (uint32_t i = 0; i < args->rects_count; i++) {
				// rectl_t  pos;
				// color_t  background_color;
				// bool     has_border, has_rounded_corners, has_texture, has_texture_array, has_glyph;
				// float    border_width;
				// color_t  border_color;
				// uint32_t corner_radius;
				// GLuint   texture_index;
				// uint32_t texture_array_index;
				// rectf_t  texture_coords;
				// uint32_t random;
				rect_t* r = &args->rects_ptr[i];
				rects_ptr[i] = (vbo_rect_t){
					.flags = ((r->has_texture || r->has_texture_array) ? ONE_SSBO_USE_TEXTURE : 0) | ((r->has_border || r->has_rounded_corners) ? ONE_SSBO_USE_BORDER : 0) | (r->has_glyph ? ONE_SSBO_GLYPH : 0),
					.layer = 0, .tex_unit = r->texture_index, .tex_array_index = r->texture_array_index,
					.base_color = r->background_color,
					.left = r->pos.l, .top = r->pos.t, .right = r->pos.r, .bottom = r->pos.b,
					
					.tex_left = r->texture_coords.l, .tex_top = r->texture_coords.t, .tex_right = r->texture_coords.r, .tex_bottom = r->texture_coords.b,
					.border_color = r->border_color, .border_width = r->border_width, .corner_radius = r->corner_radius
				};
				
				if (r->corner_radius > 0) {
					rects_ptr[i].sdf_type = SDF_ROUNDED_RECT;
					rects_ptr[i].points[0] = vecs(r->pos.l, r->pos.t);
					rects_ptr[i].points[1] = vecs(r->pos.r, r->pos.b);
					rects_ptr[i].point_count = 2;
				}
			}
			
		report_gen_buffers_done();
			
			// Create a new GPU buffer each time so we don't have to wait for the previous draw call to finish.
			// Instead the old buffer data gets orphaned and freed once the previous frame is done. This prevents a
			// pipeline stall on continous refresh.
			glNamedBufferData(rects_vbo, rects_size, rects_ptr, GL_STREAM_DRAW);
			
		report_upload_done();
			
			glClearColor(0.8, 0.8, 1.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
			
		report_clear_done();
			
			glBindVertexArray(vao);
				glUseProgram(shader_program);
					glProgramUniform2f(shader_program, 0, window_width / 2, window_height / 2);
					
					glBindTextureUnit(0, args->glyph_texture);
					glBindTextureUnit(1, args->image_texture);
					glBindTextureUnit(12, args->texture_array);
					
					glDrawArraysInstanced(GL_TRIANGLES, 0, 6, args->rects_count);
				glUseProgram(0);
			glBindVertexArray(0);
			
		report_draw_done();
			
			SDL_GL_SwapWindow(args->window);
			
		report_frame_end();
	}
	report_approach_end();
	
	unload_shader_program(shader_program);
	glDeleteVertexArrays(1, &vao);
	free(rects_ptr);
	glDeleteBuffers(1, &rects_vbo);
	glDeleteBuffers(1, &one_rect_vbo);
}



//
// Main program that starts all benchmarks in various configurations
//

int main(int argc, char** argv) {
	// Process command line arguments
	bool use_gl_debug_log = false, write_gl_info = false, print_scenario_stats = false;
	uint32_t frame_count = 100;
	for (int i = 1; i < argc; i++) {
		if ( strcmp(argv[i], "--gl-debug-log") == 0 )
			use_gl_debug_log = true;
		else if ( strcmp(argv[i], "--dont-output-csv-headers") == 0 )
			reporting_output_csv_headers = false;
		else if ( strcmp(argv[i], "--capture-last-frames") == 0 )
			reporting_capture_last_frames = true;
		else if ( strcmp(argv[i], "--write-gl-info") == 0 )
			write_gl_info = true;
		else if ( strcmp(argv[i], "--disable-timer-queries") == 0 )
			reporting_query_timers = false;
		else if ( strcmp(argv[i], "--disable-per-frame-data") == 0 )
			reporting_output_per_frame_data = false;
		else if ( strcmp(argv[i], "--only-one-frame") == 0 )
			frame_count = 1;
		else if ( strcmp(argv[i], "--print-scenario-stats") == 0 )
			print_scenario_stats = true;
		else {
			fprintf(stderr, "Unknown command line option: %s\n", argv[i]);
			return 1;
		}
	}
	
	// Open window
	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);
	
	int window_width = 1600, window_height = 1000;
	SDL_Window* window = SDL_CreateWindow("Rectangle rendering micro-benchmark", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, SDL_WINDOW_OPENGL);
	
	// Init OpenGL context (with vsync disabled)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
	SDL_GL_SetSwapInterval(0);
	
	gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress); // Expects a function that returns a function pointer, but SDL_GL_GetProcAddress() just returns a void pointer. Hence the cast.
	if (use_gl_debug_log)
		gl_init_debug_log();
	
	if (write_gl_info) {
		FILE* f = fopen("glinfo.txt", "wb");
		
		fprintf(f, "gl vendor, %s\n", glGetString(GL_VENDOR));
		fprintf(f, "gl renderer, %s\n", glGetString(GL_RENDERER));
		fprintf(f, "gl version, %s\n", glGetString(GL_VERSION));
		fprintf(f, "gl shading language version, %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
		
		fprintf(f, "gl extensions,");
		GLint extension_count = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
		for (int i = 0; i < extension_count; i++)
			fprintf(f, " %s", glGetStringi(GL_EXTENSIONS, i));
		fprintf(f, "\n");
		
		fclose(f);
	}
	
	// Setup OpenGL rendering
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	// Load the test textures
	GLuint glyph_atlas_texture = load_gl_texture("26-glyph-atlas.png");
	GLuint image_texture = load_gl_texture("images/Clouds Battle by arsenixc.jpg");
	GLuint texture_array = load_gl_texture_array(48, 48, 10, "icons/%02d.png", 1, 10);
	
	// Setup
	scenario_args_t scenario_args = (scenario_args_t){
		.rects_count = 1000, .rects_ptr = NULL, .frame_count = frame_count,
		.glyph_texture = glyph_atlas_texture, .image_texture = image_texture, .texture_array = texture_array,
		.window = window
	};
	reporting_setup();
	
	// Process initial SDL events
	SDL_Event event;
	while( SDL_PollEvent(&event) ) {
	}
	
	
	// Run the bechnmarks
	
	// Old unused scenarios
	/*
	report_scenario("opaque");
	generate_rects_random(scenario_args.rects_count, &scenario_args.rects_count, &scenario_args.rects_ptr, (generate_rects_opts_t){ });
	if (print_scenario_stats) scenario_dump_stats("opaque", &scenario_args);
	
	bench_one_rect_per_draw(&scenario_args, false);
	bench_one_rect_per_draw(&scenario_args, true);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, false);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, true);
	bench_complete_vertex_buffer_for_all_rects(&scenario_args);
	bench_one_ssbo(&scenario_args);
	bench_ssbo_instruction_list(&scenario_args);
	bench_ssbo_inlined_instr_6(&scenario_args);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 6);
	
	report_scenario("transparent");
	generate_rects_random(scenario_args.rects_count, &scenario_args.rects_count, &scenario_args.rects_ptr, (generate_rects_opts_t){ .transparent_bg_color = true });
	if (print_scenario_stats) scenario_dump_stats("transparent", &scenario_args);
	
	bench_one_rect_per_draw(&scenario_args, false);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, false);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, true);
	bench_complete_vertex_buffer_for_all_rects(&scenario_args);
	bench_one_ssbo(&scenario_args);
	bench_ssbo_instruction_list(&scenario_args);
	bench_ssbo_inlined_instr_6(&scenario_args);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 6);
	*/
	
	#define RUN_ALL_BENCHS
	
	report_scenario("sublime");
	generate_rects_sublime_sample(&scenario_args.rects_count, &scenario_args.rects_ptr);
	if (print_scenario_stats) scenario_dump_stats("sublime", &scenario_args);
	
	bench_one_rect_per_draw(&scenario_args, false);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, false);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, true);
	bench_complete_vertex_buffer_for_all_rects(&scenario_args);
	bench_one_ssbo(&scenario_args);
	bench_ssbo_instruction_list(&scenario_args);
	bench_ssbo_inlined_instr_6(&scenario_args);
	#ifdef RUN_ALL_BENCHS
	bench_ssbo_inlined_instr(&scenario_args,  4,  4);
	bench_ssbo_inlined_instr(&scenario_args,  6,  6);
	bench_ssbo_inlined_instr(&scenario_args,  8,  8);
	bench_ssbo_inlined_instr(&scenario_args, 10, 10);
	bench_ssbo_inlined_instr(&scenario_args, 20, 20);
	bench_ssbo_inlined_instr(&scenario_args,  4,  6);
	bench_ssbo_inlined_instr(&scenario_args,  4,  8);
	bench_ssbo_inlined_instr(&scenario_args,  4, 10);
	bench_ssbo_inlined_instr(&scenario_args,  4, 20);
	bench_ssbo_inlined_instr(&scenario_args,  6,  4);
	bench_ssbo_inlined_instr(&scenario_args,  8,  4);
	bench_ssbo_inlined_instr(&scenario_args, 10,  4);
	bench_ssbo_inlined_instr(&scenario_args, 20,  4);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 4);
	#endif
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 6);
	#ifdef RUN_ALL_BENCHS
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 8);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 10);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 20);
	#endif
	bench_one_ssbo_ext_no_sdf(&scenario_args);
	bench_one_ssbo_ext_sdf_list(&scenario_args);
	bench_one_ssbo_ext_one_sdf(&scenario_args, false);
	bench_one_ssbo_ext_one_sdf_pack(&scenario_args);
	bench_instancing_and_divisor(&scenario_args);
	
	report_scenario("mediaplayer");
	generate_rects_mediaplayer_sample(&scenario_args.rects_count, &scenario_args.rects_ptr);
	if (print_scenario_stats) scenario_dump_stats("mediaplayer", &scenario_args);
	
	bench_one_rect_per_draw(&scenario_args, false);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, false);
	bench_simple_vertex_buffer_for_all_rects(&scenario_args, true);
	bench_complete_vertex_buffer_for_all_rects(&scenario_args);
	bench_one_ssbo(&scenario_args);
	bench_ssbo_instruction_list(&scenario_args);
	bench_ssbo_inlined_instr_6(&scenario_args);
	#ifdef RUN_ALL_BENCHS
	bench_ssbo_inlined_instr(&scenario_args,  4,  4);
	bench_ssbo_inlined_instr(&scenario_args,  6,  6);
	bench_ssbo_inlined_instr(&scenario_args,  8,  8);
	bench_ssbo_inlined_instr(&scenario_args, 10, 10);
	bench_ssbo_inlined_instr(&scenario_args, 20, 20);
	bench_ssbo_inlined_instr(&scenario_args,  4,  6);
	bench_ssbo_inlined_instr(&scenario_args,  4,  8);
	bench_ssbo_inlined_instr(&scenario_args,  4, 10);
	bench_ssbo_inlined_instr(&scenario_args,  4, 20);
	bench_ssbo_inlined_instr(&scenario_args,  6,  4);
	bench_ssbo_inlined_instr(&scenario_args,  8,  4);
	bench_ssbo_inlined_instr(&scenario_args, 10,  4);
	bench_ssbo_inlined_instr(&scenario_args, 20,  4);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 4);
	#endif
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 6);
	#ifdef RUN_ALL_BENCHS
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 8);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 10);
	bench_ssbo_fixed_vertex_to_fragment_buffer(&scenario_args, 20);
	#endif
	bench_one_ssbo_ext_no_sdf(&scenario_args);
	bench_one_ssbo_ext_sdf_list(&scenario_args);
	bench_one_ssbo_ext_one_sdf(&scenario_args, false);
	bench_one_ssbo_ext_one_sdf_pack(&scenario_args);
	bench_instancing_and_divisor(&scenario_args);
	
	report_scenario("demo");
	bench_one_ssbo_ext_one_sdf(&scenario_args, true);
	
	
	// Cleanup
	free(scenario_args.rects_ptr);
	
	reporting_cleanup();
	
	glDeleteTextures(1, &texture_array);
	glDeleteTextures(1, &image_texture);
	glDeleteTextures(1, &glyph_atlas_texture);
	
	SDL_GL_DeleteContext(gl_ctx);
	SDL_DestroyWindow(window);
	
	return 0;
}
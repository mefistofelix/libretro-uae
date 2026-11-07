/*
 * Headless libretro frontend for testing the PUAE JIT (e.g. under qemu-arm).
 *
 * usage: jit-runner <core.so> <frames> <content|-> [key=value ...]
 *   key=value: core options (puae_*) passed to the core.
 * Prints one status line every 50 frames, then a summary:
 *   resets, exceptions (illegal/A-line/F-line traps logged by the core), frame and chip RAM hashes.
 * Writes the last frame to last.ppm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include "libretro.h"

#define MAX_OPTS 64
static const char *opt_key[MAX_OPTS], *opt_val[MAX_OPTS];
static int n_opts;
static char sys_dir[1024], save_dir[1024];
static unsigned resets, traps;
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t *frame;
static unsigned frame_w, frame_h;
static size_t frame_pitch;
static int quiet_log;

static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h)
{
	while (n--)
		h = (h ^ *p++) * 16777619u;
	return h;
}

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
	char msg[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (strstr(msg, "CPU reset PC"))
		resets++;
	if (strstr(msg, "-Trap ") || strstr(msg, "Illegal instruction"))
		traps++;
	if (!quiet_log || level >= RETRO_LOG_WARN || strstr(msg, "JIT") || strstr(msg, "Trap")
	    || strstr(msg, "Illegal") || strstr(msg, "reset"))
		printf("[core] %s", msg);
}

static bool environment(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
		*(const char **)data = sys_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = save_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		for (int i = 0; i < n_opts; i++)
			if (!strcmp(v->key, opt_key[i])) {
				v->value = opt_val[i];
				return true;
			}
		v->value = NULL;
		return false;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = log_cb;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		pixfmt = *(const enum retro_pixel_format *)data;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned *)data = 0;  /* legacy SET_VARIABLES is enough */
		return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES:
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE:
		*(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
		return true;
	default:
		return false;
	}
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
	if (!data)
		return;
	size_t need = pitch * h;
	frame = realloc(frame, need);
	memcpy(frame, data, need);
	frame_w = w; frame_h = h; frame_pitch = pitch;
}
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t n) { (void)d; return n; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned p, unsigned d, unsigned i, unsigned id) { return 0; }

static uint32_t frame_hash(void)
{
	uint32_t h = 2166136261u;
	unsigned bpp = pixfmt == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2;
	for (unsigned y = 0; frame && y < frame_h; y++)
		h = fnv(frame + y * frame_pitch, frame_w * bpp, h);
	return h;
}

static void write_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f || !frame)
		return;
	fprintf(f, "P6\n%u %u\n255\n", frame_w, frame_h);
	for (unsigned y = 0; y < frame_h; y++)
		for (unsigned x = 0; x < frame_w; x++) {
			uint8_t rgb[3];
			if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
				uint32_t p = ((uint32_t *)(frame + y * frame_pitch))[x];
				rgb[0] = p >> 16; rgb[1] = p >> 8; rgb[2] = p;
			} else if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
				uint16_t p = ((uint16_t *)(frame + y * frame_pitch))[x];
				rgb[0] = (p >> 11) << 3; rgb[1] = ((p >> 5) & 63) << 2; rgb[2] = (p & 31) << 3;
			} else {
				uint16_t p = ((uint16_t *)(frame + y * frame_pitch))[x];
				rgb[0] = ((p >> 10) & 31) << 3; rgb[1] = ((p >> 5) & 31) << 3; rgb[2] = (p & 31) << 3;
			}
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

#define SYM(name) do { *(void **)&p_##name = dlsym(core, #name); \
	if (!p_##name) { fprintf(stderr, "missing %s\n", #name); return 2; } } while (0)

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s core.so frames content|- [key=value...]\n", argv[0]);
		return 2;
	}
	int frames = atoi(argv[2]);
	const char *content = strcmp(argv[3], "-") ? argv[3] : NULL;
	for (int i = 4; i < argc && n_opts < MAX_OPTS; i++) {
		char *eq = strchr(argv[i], '=');
		if (!eq)
			continue;
		*eq = 0;
		if (!strcmp(argv[i], "quiet")) { quiet_log = atoi(eq + 1); continue; }
		opt_key[n_opts] = argv[i];
		opt_val[n_opts++] = eq + 1;
	}
	snprintf(sys_dir, sizeof(sys_dir), "system");
	snprintf(save_dir, sizeof(save_dir), "saves");
	mkdir(sys_dir, 0755);
	mkdir(save_dir, 0755);

	void *core = dlopen(argv[1], RTLD_NOW);
	if (!core) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		return 2;
	}
	void (*p_retro_set_environment)(retro_environment_t);
	void (*p_retro_set_video_refresh)(retro_video_refresh_t);
	void (*p_retro_set_audio_sample)(retro_audio_sample_t);
	void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*p_retro_set_input_poll)(retro_input_poll_t);
	void (*p_retro_set_input_state)(retro_input_state_t);
	void (*p_retro_init)(void);
	bool (*p_retro_load_game)(const struct retro_game_info *);
	void (*p_retro_run)(void);
	void *(*p_retro_get_memory_data)(unsigned);
	size_t (*p_retro_get_memory_size)(unsigned);
	SYM(retro_set_environment); SYM(retro_set_video_refresh); SYM(retro_set_audio_sample);
	SYM(retro_set_audio_sample_batch); SYM(retro_set_input_poll); SYM(retro_set_input_state);
	SYM(retro_init); SYM(retro_load_game); SYM(retro_run);
	SYM(retro_get_memory_data); SYM(retro_get_memory_size);

	p_retro_set_environment(environment);
	p_retro_set_video_refresh(video_cb);
	p_retro_set_audio_sample(audio_cb);
	p_retro_set_audio_sample_batch(audio_batch_cb);
	p_retro_set_input_poll(input_poll_cb);
	p_retro_set_input_state(input_state_cb);
	p_retro_init();

	struct retro_game_info info = { content, NULL, 0, NULL };
	if (!p_retro_load_game(content ? &info : NULL)) {
		fprintf(stderr, "retro_load_game failed\n");
		return 3;
	}
	for (int f = 1; f <= frames; f++) {
		p_retro_run();
		if (f % 50 == 0) {
			printf("frame %d resets %u traps %u screen %08x\n", f, resets, traps, frame_hash());
			fflush(stdout);
		}
	}
	uint8_t *ram = p_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	size_t ram_size = p_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
	printf("RESULT resets=%u traps=%u screen=%08x chipram=%08x\n", resets, traps, frame_hash(),
	       ram ? fnv(ram, ram_size, 2166136261u) : 0);
	write_ppm("last.ppm");
	return 0;
}

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

//#include <gl45.h>


void gl_init_debug_log();

typedef struct { GLenum type; const char* source; } shader_type_and_source_t;
GLuint load_shader_program(size_t shader_count, shader_type_and_source_t shaders[shader_count]);
void unload_shader_program(GLuint program);

void save_texture_as_ppm(GLuint texture, const char* filename);



#ifdef GL45_HELPERS_IMPLEMENTATION
#undef GL45_HELPERS_IMPLEMENTATION

void gl_debug_callback(GLenum src, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* msg, void const* user_param) {
	const char *src_str = NULL, *type_str = NULL, *severity_str = NULL;
	
	switch (src) {
		case GL_DEBUG_SOURCE_API:             src_str = "API";             break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   src_str = "WINDOW SYSTEM";   break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: src_str = "SHADER COMPILER"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:     src_str = "THIRD PARTY";     break;
		case GL_DEBUG_SOURCE_APPLICATION:     src_str = "APPLICATION";     break;
		case GL_DEBUG_SOURCE_OTHER:           src_str = "OTHER";           break;
	}
	switch (type) {
		case GL_DEBUG_TYPE_ERROR:               type_str = "ERROR";               break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_str = "DEPRECATED_BEHAVIOR"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  type_str = "UNDEFINED_BEHAVIOR";  break;
		case GL_DEBUG_TYPE_PORTABILITY:         type_str = "PORTABILITY";         break;
		case GL_DEBUG_TYPE_PERFORMANCE:         type_str = "PERFORMANCE";         break;
		case GL_DEBUG_TYPE_MARKER:              type_str = "MARKER";              break;
		case GL_DEBUG_TYPE_OTHER:               type_str = "OTHER";               break;
	}
	switch (severity) {
		case GL_DEBUG_SEVERITY_NOTIFICATION: severity_str = "NOTIFICATION"; break;
		case GL_DEBUG_SEVERITY_LOW:          severity_str = "LOW";          break;
		case GL_DEBUG_SEVERITY_MEDIUM:       severity_str = "MEDIUM";       break;
		case GL_DEBUG_SEVERITY_HIGH:         severity_str = "HIGH";         break;
	}
	
	fprintf(stderr, "[GL %s %s %s] %u: %s\n", src_str, type_str, severity_str, id, msg);
}

void gl_init_debug_log() {
	// Enable OpenGL debugging
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(gl_debug_callback, NULL);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
}


GLuint load_shader_program(size_t shader_count, shader_type_and_source_t shaders[shader_count]) {
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

void unload_shader_program(GLuint program) {
	GLsizei shader_count = 0;
	GLuint shaders[8];
	glGetAttachedShaders(program, sizeof(shaders) / sizeof(shaders[0]), &shader_count, shaders);
	for (GLsizei i = 0; i < shader_count; i++)
		glDeleteShader(shaders[i]);  // will delete shader as soon as it's no longer attached
	glDeleteProgram(program);  // will also detatch (and thus delete) shaders
}


void save_texture_as_ppm(GLuint texture, const char* filename) {
	GLint width = 0, height = 0;
	glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_WIDTH, &width);
	glGetTextureLevelParameteriv(texture, 0, GL_TEXTURE_HEIGHT, &width);
	
	int image_size = width * height * 3 * sizeof(uint8_t);
	uint8_t* image = malloc(image_size);
	assert(image);
	
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glGetTextureImage(texture, 0, GL_RGB, GL_UNSIGNED_BYTE, image_size, image);
	
	FILE* f = fopen(filename, "wb");
	assert(f);
	fprintf(f, "P6 %d %d 255\n", width, height);
	
	for (int i = 0; i < height; i++) {
		// Remember, OpenGL images are bottom to top. Have to reverse.
		uint8_t* row_ptr = image + (height - 1 - i) * width * 3;
		fwrite(row_ptr, 1, width * 3, f);
	}
	
	fclose(f);
	free(image);
}

/**
 * back_or_front_buffer specifies if you want to read from the back buffer (GL_BACK, default, color buffer that is currently rendered into)
 * or the front buffer (GL_FRONT, color buffer displayed after a frame swap).
 */
void save_default_framebuffer_as_ppm(const char* filename, GLenum back_or_front_buffer) {
	GLint values[4];
	glad_glGetIntegerv(GL_VIEWPORT, values);
	GLint width = values[2], height = values[3];
	
	int image_size = width * height * 3 * sizeof(uint8_t);
	uint8_t* image = malloc(image_size);
	assert(image);
	
	GLint prev_read_buffer = 0;
	glGetIntegerv(GL_READ_BUFFER, &prev_read_buffer);
	
	glReadBuffer(back_or_front_buffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadnPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image_size, image);
	glReadBuffer(prev_read_buffer);
	
	FILE* f = fopen(filename, "wb");
	assert(f);
	fprintf(f, "P6 %d %d 255\n", width, height);
	
	for (int i = 0; i < height; i++) {
		// Remember, OpenGL images are bottom to top. Have to reverse.
		uint8_t* row_ptr = image + (height - 1 - i) * width * 3;
		fwrite(row_ptr, 1, width * 3, f);
	}
	
	fclose(f);
	free(image);
}

#endif
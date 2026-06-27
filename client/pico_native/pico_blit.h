#pragma once

#include <GLES2/gl2.h>
#include <cstdint>

class pico_blit_pipeline
{
	GLuint program = 0;
	GLuint vertex_buffer = 0;
	GLuint texture = 0;

	GLint pos_attrib = -1;
	GLint uv_attrib = -1;
	GLint tex_uniform = -1;

	int eye_width = 0;
	int eye_height = 0;

	bool initialized = false;

public:
	pico_blit_pipeline() = default;
	~pico_blit_pipeline();

	void init(int w, int h);
	void draw(int eye, GLuint src_texture);
	bool is_initialized() const { return initialized; }
};

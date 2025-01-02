#include "snes_spc/SNES_SPC.h"
#include "snes_spc/spc.h"
#include "spc.h"
#include <windows.h>
#include <cstdint>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <Mmsystem.h>
#pragma comment(lib,"winmm.lib")

static constexpr uint64_t operator"" _MiB(uint64_t value) {
	return value * 1024ULL * 1024ULL;
}

class DynamicBuffer
{
public:
	DynamicBuffer() :_buf(new uint8_t[_capacity_growth_rate]), _capacity(_capacity_growth_rate), _size(0) {}
	~DynamicBuffer() { delete[] _buf; }

	void write(size_t position, uint8_t* in, size_t size) {
		if (position + size > _capacity) {
			_capacity += _capacity_growth_rate;
			uint8_t* newBuf = new uint8_t[_capacity];
			memcpy(newBuf, _buf, _size);
			delete[] _buf;
			_buf = newBuf;
		}

		memcpy(_buf + position, in, size);
	}

	void append(uint8_t* in, size_t size) {
		if (size != 0) {
			write(_size, in, size);
			_size += size;
		}
	}

	size_t size() const { return _size; };
	uint8_t* buf() const { return _buf; };

private:
	const size_t _capacity_growth_rate = 10_MiB;

	uint8_t* _buf;
	size_t _size;
	size_t _capacity;
};

class WaveBuilder
{
public:
	WaveBuilder(DWORD sample_rate) {
		_buf = new uint8_t[_buf_size];

		_sample_rate = sample_rate;
		_buf_pos = 0x2C;
		_sample_count = 0;
	}

	void write(int16_t* in, DWORD remain) {
		_sample_count += remain;
		while (remain) {
			if (_buf_pos >= _buf_size) _flush();

			uint8_t* p = &_buf[_buf_pos];
			int32_t n = (_buf_size - _buf_pos) / 2;
			if (n > remain)
				n = remain;
			remain -= n;

			/* convert to LSB first format */
			while (n--)
			{
				int32_t s = *in++;
				*p++ = (uint8_t)s;
				*p++ = (uint8_t)(s >> 8);
			}

			_buf_pos = p - _buf;
			assert(_buf_pos <= _buf_size);
		}
	}

	void build() {
		_flush();

		/* generate header */
		unsigned char h[0x2C] =
		{
			'R','I','F','F',
			0,0,0,0,        /* length of rest of file */
			'W','A','V','E',
			'f','m','t',' ',
			0x10,0,0,0,     /* size of fmt chunk */
			1,0,            /* uncompressed format */
			0,0,            /* channel count */
			0,0,0,0,        /* sample rate */
			0,0,0,0,        /* bytes per second */
			0,0,            /* bytes per sample frame */
			16,0,           /* bits per sample */
			'd','a','t','a',
			0,0,0,0,        /* size of sample data */
			/* ... */       /* sample data */
		};

		_set_le32(h + 0x04, 0x2C - 8 + _sample_count * 2);
		h[0x16] = _chan_count;
		_set_le32(h + 0x18, _sample_rate);
		_set_le32(h + 0x1C, _sample_rate * _chan_count * 2);
		h[0x20] = _chan_count * 2;
		_set_le32(h + 0x28, _sample_count * 2);

		_wav.write(0, h, 0x2C);
	}

	DWORD get_sample_count() const { return _sample_count; }

	const DynamicBuffer& get_wave() const { return _wav; };
private:
	void _flush() {
		_wav.append(_buf, _buf_pos);
		_buf_pos = 0;
	}

	void _set_le32(void* p, unsigned long n) {
		((unsigned char*)p)[0] = (unsigned char)n;
		((unsigned char*)p)[1] = (unsigned char)(n >> 8);
		((unsigned char*)p)[2] = (unsigned char)(n >> 16);
		((unsigned char*)p)[3] = (unsigned char)(n >> 24);
	}

	const DWORD _buf_size = 65536;
	const DWORD _chan_count = 2;

	uint8_t* _buf;
	DWORD _sample_count;
	DWORD _sample_rate;
	DWORD _buf_pos;

	DynamicBuffer _wav;
};

void play_spc_memory(uint8_t* spc, size_t spc_size) {
	SNES_SPC* snes_spc = spc_new();
	SPC_Filter* filter = spc_filter_new();

	spc_load_spc(snes_spc, spc, spc_size);
	WaveBuilder builder(spc_sample_rate);
	while (builder.get_sample_count() < 20 * spc_sample_rate * 2)
	{
		/* Play into buffer */
#define BUF_SIZE 2048
		short buf[BUF_SIZE];
		spc_play(snes_spc, BUF_SIZE, buf);

		/* Filter samples */
		spc_filter_run(filter, buf, BUF_SIZE);

		builder.write(buf, BUF_SIZE);
	}
	spc_filter_delete(filter);
	spc_delete(snes_spc);

	builder.build();

	while (1) {
		PlaySoundA((LPCSTR)builder.get_wave().buf(), NULL, SND_MEMORY | SND_SYNC);
	}
}

int main() {
	play_spc_memory((uint8_t*)spc, spc_size);
}

/*
 * fec.c -- forward error correction based on Vandermonde matrices
 *
 * Copyright 1997-98 Luigi Rizzo (luigi@iet.unipi.it)
 * Copyright 2001 Alain Knaff (alain@knaff.lu)
 * Copyright 2017 Iwan Timmer (irtimmer@gmail.com)
 *
 * Portions derived from code by Phil Karn (karn@ka9q.ampr.org),
 * Robert Morelos-Zaragoza (robert@spectra.eng.hawaii.edu) and Hari
 * Thirumoorthy (harit@spectra.eng.hawaii.edu), Aug 1995
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
// SPDX-License-Identifier: BSD-2-Clause

#ifndef __RS_H_
#define __RS_H_

#ifdef __cplusplus
extern "C"
{
#endif

/* use small value to save memory */
#define DATA_SHARDS_MAX 2048

	typedef struct _reed_solomon
	{
		int data_shards;
		int parity_shards;
		int shards;
		unsigned char * m;
		unsigned char * parity;
	} reed_solomon;

	/**
	 * MUST initial one time
	 * */
	void
	reed_solomon_init(void);

	reed_solomon *
	reed_solomon_new(int data_shards, int parity_shards);
	void
	reed_solomon_release(reed_solomon * rs);

	/**
	 * encode a big size of buffer
	 * input:
	 * rs
	 * nr_shards: assert(0 == nr_shards % rs->data_shards)
	 * shards[nr_shards][block_size]
	 * */
	int
	reed_solomon_encode(reed_solomon * rs, unsigned char ** shards, int nr_shards, int block_size);

	/**
	 * reconstruct a big size of buffer
	 * input:
	 * rs
	 * nr_shards: assert(0 == nr_shards % rs->data_shards)
	 * shards[nr_shards][block_size]
	 * marks[nr_shards] marks as errors
	 * */
	int
	reed_solomon_reconstruct(reed_solomon * rs, unsigned char ** shards, unsigned char * marks, int nr_shards, int block_size);

#ifdef __cplusplus
};
#endif
#endif

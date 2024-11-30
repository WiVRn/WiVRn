/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * Ported from libgcrypt to openssl, intially from libotr:
 *  Off-the-Record Messaging library
 *  Copyright (C) 2004-2014  Ian Goldberg, David Goulet, Rob Smits,
 *                           Chris Alexander, Willy Lew, Lisa Du,
 *                           Nikita Borisov
 *                           <otr@cypherpunks.ca>
 */

#pragma once

#include <array>
#include <cassert>
#include <memory>
#include <openssl/bn.h>
#include <string>

namespace crypto
{

struct deleter
{
	void operator()(BIGNUM * bn);
	void operator()(EVP_MD_CTX * bn);
};

class bignum
{
	std::unique_ptr<BIGNUM, deleter> number;

public:
	bignum() = default;
	bignum(const bignum & other);
	bignum(bignum && other) = default;
	bignum & operator=(const bignum & other);
	bignum & operator=(bignum && other) = default;

	bignum(int64_t);
	static bignum from_hex(const char *);
	static bignum from_data(const std::string & value);
	static bignum from_mpi(const std::string & value);

	std::string to_hex() const;
	std::string to_mpi() const;
	std::string to_data() const;
	size_t data_size() const
	{
		return BN_num_bytes(**this);
	}

	BIGNUM * operator*()
	{
		if (!number)
			number.reset(BN_new());

		return number.get();
	}

	const BIGNUM * operator*() const
	{
		assert(number);

		return number.get();
	}

	bool is_valid() const
	{
		return number.get();
	}
};

bignum operator-(const bignum & a, const bignum & b);

class smp_cheated : public std::runtime_error
{
public:
	smp_cheated() :
	        std::runtime_error("Some verification failed") {}
};

class smp
{
public:
	using msg1 = std::array<bignum, 6>;  // [0] = g2a, [1] = c2, [2] = d2, [3] = g3a, [4] = c3, [5] = d3
	using msg2 = std::array<bignum, 11>; // [0] = g2b, [1] = c2, [2] = d2, [3] = g3b, [4] = c3, [5] = d3, [6] = pb, [7] = qb, [8] = cp, [9] = d5, [10] = d6
	using msg3 = std::array<bignum, 8>;  // [0] = pa,  [1] = qa, [2] = cp, [3] = d5,  [4] = d6, [5] = ra, [6] = cr, [7] = d7
	using msg4 = std::array<bignum, 3>;  // [0] = rb,  [1] = cr, [2] = d7

	// Constants
	static const int SM_MOD_LEN_BITS = 1536;
	static const int SM_MOD_LEN_BYTES = 192;

	// The modulus p
	static inline bignum SM_MODULUS = bignum::from_hex(
	        "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	        "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	        "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	        "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	        "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
	        "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
	        "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
	        "670C354E4ABC9804F1746C08CA237327FFFFFFFFFFFFFFFF");

	// The order of the group q = (p-1)/2
	static inline bignum SM_ORDER = bignum::from_hex(
	        "7FFFFFFFFFFFFFFFE487ED5110B4611A62633145C06E0E68"
	        "948127044533E63A0105DF531D89CD9128A5043CC71A026E"
	        "F7CA8CD9E69D218D98158536F92F8A1BA7F09AB6B6A8E122"
	        "F242DABB312F3F637A262174D31BF6B585FFAE5B7A035BF6"
	        "F71C35FDAD44CFD2D74F9208BE258FF324943328F6722D9E"
	        "E1003E5C50B1DF82CC6D241B0E2AE9CD348B1FD47E9267AF"
	        "C1B2AE91EE51D6CB0E3179AB1042A95DCF6A9483B84B4B36"
	        "B3861AA7255E4C0278BA36046511B993FFFFFFFFFFFFFFFF");

	static inline bignum SM_GENERATOR = 0x02;

	static inline bignum SM_MODULUS_MINUS_2 = SM_MODULUS - bignum(2);

private:
	bignum secret;
	bignum x2;
	bignum x3;
	bignum g1 = SM_GENERATOR;
	bignum g2;
	bignum g3;
	bignum g3o;
	bignum p;
	bignum q;
	bignum pab;
	bignum qab;

	static bignum hash(int version, const bignum & a, const bignum * b);
	static bool check_group_elem(const bignum & g);
	static int check_expon(const bignum & x);

	std::tuple<bignum, bignum, bignum> /*c, d1, d2 */ proof_equal_coords(const bignum & r, int version);
	static std::pair<bignum, bignum> /* c, d */ proof_know_log(const bignum & g, const bignum & x, int version);
	static std::strong_ordering check_know_log(const bignum & c, const bignum & d, const bignum & g, const bignum & x, int version);
	std::strong_ordering check_equal_coords(const bignum & c,
	                                        const bignum & d1,
	                                        const bignum & d2,
	                                        const bignum & p,
	                                        const bignum & q,
	                                        int version);
	std::pair<bignum, bignum> /* c, d */ proof_equal_logs(int version);
	std::strong_ordering check_equal_logs(const bignum & c,
	                                      const bignum & d,
	                                      const bignum & r,
	                                      int version);

	void reset();

public:
	// executed by alice
	msg1 step1(const std::string & secret);

	// executed by bob
	void step2a(const msg1 & input);
	msg2 step2b(const std::string & secret);

	msg2 step2(const msg1 & input, const std::string & secret)
	{
		step2a(input);
		return step2b(secret);
	}

	// executed by alice
	msg3 step3(const msg2 & input);

	// executed by bob, returns true in the second member if secrets match
	std::pair<msg4, bool> step4(const msg3 & input);

	// executed by alice, returns true if secrets match
	bool step5(const msg4 & input);
};

} // namespace crypto

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

#include "smp.h"
#include <compare>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>

static void throw_openssl_error()
{
	char buf[256];
	ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
	throw std::runtime_error(buf);
}

namespace crypto
{
void deleter::operator()(BIGNUM * bn)
{
	BN_free(bn);
}

void deleter::operator()(EVP_MD_CTX * ctx)
{
	EVP_MD_CTX_destroy(ctx);
}

bignum::bignum(const bignum & other) :
        number(other.number ? BN_dup(*other) : nullptr)
{}

bignum & bignum::operator=(const bignum & other)
{
	number.reset(other.number ? BN_dup(*other) : nullptr);
	return *this;
}

bignum::bignum(int64_t value)
{
	BIGNUM * bn = BN_new();
	if (BN_set_word(bn, value) == 0)
		throw_openssl_error();
	number.reset(bn);
}

bignum bignum::from_hex(const char * value)
{
	BIGNUM * bn{};
	if (BN_hex2bn(&bn, value) == 0)
		throw_openssl_error();

	bignum bn2;
	bn2.number.reset(bn);
	return bn2;
}

bignum bignum::from_data(const std::string & value)
{
	BIGNUM * bn = BN_bin2bn(reinterpret_cast<const unsigned char *>(value.data()), value.size(), nullptr);

	if (not bn)
		throw_openssl_error();

	bignum bn2;
	bn2.number.reset(bn);
	return bn2;
}

std::string bignum::to_mpi() const
{
	std::string output;
	output.resize(BN_bn2mpi(**this, nullptr));
	BN_bn2mpi(**this, reinterpret_cast<unsigned char *>(output.data()));
	return output;
}

std::string bignum::to_data() const
{
	std::string output;
	output.resize(BN_num_bytes(**this));
	BN_bn2bin(**this, reinterpret_cast<unsigned char *>(output.data()));
	return output;
}

std::string bignum::to_hex() const
{
	char * hex = BN_bn2hex(**this);
	std::string output = hex;
	OPENSSL_free(hex);
	return output;
}

bignum bignum::from_mpi(const std::string & value)
{
	BIGNUM * bn = BN_mpi2bn(reinterpret_cast<const unsigned char *>(value.data()), value.size(), nullptr);

	if (not bn)
		throw_openssl_error();

	bignum bn2;
	bn2.number.reset(bn);
	return bn2;
}

static BN_CTX * bn_ctx()
{
	thread_local BN_CTX * ctx = BN_CTX_new();
	return ctx;
}

bignum operator-(const bignum & a, const bignum & b)
{
	bignum r;
	if (BN_sub(*r, *a, *b) == 0)
		throw_openssl_error();
	return r;
}

std::strong_ordering operator<=>(const bignum & a, const bignum & b)
{
	int cmp = BN_cmp(*a, *b);
	if (cmp < 0)
		return std::strong_ordering::less;
	if (cmp > 0)
		return std::strong_ordering::greater;
	return std::strong_ordering::equal;
}

bool operator==(const bignum & a, const bignum & b)
{
	return BN_cmp(*a, *b) == 0;
}

bignum powm(const bignum & b, const bignum & e, const bignum & m)
{
	bignum r;
	if (BN_mod_exp(*r, *b, *e, *m, bn_ctx()) == 0)
		throw_openssl_error();

	return r;
}

bignum mulm(const bignum & a, const bignum & b, const bignum & m)
{
	bignum r;
	if (BN_mod_mul(*r, *a, *b, *m, bn_ctx()) == 0)
		throw_openssl_error();

	return r;
}

bignum subm(const bignum & a, const bignum & b, const bignum & m)
{
	bignum r;
	if (BN_mod_sub(*r, *a, *b, *m, bn_ctx()) == 0)
		throw_openssl_error();

	return r;
}

bignum invm(const bignum & a, const bignum & n)
{
	bignum r;
	if (BN_mod_inverse(*r, *a, *n, bn_ctx()) == 0)
		throw_openssl_error();

	return r;
}

static bignum random_exponent()
{
	/* Generate a random exponent */
	bignum randexpon;
	BN_rand(*randexpon, smp::SM_MOD_LEN_BITS, -1, 0);

	return randexpon;
}

// Hash one or two bignums. To hash only one bignum, b may be set to nullptr.
bignum smp::hash(int version, const bignum & a, const bignum * b)
{
	std::string input = std::string{1, (char)version} +
	                    a.to_mpi() +
	                    (b ? b->to_mpi() : "");

	std::unique_ptr<EVP_MD_CTX, deleter> ctx{EVP_MD_CTX_create()};

	if (EVP_DigestInit(ctx.get(), EVP_sha256()) == 0)
		throw_openssl_error();

	if (EVP_DigestUpdate(ctx.get(), input.data(), input.size()) == 0)
		throw_openssl_error();

	std::string output;
	output.resize(EVP_MD_CTX_size(ctx.get()));
	if (EVP_DigestFinal(ctx.get(), reinterpret_cast<unsigned char *>(output.data()), nullptr) == 0)
		throw_openssl_error();

	return bignum::from_data(output);
}

// Check that a bignum is in the right range to be a (non-unit) group element
bool smp::check_group_elem(const bignum & g)
{
	return g < 2 or g > smp::SM_MODULUS_MINUS_2;
}

// Check that a bignum is in the right range to be a (non-zero) exponent
int smp::check_expon(const bignum & x)
{
	return x < 1 or x >= smp::SM_ORDER;
}

// Proof of knowledge of a discrete logarithm
std::pair<bignum, bignum> smp::proof_know_log(const bignum & g, const bignum & x, int version)
{
	bignum r = random_exponent();
	bignum temp = powm(g, r, smp::SM_MODULUS);

	bignum c = hash(version, temp, NULL);

	temp = mulm(x, c, smp::SM_ORDER);

	bignum d = subm(r, temp, smp::SM_ORDER);

	return {c, d};
}

// Verify a proof of knowledge of a discrete logarithm.
// Checks that c = h(g^d x^c)
std::strong_ordering smp::check_know_log(const bignum & c, const bignum & d, const bignum & g, const bignum & x, int version)
{
	bignum gd = powm(g, d, smp::SM_MODULUS);     // g^d
	bignum xc = powm(x, c, smp::SM_MODULUS);     // x^c
	bignum gdxc = mulm(gd, xc, smp::SM_MODULUS); // (g^d x^c)
	bignum hgdxc = hash(version, gdxc, nullptr); // h(g^d x^c)

	return hgdxc <=> c;
}

// Proof of knowledge of coordinates with first components being equal
std::tuple<bignum, bignum, bignum> smp::proof_equal_coords(const bignum & r, int version)
{
	bignum r1 = random_exponent();
	bignum r2 = random_exponent();

	/* Compute the value of c, as c = h(g3^r1, g1^r1 g2^r2) */
	bignum temp1 = powm(g1, r1, SM_MODULUS);
	bignum temp2 = powm(g2, r2, SM_MODULUS);
	temp2 = mulm(temp1, temp2, SM_MODULUS);
	temp1 = powm(g3, r1, SM_MODULUS);
	bignum c = hash(version, temp1, &temp2);

	/* Compute the d values, as d1 = r1 - r c, d2 = r2 - secret c */
	temp1 = mulm(r, c, SM_ORDER);
	bignum d1 = subm(r1, temp1, SM_ORDER);

	temp1 = mulm(secret, c, SM_ORDER);
	bignum d2 = subm(r2, temp1, SM_ORDER);

	/* All clear */
	return {c, d1, d2};
}

// Verify a proof of knowledge of coordinates with first components being equal
std::strong_ordering smp::check_equal_coords(const bignum & c,
                                             const bignum & d1,
                                             const bignum & d2,
                                             const bignum & p,
                                             const bignum & q,
                                             int version)
{
	/* To verify, we test that hash(g3^d1 * p^c, g1^d1 * g2^d2 * q^c) = c
	 * If indeed c = hash(g3^r1, g1^r1 g2^r2), d1 = r1 - r*c,
	 * d2 = r2 - secret*c.  And if indeed p = g3^r, q = g1^r * g2^secret
	 * Then we should have that:
	 *   hash(g3^d1 * p^c, g1^d1 * g2^d2 * q^c)
	 * = hash(g3^(r1 - r*c + r*c), g1^(r1 - r*c + q*c) *
	 *      g2^(r2 - secret*c + secret*c))
	 * = hash(g3^r1, g1^r1 g2^r2)
	 * = c
	 */
	bignum temp2 = powm(g3, d1, SM_MODULUS);
	bignum temp3 = powm(p, c, SM_MODULUS);
	bignum temp1 = mulm(temp2, temp3, SM_MODULUS);

	temp2 = powm(g1, d1, SM_MODULUS);
	temp3 = powm(g2, d2, SM_MODULUS);
	temp2 = mulm(temp2, temp3, SM_MODULUS);
	temp3 = powm(q, c, SM_MODULUS);
	temp2 = mulm(temp3, temp2, SM_MODULUS);

	bignum cprime = hash(version, temp1, &temp2);

	return c <=> cprime;
}

// Proof of knowledge of logs with exponents being equal
std::pair<bignum, bignum> smp::proof_equal_logs(int version)
{
	bignum r = random_exponent();

	/* Compute the value of c, as c = h(g1^r, (Qa/Qb)^r) */
	bignum temp1 = powm(g1, r, SM_MODULUS);
	bignum temp2 = powm(qab, r, SM_MODULUS);
	bignum c = hash(version, temp1, &temp2);

	/* Compute the d values, as d = r - x3 c */
	temp1 = mulm(x3, c, SM_ORDER);
	bignum d = subm(r, temp1, SM_ORDER);

	/* All clear */
	return {c, d};
}

// Verify a proof of knowledge of logs with exponents being equal
std::strong_ordering smp::check_equal_logs(const bignum & c,
                                           const bignum & d,
                                           const bignum & r,
                                           int version)
{
	/* Here, we recall the exponents used to create g3.
	 * If we have previously seen g3o = g1^x where x is unknown
	 * during the DH exchange to produce g3, then we may proceed with:
	 *
	 * To verify, we test that hash(g1^d * g3o^c, qab^d * r^c) = c
	 * If indeed c = hash(g1^r1, qab^r1), d = r1- x * c
	 * And if indeed r = qab^x
	 * Then we should have that:
	 *   hash(g1^d * g3o^c, qab^d r^c)
	 * = hash(g1^(r1 - x*c + x*c), qab^(r1 - x*c + x*c))
	 * = hash(g1^r1, qab^r1)
	 * = c
	 */
	bignum temp2 = powm(g1, d, SM_MODULUS);
	bignum temp3 = powm(g3o, c, SM_MODULUS);
	bignum temp1 = mulm(temp2, temp3, SM_MODULUS);

	temp3 = powm(qab, d, SM_MODULUS);
	temp2 = powm(r, c, SM_MODULUS);
	temp2 = mulm(temp3, temp2, SM_MODULUS);

	bignum cprime = hash(version, temp1, &temp2);

	return c <=> cprime;
}

void smp::reset()
{
	secret = bignum{};
	x2 = bignum{};
	x3 = bignum{};
	g1 = SM_GENERATOR;
	g2 = bignum{};
	g3 = bignum{};
	g3o = bignum{};
	p = bignum{};
	q = bignum{};
	pab = bignum{};
	qab = bignum{};
}

/* Create first message in SMP exchange.  Input is Alice's secret value
 * which this protocol aims to compare to Bob's.  Output is a
 * bignum array whose elements correspond to the following:
 * [0] = g2a, Alice's half of DH exchange to determine g2
 * [1] = c2, [2] = d2, Alice's ZK proof of knowledge of g2a exponent
 * [3] = g3a, Alice's half of DH exchange to determine g3
 * [4] = c3, [5] = d3, Alice's ZK proof of knowledge of g3a exponent */
smp::msg1 smp::step1(const std::string & secret_str)
{
	reset();

	// Initialize the sm state or update the secret
	secret = bignum::from_data(secret_str);

	msg1 output;

	x2 = random_exponent();
	x3 = random_exponent();

	output[0] = powm(g1, x2, SM_MODULUS);

	std::tie(output[1], output[2]) = proof_know_log(g1, x2, 1);

	output[3] = powm(g1, x3, SM_MODULUS);

	std::tie(output[4], output[5]) = proof_know_log(g1, x3, 2);

	return output;
}

/* Receive the first message in SMP exchange, which was generated by
 * otrl_sm_step1.  Input is saved until the user inputs their secret
 * information.  No output. */
void smp::step2a(const msg1 & input)
{
	reset();

	if (check_group_elem(input[0]) or check_expon(input[2]) or
	    check_group_elem(input[3]) or check_expon(input[5]))
		throw smp_cheated{};

	// Store Alice's g3a value for later in the protocol
	g3o = input[3];

	// Verify Alice's proofs
	if (check_know_log(input[1], input[2], g1, input[0], 1) != std::strong_ordering::equal or
	    check_know_log(input[4], input[5], g1, input[3], 2) != std::strong_ordering::equal)
		throw smp_cheated{};

	// Create Bob's half of the generators g2 and g3
	x2 = random_exponent();
	x3 = random_exponent();

	// Combine the two halves from Bob and Alice and determine g2 and g3
	g2 = powm(input[0], x2, SM_MODULUS);
	g3 = powm(input[3], x3, SM_MODULUS);
}

/* Create second message in SMP exchange.  Input is Bob's secret value.
 * Information from earlier steps in the exchange is taken from Bob's
 * state.  Output is a bignum array whose elements correspond
 * to the following:
 * [0] = g2b, Bob's half of DH exchange to determine g2
 * [1] = c2, [2] = d2, Bob's ZK proof of knowledge of g2b exponent
 * [3] = g3b, Bob's half of DH exchange to determine g3
 * [4] = c3, [5] = d3, Bob's ZK proof of knowledge of g3b exponent
 * [6] = pb, [7] = qb, Bob's halves of the (Pa/Pb) and (Qa/Qb) values
 * [8] = cp, [9] = d5, [10] = d6, Bob's ZK proof that pb, qb formed correctly */
smp::msg2 smp::step2b(const std::string & secret_str)
{
	// Convert the given secret to the proper form and store it
	secret = bignum::from_data(secret_str);

	msg2 output;

	output[0] = powm(g1, x2, SM_MODULUS);
	std::tie(output[1], output[2]) = proof_know_log(g1, x2, 3);
	output[3] = powm(g1, x3, SM_MODULUS);
	std::tie(output[4], output[5]) = proof_know_log(g1, x3, 4);

	// Calculate P and Q values for Bob
	bignum r = random_exponent();
	bignum qb1;
	bignum qb2;

	p = powm(g3, r, SM_MODULUS);
	output[6] = p;
	qb1 = powm(g1, r, SM_MODULUS);
	qb2 = powm(g2, secret, SM_MODULUS);
	q = mulm(qb1, qb2, SM_MODULUS);
	output[7] = q;

	std::tie(output[8], output[9], output[10]) = proof_equal_coords(r, 5);

	return output;
}

/* Create third message in SMP exchange.  Input is a message generated
 * by otrl_sm_step2b. Output is a bignum array whose elements
 * correspond to the following:
 * [0] = pa, [1] = qa, Alice's halves of the (Pa/Pb) and (Qa/Qb) values
 * [2] = cp, [3] = d5, [4] = d6, Alice's ZK proof that pa, qa formed correctly
 * [5] = ra, calculated as (Qa/Qb)^x3 where x3 is the exponent used in g3a
 * [6] = cr, [7] = d7, Alice's ZK proof that ra is formed correctly */
smp::msg3 smp::step3(const msg2 & input)
{
	if (check_group_elem(input[0]) or check_group_elem(input[3]) or
	    check_group_elem(input[6]) or check_group_elem(input[7]) or
	    check_expon(input[2]) or check_expon(input[5]) or
	    check_expon(input[9]) or check_expon(input[10]))
		throw smp_cheated{};

	msg3 output;

	// Store Bob's g3a value for later in the protocol
	g3o = input[3];

	// Verify Bob's knowledge of discrete log proofs
	if (check_know_log(input[1], input[2], g1, input[0], 3) != std::strong_ordering::equal or
	    check_know_log(input[4], input[5], g1, input[3], 4) != std::strong_ordering::equal)
		throw smp_cheated{};

	// Combine the two halves from Bob and Alice and determine g2 and g3
	g2 = powm(input[0], x2, SM_MODULUS);
	g3 = powm(input[3], x3, SM_MODULUS);

	// Verify Bob's coordinate equality proof
	if (check_equal_coords(input[8], input[9], input[10], input[6], input[7], 5) != std::strong_ordering::equal)
		throw smp_cheated{};

	// Calculate P and Q values for Alice
	bignum r = random_exponent();
	p = powm(g3, r, SM_MODULUS);
	output[0] = p;
	bignum qa1 = powm(g1, r, SM_MODULUS);
	bignum qa2 = powm(g2, secret, SM_MODULUS);
	q = mulm(qa1, qa2, SM_MODULUS);
	output[1] = q;

	std::tie(output[2], output[3], output[4]) = proof_equal_coords(r, 6);

	// Calculate Ra and proof
	pab = mulm(p, invm(input[6], SM_MODULUS), SM_MODULUS);
	qab = mulm(q, invm(input[7], SM_MODULUS), SM_MODULUS);
	output[5] = powm(qab, x3, SM_MODULUS);
	std::tie(output[6], output[7]) = proof_equal_logs(7);

	return output;
}

// Create final message in SMP exchange.  Input is a message generated
// by otrl_sm_step3. Output is a bignum array whose elements
// correspond to the following:
// [0] = rb, calculated as (Qa/Qb)^x3 where x3 is the exponent used in g3b
// [1] = cr, [2] = d7, Bob's ZK proof that rb is formed correctly
// This method also checks if Alice and Bob's secrets were the same.  If
// so, it returns NO_ERROR.  If the secrets differ, an INV_VALUE error is
// returned instead.
std::pair<smp::msg4, bool> smp::step4(const msg3 & input)
{
	if (check_group_elem(input[0]) or check_group_elem(input[1]) or
	    check_group_elem(input[5]) or check_expon(input[3]) or
	    check_expon(input[4]) or check_expon(input[7]))
		throw smp_cheated{};

	msg4 output;

	// Verify Alice's coordinate equality proof
	if (check_equal_coords(input[2], input[3], input[4], input[0], input[1], 6) != std::strong_ordering::equal)
		throw smp_cheated{};

	// Find Pa/Pb and Qa/Qb
	pab = mulm(input[0], invm(p, SM_MODULUS), SM_MODULUS);
	qab = mulm(input[1], invm(q, SM_MODULUS), SM_MODULUS);

	// Verify Alice's log equality proof
	if (check_equal_logs(input[6], input[7], input[5], 7) != std::strong_ordering::equal)
		throw smp_cheated{};

	// Calculate Rb and proof
	output[0] = powm(qab, x3, SM_MODULUS);
	std::tie(output[1], output[2]) = proof_equal_logs(8);

	// Calculate Rab and verify that secrets match
	bignum rab = powm(input[5], x3, SM_MODULUS);

	return {output, rab == pab};
}

/* Receives the final SMP message, which was generated in otrl_sm_step.
 * This method checks if Alice and Bob's secrets were the same.  If
 * so, it returns NO_ERROR.  If the secrets differ, an INV_VALUE error is
 * returned instead. */
bool smp::step5(const msg4 & input)
{
	if (check_group_elem(input[0]) or check_expon(input[2]))
		throw smp_cheated{};

	// Verify Bob's log equality proof
	if (check_equal_logs(input[1], input[2], input[0], 8) != std::strong_ordering::equal)
		throw smp_cheated{};

	// Calculate Rab and verify that secrets match
	bignum rab = powm(input[0], x3, SM_MODULUS);

	return rab == pab;
}

} // namespace crypto

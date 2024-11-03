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
 */

#include "crypto.h"

#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/pem.h>
// #include <openssl/thread.h>
#include <stdexcept>
#include <string>

namespace
{
void throw_openssl_error()
{
	char buf[256];
	ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
	throw std::runtime_error(buf);
}

struct bio
{
	BIO * mem = nullptr;

	bio()
	{
		mem = BIO_new(BIO_s_mem());
	}

	bio(std::string_view data)
	{
		mem = BIO_new_mem_buf(data.data(), data.size());
		if (!mem)
			throw_openssl_error();
	}

	bio(const bio &) = delete;
	bio & operator=(const bio &) = delete;

	~bio()
	{
		BIO_free(mem);
	}

	operator BIO *()
	{
		return mem;
	}

	operator std::string_view()
	{
		char * data = nullptr;
		size_t size = BIO_get_mem_data(mem, &data);

		return {data, size};
	}

	operator std::string()
	{
		char * data = nullptr;
		size_t size = BIO_get_mem_data(mem, &data);

		return {data, size};
	}
};
} // namespace

namespace crypto::details
{
class key_context
{
	EVP_PKEY_CTX * ctx = nullptr;

public:
	key_context() = default;
	key_context(const key_context &) = delete;
	key_context & operator=(const key_context &) = delete;
	key_context(key_context && other) noexcept :
	        ctx(other.ctx)
	{
		other.ctx = nullptr;
	}

	key_context & operator=(key_context && other) noexcept
	{
		std::swap(ctx, other.ctx);
		return *this;
	}

	~key_context()
	{
		EVP_PKEY_CTX_free(ctx);
	}

	key_context(EVP_PKEY_CTX * ctx) :
	        ctx(ctx) {}

	static key_context from_id(int id)
	{
		key_context ctx{EVP_PKEY_CTX_new_id(id, nullptr)};
		if (!ctx)
			throw_openssl_error();
		return ctx;
	}

	static key_context from_key(key & k)
	{
		key_context ctx{EVP_PKEY_CTX_new(k, nullptr)};
		if (!ctx)
			throw_openssl_error();
		return ctx;
	}

	operator EVP_PKEY_CTX *()
	{
		return ctx;
	}

	operator bool() const
	{
		return ctx != nullptr;
	}
};

class kdf_context
{
	EVP_KDF * kdf = nullptr;
	EVP_KDF_CTX * ctx = nullptr;

public:
	kdf_context() = default;
	kdf_context(const kdf_context &) = delete;
	kdf_context & operator=(const kdf_context &) = delete;
	kdf_context(kdf_context && other) noexcept :
	        ctx(other.ctx)
	{
		other.ctx = nullptr;
	}

	kdf_context & operator=(kdf_context && other) noexcept
	{
		std::swap(ctx, other.ctx);
		return *this;
	}

	~kdf_context()
	{
		EVP_KDF_CTX_free(ctx);
		EVP_KDF_free(kdf);
	}

	kdf_context(const char * algorithm)
	{
		kdf = EVP_KDF_fetch(nullptr, algorithm /*"ARGON2D"*/, nullptr);
		if (!kdf)
			throw_openssl_error();

		ctx = EVP_KDF_CTX_new(kdf);
		if (!ctx)
		{
			EVP_KDF_free(kdf);
			throw_openssl_error();
		}
	}

	operator EVP_KDF_CTX *()
	{
		return ctx;
	}

	operator bool() const
	{
		return ctx != nullptr;
	}
};

} // namespace crypto::details

namespace crypto
{
key key::from_public_key(std::string_view pem)
{
	bio mem{pem};

	key pkey{PEM_read_bio_PUBKEY(mem, nullptr, nullptr, nullptr)};

	if (!pkey.pkey)
		throw_openssl_error();

	return pkey;
}

key key::from_private_key(std::string_view pem)
{
	bio mem{pem};

	key pkey{PEM_read_bio_PrivateKey(mem, nullptr, nullptr, nullptr)};

	if (!pkey.pkey)
		throw_openssl_error();

	return pkey;
}

std::string key::public_key() const
{
	bio mem;
	PEM_write_bio_PUBKEY(mem, pkey);
	return mem;
}

std::string key::private_key() const
{
	bio mem;
	PEM_write_bio_PrivateKey(mem, pkey, nullptr, nullptr, 0, nullptr, nullptr);
	return mem;
}

key key::generate_rsa_keypair(int bits)
{
	auto ctx = details::key_context::from_id(EVP_PKEY_RSA);

	if (EVP_PKEY_keygen_init(ctx) <= 0)
		throw_openssl_error();

	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0)
		throw_openssl_error();

	EVP_PKEY * pkey = nullptr;
	if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
		throw_openssl_error();

	return pkey;
}

key key::generate_x25519_keypair()
{
	auto ctx = details::key_context::from_id(EVP_PKEY_X25519);

	if (EVP_PKEY_keygen_init(ctx) <= 0)
		throw_openssl_error();

	EVP_PKEY * pkey = nullptr;
	if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
		throw_openssl_error();

	return pkey;
}

key key::generate_x448_keypair()
{
	auto ctx = details::key_context::from_id(EVP_PKEY_X448);

	if (EVP_PKEY_keygen_init(ctx) <= 0)
		throw_openssl_error();

	EVP_PKEY * pkey = nullptr;
	if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
		throw_openssl_error();

	return pkey;
}

std::vector<uint8_t> key::diffie_hellman(key & my_key, key & peer_key)
{
	auto ctx = details::key_context::from_key(my_key);

	if (EVP_PKEY_derive_init(ctx) <= 0)
		throw_openssl_error();

	if (EVP_PKEY_derive_set_peer(ctx, peer_key) <= 0)
		throw_openssl_error();

	size_t skeylen = 0;
	if (EVP_PKEY_derive(ctx, NULL, &skeylen) <= 0)
		throw_openssl_error();

	std::vector<uint8_t> skey;
	skey.resize(skeylen);

	if (EVP_PKEY_derive(ctx, skey.data(), &skeylen) <= 0)
		throw_openssl_error();

	return skey;
}

key::wrapped_secret key::encapsulate()
{
	auto ctx = details::key_context::from_key(*this);

	if (EVP_PKEY_encapsulate_init(ctx, nullptr) <= 0)
		throw_openssl_error();

	if (EVP_PKEY_CTX_set_kem_op(ctx, "RSASVE") <= 0)
		throw_openssl_error();

	size_t wrapped_len = 0, secret_len = 0;
	if (EVP_PKEY_encapsulate(ctx, nullptr, &wrapped_len, nullptr, &secret_len) <= 0)
		throw_openssl_error();

	wrapped_secret ws;

	ws.wrapped.resize(wrapped_len);
	ws.secret.resize(secret_len);

	if (EVP_PKEY_encapsulate(ctx, ws.wrapped.data(), &wrapped_len, ws.secret.data(), &secret_len) <= 0)
		throw_openssl_error();

	return ws;
}

std::vector<uint8_t> key::decapsulate(std::span<uint8_t> wrapped)
{
	auto ctx = details::key_context::from_key(*this);

	if (EVP_PKEY_decapsulate_init(ctx, nullptr) <= 0)
		throw_openssl_error();

	if (EVP_PKEY_CTX_set_kem_op(ctx, "RSASVE") <= 0)
		throw_openssl_error();

	size_t secret_len = 0;
	if (EVP_PKEY_decapsulate(ctx, nullptr, &secret_len, wrapped.data(), wrapped.size()) <= 0)
		throw_openssl_error();

	std::vector<uint8_t> secret;
	secret.resize(secret_len);

	if (EVP_PKEY_decapsulate(ctx, secret.data(), &secret_len, wrapped.data(), wrapped.size()) <= 0)
		throw_openssl_error();

	return secret;
}

void cipher_context::set_key(std::span<uint8_t> key)
{
	if (not ctx)
		throw std::invalid_argument("Uninitalized context");

	if (key.size() != key_length())
		throw std::invalid_argument("Wrong key length, expected " + std::to_string(key_length()) + ", got " + std::to_string(key.size()));

	if (not EVP_CipherInit_ex2(ctx, nullptr, key.data(), nullptr, -1, nullptr))
		throw_openssl_error();
}

void cipher_context::set_iv(std::span<uint8_t> iv)
{
	if (not ctx)
		throw std::invalid_argument("Uninitalized context");

	if (iv.size() != iv_length())
		throw std::invalid_argument("Wrong IV length, expected" + std::to_string(iv_length()) + ", got " + std::to_string(iv.size()));

	if (not EVP_CipherInit_ex2(ctx, nullptr, nullptr, iv.data(), -1, nullptr))
		throw_openssl_error();
}

void cipher_context::set_key_and_iv(std::span<uint8_t> key, std::span<uint8_t> iv)
{
	if (not ctx)
		throw std::invalid_argument("Uninitalized context");

	if (key.size() != key_length())
		throw std::invalid_argument("Wrong key length, expected " + std::to_string(key_length()) + ", got " + std::to_string(key.size()));

	if (iv.size() != iv_length())
		throw std::invalid_argument("Wrong IV length, expected" + std::to_string(iv_length()) + ", got " + std::to_string(iv.size()));

	if (not EVP_CipherInit_ex2(ctx, nullptr, key.data(), iv.data(), -1, nullptr))
		throw_openssl_error();
}

encrypt_context::encrypt_context(const EVP_CIPHER * cipher)
{
	ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		throw_openssl_error();

	if (not EVP_EncryptInit_ex2(ctx, cipher, nullptr, nullptr, nullptr))
		throw_openssl_error();

	key_length_ = EVP_CIPHER_CTX_get_key_length(ctx);
	iv_length_ = EVP_CIPHER_CTX_get_iv_length(ctx);
	block_size_ = EVP_CIPHER_CTX_get_block_size(ctx);
}

// TODO handle AES-GCM correctly
std::vector<uint8_t> encrypt_context::encrypt(std::span<uint8_t> plaintext)
{
	if (not EVP_EncryptInit_ex2(ctx, nullptr, nullptr, nullptr, nullptr))
		throw_openssl_error();

	std::vector<uint8_t> ciphertext;
	ciphertext.resize(plaintext.size() + block_size());

	int size_out = 0;
	if (not EVP_EncryptUpdate(ctx, ciphertext.data(), &size_out, plaintext.data(), plaintext.size()))
		throw_openssl_error();

	int size_out2 = 0;
	if (not EVP_EncryptFinal_ex(ctx, plaintext.data() + size_out, &size_out2))
		throw_openssl_error();

	ciphertext.resize(size_out + size_out2);
	return ciphertext;
}

void encrypt_context::encrypt_in_place(std::span<uint8_t> text)
{
	if (block_size() != 1)
		throw std::runtime_error("Not a stream cipher");

	int size_out = text.size();
	if (not EVP_EncryptUpdate(ctx, text.data(), &size_out, text.data(), text.size()))
		throw_openssl_error();
}

void encrypt_context::encrypt_in_place(std::span<std::span<uint8_t>> text)
{
	if (block_size() != 1)
		throw std::runtime_error("Not a stream cipher");

	for (std::span<uint8_t> i: text)
	{
		int size_out = i.size();
		if (not EVP_EncryptUpdate(ctx, i.data(), &size_out, i.data(), i.size()))
			throw_openssl_error();
	}
}

decrypt_context::decrypt_context(const EVP_CIPHER * cipher)
{
	ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		throw_openssl_error();

	if (not EVP_DecryptInit(ctx, cipher, nullptr, nullptr))
		throw_openssl_error();

	key_length_ = EVP_CIPHER_CTX_get_key_length(ctx);
	iv_length_ = EVP_CIPHER_CTX_get_iv_length(ctx);
	block_size_ = EVP_CIPHER_CTX_get_block_size(ctx);
}

std::vector<uint8_t> decrypt_context::decrypt(std::span<uint8_t> ciphertext)
{
	if (not EVP_DecryptInit_ex2(ctx, nullptr, nullptr, nullptr, nullptr))
		throw_openssl_error();

	std::vector<uint8_t> plaintext;
	plaintext.resize(ciphertext.size() + block_size());

	int size_out = 0;
	if (not EVP_DecryptUpdate(ctx, plaintext.data(), &size_out, ciphertext.data(), ciphertext.size()))
		throw_openssl_error();

	int size_out2 = 0;
	if (not EVP_DecryptFinal_ex(ctx, ciphertext.data() + size_out, &size_out2))
		throw_openssl_error();

	plaintext.resize(size_out + size_out2);
	return plaintext;
}

void decrypt_context::decrypt_in_place(std::span<uint8_t> text)
{
	if (block_size() != 1)
		throw std::runtime_error("Not a stream cipher");

	int size_out = text.size();
	if (not EVP_DecryptUpdate(ctx, text.data(), &size_out, text.data(), text.size()))
		throw_openssl_error();
}

void decrypt_context::decrypt_in_place(std::span<std::span<uint8_t>> text)
{
	if (block_size() != 1)
		throw std::runtime_error("Not a stream cipher");

	for (std::span<uint8_t> i: text)
	{
		int size_out = i.size();
		if (not EVP_DecryptUpdate(ctx, i.data(), &size_out, i.data(), i.size()))
			throw_openssl_error();
	}
}

std::vector<uint8_t> argon2(std::string pass, std::string salt, std::span<uint8_t> secret, size_t size)
{
	/* argon2 params, please refer to RFC9106 for recommended defaults */
	uint32_t lanes = 2;
	uint32_t threads = 2;
	uint32_t memcost = 65536;

	// if (OSSL_set_max_threads(nullptr, threads) != 1)
	// 	throw_openssl_error();

	std::array params{
	        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
	        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
	        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memcost),
	        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt.data(), salt.size()),
	        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, pass.data(), pass.size()),
	        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SECRET, secret.data(), secret.size()),
	        OSSL_PARAM_construct_end(),
	};

	details::kdf_context kdf{"ARGON2I"};

	std::vector<uint8_t> result;
	result.resize(size);
	if (EVP_KDF_derive(kdf, result.data(), size, params.data()) != 1)
		throw_openssl_error();

	return result;
}

} // namespace crypto

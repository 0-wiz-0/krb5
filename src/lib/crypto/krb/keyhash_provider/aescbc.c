/*
 * lib/crypto/keyhash_provider/aescbc.c
 *
 * Copyright 2008 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 */

#include "k5-int.h"
#include "keyhash_provider.h"
#include "hash_provider.h"
#include "enc_provider/enc_provider.h"
#include "../etypes.h"
#include "../aes/aes.h"
#include "../aead.h"
#include "../dk/dk.h"

#define K5CLENGTH 5 /* 32 bit net byte order integer + one byte seed */

static void xorblock(unsigned char *out, unsigned const char *in)
{
    int z;
    for (z = 0; z < BLOCK_SIZE; z++)
	out[z] ^= in[z];
}

static krb5_error_code
k5_aescbc_hash_iov (krb5_key key, krb5_keyusage usage,
		    const krb5_data *iv,
		    const krb5_crypto_iov *data, size_t num_data,
		    krb5_data *output)
{
    unsigned char constantdata[K5CLENGTH];
    krb5_error_code ret;
    krb5_data d1;
    krb5_key kc;
    int i;
    aes_ctx ctx;
    unsigned char blockY[BLOCK_SIZE];
    struct iov_block_state iov_state;

    if (output->length < BLOCK_SIZE)
	return KRB5_BAD_MSIZE;

    d1.data = (char *)constantdata;
    d1.length = K5CLENGTH;

    d1.data[0] = (usage >> 24) & 0xFF;
    d1.data[1] = (usage >> 16) & 0xFF;
    d1.data[2] = (usage >> 8 ) & 0xFF;
    d1.data[3] = (usage      ) & 0xFF;

    d1.data[4] = 0xCC;

    for (i = 0, kc = NULL; i < krb5_enctypes_length; i++) {
        if (krb5_enctypes_list[i].etype == krb5_k_key_enctype(NULL, key)) {
            ret = krb5_derive_key(krb5_enctypes_list[i].enc, key, &kc, &d1);
            if (ret != 0)
                return ret;
            break;
        }
    }

    if (kc == NULL)
        abort();

    if (aes_enc_key(key->keyblock.contents,
		    key->keyblock.length, &ctx) != aes_good)
	abort();

    if (iv != NULL)
	memcpy(blockY, iv->data, BLOCK_SIZE);
    else
	memset(blockY, 0, BLOCK_SIZE);

    IOV_BLOCK_STATE_INIT(&iov_state);

    /*
     * The CCM header may not fit in a block, because it includes a variable
     * length encoding of the associated data length. This encoding plus the
     * associated data itself is padded to the block size.
     */
    iov_state.include_sign_only = 1;
    iov_state.pad_to_boundary = 1;

    for (;;) {
	unsigned char blockB[BLOCK_SIZE];

	if (!krb5int_c_iov_get_block(blockB, BLOCK_SIZE, data, num_data, &iov_state))
	    break;

	xorblock(blockB, blockY);

	if (aes_enc_blk(blockB, blockY, &ctx) != aes_good)
	    abort();
    }

    output->length = BLOCK_SIZE;
    memcpy(output->data, blockY, BLOCK_SIZE);

    krb5_k_free_key(NULL, kc);

    return 0;
}

static krb5_error_code
k5_aescbc_hash (krb5_key key, krb5_keyusage usage,
		const krb5_data *iv,
		const krb5_data *input, krb5_data *output)
{
    krb5_crypto_iov iov[1];

    iov[0].flags = KRB5_CRYPTO_TYPE_DATA;
    iov[0].data = *input;

    return k5_aescbc_hash_iov(key, usage, iv, iov, 1, output);
}

const struct krb5_keyhash_provider krb5int_keyhash_aescbc_128 = {
    16,
    k5_aescbc_hash,
    NULL, /*checksum  again*/
    k5_aescbc_hash_iov,
    NULL  /*checksum  again */
};

const struct krb5_keyhash_provider krb5int_keyhash_aescbc_256 = {
    16,
    k5_aescbc_hash,
    NULL, /*checksum  again*/
    k5_aescbc_hash_iov,
    NULL  /*checksum  again */
};


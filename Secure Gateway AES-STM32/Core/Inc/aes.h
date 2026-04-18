#ifndef _AES_H_
#define _AES_H_

#include <stdint.h>
#include <stddef.h>
#include "buffer_pool.h"

#define AES_BLOCKLEN 16 // Block length in bytes - AES is 128b block only
#define AES_KEYLEN 16   // Key length in bytes
#define AES_keyExpSize 176

typedef struct
{
  uint8_t RoundKey[AES_keyExpSize];
  uint8_t Iv[AES_BLOCKLEN];
} AES_ctx;

void AES_init_ctx_iv(AES_ctx* ctx, const uint8_t* key, const uint8_t* iv);
void AES_ctx_set_iv(AES_ctx* ctx, const uint8_t* iv);

void AES_CBC_encrypt_buffer(AES_ctx* ctx, uint8_t* buf, size_t length);
void AES_CBC_PKCS7_Encrypt(AES_ctx* ctx, PacketBuffer* packet, uint16_t start_offset);

#endif // _AES_H_

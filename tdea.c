// See NIST SP 800-67

// The DES algorithm is an over-complicated horror which involves
// bit permutations for no apparent benefit.  It is ridiculously
// difficult to code efficiently.  This implementation is
// intended to be correct, and "not too bad" - having some of the
// permutations "built in" and using bitmaps where possible.
// Note that almost any other algorithm, eg AES or Blowfish, will
// be an order of magnitude faster - so don't use this unless you
// have to!

// 3DES/TDEA uses three applicatopns of DES back-to-back with
// different keys. The underlying DES algorithm is considered
// too weak for normal use on its own, but we do export an interface
// for it as it is needed for another horror - MSChapV2.

#include "tdea.h"

static inline ui32 get32(const ui8 *p)
{
  return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static inline ui64 get64(const ui8 *p)
{
  return ((ui64)get32(p) << 32) | get32(p + 4);
}

static inline void put32(ui32 v, ui8 *p)
{
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static inline void put64(ui64 v, ui8 *p)
{
  put32(v >> 32, p);
  put32(v, p + 4);
}

static inline void bitswap(ui32 *A, ui32 *B, ui32 mask, int shift)
{
  // swap mask bits in A with (mask << shift) bits in B
  ui32 diff = ((*B >> shift) ^ *A) & mask;
  *A ^= diff;
  *B ^= diff << shift;
}

static ui64 ipfwd(ui64 v)
{
  ui32 L = v >> 32;
  ui32 R = v;
  bitswap(&R, &L, 0x0f0f0f0f, 4);
  bitswap(&R, &L, 0x0000ffff, 16);
  bitswap(&L, &R, 0x33333333, 2);
  bitswap(&L, &R, 0x00ff00ff, 8);
  bitswap(&R, &L, 0x55555555, 1);
  return ((ui64)L << 32) | R;
}

static ui64 iprev(ui64 v)
{
  ui32 L = v >> 32;
  ui32 R = v;
  bitswap(&R, &L, 0x55555555, 1);
  bitswap(&L, &R, 0x00ff00ff, 8);
  bitswap(&L, &R, 0x33333333, 2);
  bitswap(&R, &L, 0x0000ffff, 16);
  bitswap(&R, &L, 0x0f0f0f0f, 4);
  return ((ui64)L << 32) | R;
}

// The following tables are generated from the "S" and "P"
// tables in the DES spec.  A routine to generate them from
// the spec data is included below in the standalone code

static const ui32 sp[8][64] =
{
  { // s0
    0x00808200, 0x00000000, 0x00008000, 0x00808202,
    0x00808002, 0x00008202, 0x00000002, 0x00008000,
    0x00000200, 0x00808200, 0x00808202, 0x00000200,
    0x00800202, 0x00808002, 0x00800000, 0x00000002,
    0x00000202, 0x00800200, 0x00800200, 0x00008200,
    0x00008200, 0x00808000, 0x00808000, 0x00800202,
    0x00008002, 0x00800002, 0x00800002, 0x00008002,
    0x00000000, 0x00000202, 0x00008202, 0x00800000,
    0x00008000, 0x00808202, 0x00000002, 0x00808000,
    0x00808200, 0x00800000, 0x00800000, 0x00000200,
    0x00808002, 0x00008000, 0x00008200, 0x00800002,
    0x00000200, 0x00000002, 0x00800202, 0x00008202,
    0x00808202, 0x00008002, 0x00808000, 0x00800202,
    0x00800002, 0x00000202, 0x00008202, 0x00808200,
    0x00000202, 0x00800200, 0x00800200, 0x00000000,
    0x00008002, 0x00008200, 0x00000000, 0x00808002
  },
  { // s1
    0x40084010, 0x40004000, 0x00004000, 0x00084010,
    0x00080000, 0x00000010, 0x40080010, 0x40004010,
    0x40000010, 0x40084010, 0x40084000, 0x40000000,
    0x40004000, 0x00080000, 0x00000010, 0x40080010,
    0x00084000, 0x00080010, 0x40004010, 0x00000000,
    0x40000000, 0x00004000, 0x00084010, 0x40080000,
    0x00080010, 0x40000010, 0x00000000, 0x00084000,
    0x00004010, 0x40084000, 0x40080000, 0x00004010,
    0x00000000, 0x00084010, 0x40080010, 0x00080000,
    0x40004010, 0x40080000, 0x40084000, 0x00004000,
    0x40080000, 0x40004000, 0x00000010, 0x40084010,
    0x00084010, 0x00000010, 0x00004000, 0x40000000,
    0x00004010, 0x40084000, 0x00080000, 0x40000010,
    0x00080010, 0x40004010, 0x40000010, 0x00080010,
    0x00084000, 0x00000000, 0x40004000, 0x00004010,
    0x40000000, 0x40080010, 0x40084010, 0x00084000
  },
  { // s2
    0x00000104, 0x04010100, 0x00000000, 0x04010004,
    0x04000100, 0x00000000, 0x00010104, 0x04000100,
    0x00010004, 0x04000004, 0x04000004, 0x00010000,
    0x04010104, 0x00010004, 0x04010000, 0x00000104,
    0x04000000, 0x00000004, 0x04010100, 0x00000100,
    0x00010100, 0x04010000, 0x04010004, 0x00010104,
    0x04000104, 0x00010100, 0x00010000, 0x04000104,
    0x00000004, 0x04010104, 0x00000100, 0x04000000,
    0x04010100, 0x04000000, 0x00010004, 0x00000104,
    0x00010000, 0x04010100, 0x04000100, 0x00000000,
    0x00000100, 0x00010004, 0x04010104, 0x04000100,
    0x04000004, 0x00000100, 0x00000000, 0x04010004,
    0x04000104, 0x00010000, 0x04000000, 0x04010104,
    0x00000004, 0x00010104, 0x00010100, 0x04000004,
    0x04010000, 0x04000104, 0x00000104, 0x04010000,
    0x00010104, 0x00000004, 0x04010004, 0x00010100
  },
  { // s3
    0x80401000, 0x80001040, 0x80001040, 0x00000040,
    0x00401040, 0x80400040, 0x80400000, 0x80001000,
    0x00000000, 0x00401000, 0x00401000, 0x80401040,
    0x80000040, 0x00000000, 0x00400040, 0x80400000,
    0x80000000, 0x00001000, 0x00400000, 0x80401000,
    0x00000040, 0x00400000, 0x80001000, 0x00001040,
    0x80400040, 0x80000000, 0x00001040, 0x00400040,
    0x00001000, 0x00401040, 0x80401040, 0x80000040,
    0x00400040, 0x80400000, 0x00401000, 0x80401040,
    0x80000040, 0x00000000, 0x00000000, 0x00401000,
    0x00001040, 0x00400040, 0x80400040, 0x80000000,
    0x80401000, 0x80001040, 0x80001040, 0x00000040,
    0x80401040, 0x80000040, 0x80000000, 0x00001000,
    0x80400000, 0x80001000, 0x00401040, 0x80400040,
    0x80001000, 0x00001040, 0x00400000, 0x80401000,
    0x00000040, 0x00400000, 0x00001000, 0x00401040
  },
  { // s4
    0x00000080, 0x01040080, 0x01040000, 0x21000080,
    0x00040000, 0x00000080, 0x20000000, 0x01040000,
    0x20040080, 0x00040000, 0x01000080, 0x20040080,
    0x21000080, 0x21040000, 0x00040080, 0x20000000,
    0x01000000, 0x20040000, 0x20040000, 0x00000000,
    0x20000080, 0x21040080, 0x21040080, 0x01000080,
    0x21040000, 0x20000080, 0x00000000, 0x21000000,
    0x01040080, 0x01000000, 0x21000000, 0x00040080,
    0x00040000, 0x21000080, 0x00000080, 0x01000000,
    0x20000000, 0x01040000, 0x21000080, 0x20040080,
    0x01000080, 0x20000000, 0x21040000, 0x01040080,
    0x20040080, 0x00000080, 0x01000000, 0x21040000,
    0x21040080, 0x00040080, 0x21000000, 0x21040080,
    0x01040000, 0x00000000, 0x20040000, 0x21000000,
    0x00040080, 0x01000080, 0x20000080, 0x00040000,
    0x00000000, 0x20040000, 0x01040080, 0x20000080
  },
  { // s5
    0x10000008, 0x10200000, 0x00002000, 0x10202008,
    0x10200000, 0x00000008, 0x10202008, 0x00200000,
    0x10002000, 0x00202008, 0x00200000, 0x10000008,
    0x00200008, 0x10002000, 0x10000000, 0x00002008,
    0x00000000, 0x00200008, 0x10002008, 0x00002000,
    0x00202000, 0x10002008, 0x00000008, 0x10200008,
    0x10200008, 0x00000000, 0x00202008, 0x10202000,
    0x00002008, 0x00202000, 0x10202000, 0x10000000,
    0x10002000, 0x00000008, 0x10200008, 0x00202000,
    0x10202008, 0x00200000, 0x00002008, 0x10000008,
    0x00200000, 0x10002000, 0x10000000, 0x00002008,
    0x10000008, 0x10202008, 0x00202000, 0x10200000,
    0x00202008, 0x10202000, 0x00000000, 0x10200008,
    0x00000008, 0x00002000, 0x10200000, 0x00202008,
    0x00002000, 0x00200008, 0x10002008, 0x00000000,
    0x10202000, 0x10000000, 0x00200008, 0x10002008
  },
  { // s6
    0x00100000, 0x02100001, 0x02000401, 0x00000000,
    0x00000400, 0x02000401, 0x00100401, 0x02100400,
    0x02100401, 0x00100000, 0x00000000, 0x02000001,
    0x00000001, 0x02000000, 0x02100001, 0x00000401,
    0x02000400, 0x00100401, 0x00100001, 0x02000400,
    0x02000001, 0x02100000, 0x02100400, 0x00100001,
    0x02100000, 0x00000400, 0x00000401, 0x02100401,
    0x00100400, 0x00000001, 0x02000000, 0x00100400,
    0x02000000, 0x00100400, 0x00100000, 0x02000401,
    0x02000401, 0x02100001, 0x02100001, 0x00000001,
    0x00100001, 0x02000000, 0x02000400, 0x00100000,
    0x02100400, 0x00000401, 0x00100401, 0x02100400,
    0x00000401, 0x02000001, 0x02100401, 0x02100000,
    0x00100400, 0x00000000, 0x00000001, 0x02100401,
    0x00000000, 0x00100401, 0x02100000, 0x00000400,
    0x02000001, 0x02000400, 0x00000400, 0x00100001
  },
  { // s7
    0x08000820, 0x00000800, 0x00020000, 0x08020820,
    0x08000000, 0x08000820, 0x00000020, 0x08000000,
    0x00020020, 0x08020000, 0x08020820, 0x00020800,
    0x08020800, 0x00020820, 0x00000800, 0x00000020,
    0x08020000, 0x08000020, 0x08000800, 0x00000820,
    0x00020800, 0x00020020, 0x08020020, 0x08020800,
    0x00000820, 0x00000000, 0x00000000, 0x08020020,
    0x08000020, 0x08000800, 0x00020820, 0x00020000,
    0x00020820, 0x00020000, 0x08020800, 0x00000800,
    0x00000020, 0x08020020, 0x00000800, 0x00020820,
    0x08000800, 0x00000020, 0x08000020, 0x08020000,
    0x08020020, 0x08000000, 0x00020000, 0x08000820,
    0x00000000, 0x08020820, 0x00020020, 0x08000020,
    0x08020000, 0x08000800, 0x08000820, 0x00000000,
    0x08020820, 0x00020800, 0x00020800, 0x00000820,
    0x00000820, 0x00020020, 0x08000000, 0x08020800
  }
};

static ui32 f(ui32 R, const ui32 *keypair)
{
  R = (R >> 31) | (R << 1);
  ui32 L = ((R >> 4) | (R << 28)) ^ keypair[0];
  R ^= keypair[1];
  ui32 v = sp[0][(L >> 24) & 0x3f];
  v |= sp[1][(R >> 24) & 0x3f];
  v |= sp[2][(L >> 16) & 0x3f];
  v |= sp[3][(R >> 16) & 0x3f];
  v |= sp[4][(L >> 8) & 0x3f];
  v |= sp[5][(R >> 8) & 0x3f];
  v |= sp[6][L & 0x3f];
  v |= sp[7][R & 0x3f];
  return v;
}

static ui64 TDEA_EncryptBody(const TDEA_DESKEY *key, ui64 v)
{
  ui32 L = v >> 32;
  ui32 R = v;
  int k;
  for (k = 0; k < 16; k++)
  {
    ui32 t = R;
    R = L ^ f(R, &key->data[2 * k]);
    L = t;
  }
  return ((ui64)R << 32) | L;
}

static ui64 TDEA_DecryptBody(const TDEA_DESKEY *key, ui64 v)
{
  ui32 L = v >> 32;
  ui32 R = v;
  int k;
  for (k = 15; k >= 0; k--)
  {
    ui32 t = R;
    R = L ^ f(R, &key->data[2 * k]);
    L = t;
  }
  return ((ui64)R << 32) | L;
}

// The key expansion algorithm doesn't need to be particularly
// efficient as it should only be done once when the key is
// first established.

static ui64 pc1(ui64 v)
{
  const ui8 pc1perm[56] =
  {
    57, 49, 41, 33, 25, 17,  9,  1, 58, 50, 42, 34, 26, 18,  
    10,  2, 59, 51, 43, 35, 27, 19, 11,  3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,  7, 62, 54, 46, 38, 30, 22,
    14,  6, 61, 53, 45, 37, 29, 21, 13,  5, 28, 20, 12,  4 
  };
  ui64 r = 0;
  int k;
  for (k = 0; k < 56; k++)
    if (v & (1ULL << (64 - pc1perm[55 - k]))) r |= 1ULL << k;
  return r;
}

static ui64 pc2(ui64 v)
{
  const ui8 pc2perm[48] =
  {
    14, 17, 11, 24,  1,  5,  3, 28, 15,  6, 21, 10,
    23, 19, 12,  4, 26,  8, 16,  7, 27, 20, 13,  2,
    41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32
  };
  ui64 r = 0;
  int k;
  for (k = 0; k < 48; k++)
    if (v & (1ULL << (56 - pc2perm[47 - k]))) r |= 1ULL << k;
  return r;
}

static void TDEA_GenDESKey(TDEA_DESKEY *deskey, const ui8 *key)
{
  ui8 shifts[16] = { 1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1 };
  int k;
  ui64 wkey = pc1(get64(key)); 
  for (k = 0; k < 16; k++)
  {
    wkey <<= shifts[k];
    ui64 m = shifts[k] > 1 ? 3 : 1;
    wkey |= (wkey >> 28) & m;
    wkey &= ~(m << 28);
    wkey |= (wkey >> 28) & (m << 28);
    ui64 rkey = pc2(wkey);
    deskey->data[2 * k] = ((rkey >> 42) & 0x3f) << 24 |
                          ((rkey >> 30) & 0x3f) << 16 |
                          ((rkey >> 18) & 0x3f) <<  8 |
                          ((rkey >>  6) & 0x3f);
    deskey->data[2 * k + 1] = ((rkey >> 36) & 0x3f) << 24 |
                              ((rkey >> 24) & 0x3f) << 16 |
                              ((rkey >> 12) & 0x3f) <<  8 |
                              ((rkey >>  0) & 0x3f);
  }
}

// Single-pass ECB DES interface.
ui64 DES_Encrypt(ui64 key, ui64 data)
{
  ui8 k[8];
  TDEA_DESKEY deskey;
  put64(key, k);
  TDEA_GenDESKey(&deskey, k);
  return iprev(TDEA_EncryptBody(&deskey, ipfwd(data)));
}

ui64 DES_Decrypt(ui64 key, ui64 data)
{
  ui8 k[8];
  TDEA_DESKEY deskey;
  put64(key, k);
  TDEA_GenDESKey(&deskey, k);
  return iprev(TDEA_DecryptBody(&deskey, ipfwd(data)));
}

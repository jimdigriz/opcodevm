/* /usr/include/x86_64-linux-gnu/bits/byteswap.h */
#define __bswap_constant_16(x) \
     ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#define __bswap_constant_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
# define __bswap_constant_64(x) \
     ((((x) & 0xff00000000000000ull) >> 56)    \
      | (((x) & 0x00ff000000000000ull) >> 40)  \
      | (((x) & 0x0000ff0000000000ull) >> 24)  \
      | (((x) & 0x000000ff00000000ull) >> 8)   \
      | (((x) & 0x00000000ff000000ull) << 8)   \
      | (((x) & 0x0000000000ff0000ull) << 24)  \
      | (((x) & 0x000000000000ff00ull) << 40)  \
      | (((x) & 0x00000000000000ffull) << 56)) \

#define FUNC(x, y)	__kernel void bswap_##x(__global y##16 *data)		\
			{							\
				__const ulong base = get_global_id(0);		\
										\
				data[base] = __bswap_constant_##x(data[base]);	\
			}

/*
 * only 32bit supported for now as vector casting for the
 * bit operations does not work for 16 and 64bit
 */
//FUNC(16, ushort)
FUNC(32, uint)
//FUNC(64, ulong)

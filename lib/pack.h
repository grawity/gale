#ifndef PACK_H
#define PACK_H

/* compatibility naming -- should go away */

#include "gale/misc.h"

#define _ga_unpack_copy gale_unpack_copy
#define _ga_unpack_compare gale_unpack_compare
#define _ga_pack_copy gale_pack_copy
#define _ga_copy_size gale_copy_size

#define _ga_unpack_rle gale_unpack_rle
#define _ga_pack_rle gale_pack_rle
#define _ga_rle_size gale_rle_size

#define _ga_unpack_str gale_unpack_str
#define _ga_pack_str gale_pack_str
#define _ga_str_size gale_str_size

#define _ga_unpack_u32 gale_unpack_u32
#define _ga_pack_u32 gale_pack_u32
#define _ga_u32_size gale_u32_size()

#endif
